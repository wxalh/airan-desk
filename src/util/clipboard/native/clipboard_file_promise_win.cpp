#include "util/clipboard/native/clipboard_file_promise.h"

#include "common/logger_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
#include <memory>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>
#include <objidl.h>
#include <shlobj.h>
#include <shldisp.h>
#include <shobjidl.h>

namespace
{
#ifndef FD_PROGRESSUI
constexpr DWORD FD_PROGRESSUI = 0x00004000;
#endif

struct PromiseCacheState
{
    QMutex mutex;
    QWaitCondition dataReady;
    qint64 availableBytes = 0;
    bool prefetchStarted = false;
    bool complete = false;
    bool failed = false;
    bool cancelled = false;
    QString error;
};

struct PromiseFile
{
    ClipboardFilePromiseItem item;
    QString cacheFilePath;
    std::shared_ptr<PromiseCacheState> cacheState;
};

std::mutex g_promiseCacheRegistryMutex;
std::map<QString, std::vector<std::weak_ptr<PromiseCacheState>>> g_promiseCacheRegistry;

void registerPromiseCacheState(const QString &cacheRoot,
                               const std::shared_ptr<PromiseCacheState> &state)
{
    if (cacheRoot.isEmpty() || !state)
        return;
    std::lock_guard<std::mutex> locker(g_promiseCacheRegistryMutex);
    g_promiseCacheRegistry[QDir::cleanPath(cacheRoot)].push_back(state);
}

void cancelPromiseCacheRoot(const QString &cacheRoot)
{
    if (cacheRoot.isEmpty())
        return;

    std::vector<std::shared_ptr<PromiseCacheState>> states;
    {
        std::lock_guard<std::mutex> locker(g_promiseCacheRegistryMutex);
        const QString key = QDir::cleanPath(cacheRoot);
        auto it = g_promiseCacheRegistry.find(key);
        if (it == g_promiseCacheRegistry.end())
            return;
        for (const auto &weakState : it->second)
        {
            if (const auto state = weakState.lock())
                states.push_back(state);
        }
        g_promiseCacheRegistry.erase(it);
    }

    for (const auto &state : states)
    {
        QMutexLocker locker(&state->mutex);
        state->cancelled = true;
        state->error = QCoreApplication::translate("ClipboardFilePromise", "Clipboard promise was cancelled.");
        state->dataReady.wakeAll();
    }
}

void startPromisePrefetch(const PromiseFile &file,
                          const ClipboardFilePromise::ReadFileChunk &reader)
{
    const std::shared_ptr<PromiseCacheState> state = file.cacheState;
    if (!state || file.item.isDirectory || file.cacheFilePath.isEmpty())
        return;

    {
        QMutexLocker locker(&state->mutex);
        if (state->prefetchStarted)
            return;
        state->prefetchStarted = true;
    }

    std::thread([file, reader, state]() {
        QFileInfo cacheInfo(file.cacheFilePath);
        if (!QDir().mkpath(cacheInfo.absolutePath()))
        {
            QMutexLocker locker(&state->mutex);
            state->failed = true;
            state->error = QCoreApplication::translate("ClipboardFilePromise", "Cannot create clipboard promise cache directory.");
            state->dataReady.wakeAll();
            return;
        }

        QFile cacheFile(file.cacheFilePath);
        if (!cacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QMutexLocker locker(&state->mutex);
            state->failed = true;
            state->error = cacheFile.errorString();
            state->dataReady.wakeAll();
            return;
        }

        constexpr qint64 kPrefetchChunkBytes = 64 * 1024;
        qint64 offset = 0;
        while (offset < file.item.size)
        {
            {
                QMutexLocker locker(&state->mutex);
                if (state->cancelled)
                    return;
            }

            bool ok = false;
            QString error;
            const qint64 requestBytes = (std::min<qint64>)(kPrefetchChunkBytes, file.item.size - offset);
            const QByteArray chunk = reader
                                         ? reader(file.item.sourcePath, offset, requestBytes, &ok, &error)
                                         : QByteArray();
            if (!ok || chunk.isEmpty() || chunk.size() > requestBytes)
            {
                QMutexLocker locker(&state->mutex);
                if (state->cancelled)
                    return;
                state->failed = true;
                state->error = error.isEmpty()
                                   ? QCoreApplication::translate("ClipboardFilePromise", "Clipboard promise prefetch returned no data before EOF.")
                                   : error;
                state->dataReady.wakeAll();
                return;
            }

            {
                QMutexLocker locker(&state->mutex);
                if (state->cancelled)
                    return;
            }
            if (cacheFile.write(chunk) != chunk.size() || !cacheFile.flush())
            {
                QMutexLocker locker(&state->mutex);
                if (state->cancelled)
                    return;
                state->failed = true;
                state->error = cacheFile.errorString();
                state->dataReady.wakeAll();
                return;
            }

            offset += chunk.size();
            {
                QMutexLocker locker(&state->mutex);
                state->availableBytes = offset;
                state->dataReady.wakeAll();
            }
        }

        cacheFile.close();
        QMutexLocker locker(&state->mutex);
        if (state->cancelled)
            return;
        state->complete = true;
        state->availableBytes = file.item.size;
        state->dataReady.wakeAll();
    }).detach();
}

CLIPFORMAT fileDescriptorFormat()
{
    static const CLIPFORMAT format = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW));
    return format;
}

CLIPFORMAT fileContentsFormat()
{
    static const CLIPFORMAT format = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILECONTENTS));
    return format;
}

CLIPFORMAT preferredDropEffectFormat()
{
    static const CLIPFORMAT format = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT));
    return format;
}

QString sanitizePathSegment(QString segment)
{
    segment = segment.trimmed();
    if (segment.isEmpty() || segment == QStringLiteral(".") || segment == QStringLiteral(".."))
        return QStringLiteral("item");

    static const QChar replacements[] = {
        QLatin1Char('<'), QLatin1Char('>'), QLatin1Char(':'), QLatin1Char('"'),
        QLatin1Char('|'), QLatin1Char('?'), QLatin1Char('*')
    };
    for (const QChar ch : replacements)
        segment.replace(ch, QLatin1Char('_'));
    return segment;
}

QString cleanDisplayName(const ClipboardFilePromiseItem &item)
{
    QString name = item.displayName.trimmed();
    if (name.isEmpty())
        name = QFileInfo(item.sourcePath).fileName();
    if (name.isEmpty())
        name = QStringLiteral("item");
    name.replace(QLatin1Char('/'), QLatin1Char('\\'));

    QStringList cleanedSegments;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QStringList segments = name.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
#else
    const QStringList segments = name.split(QLatin1Char('\\'), QString::SkipEmptyParts);
#endif
    for (const QString &segment : segments)
        cleanedSegments.append(sanitizePathSegment(segment));
    if (cleanedSegments.isEmpty())
        cleanedSegments.append(QStringLiteral("item"));
    return cleanedSegments.join(QLatin1Char('\\'));
}

void setMediumEmpty(STGMEDIUM *medium)
{
    if (!medium)
        return;
    medium->tymed = TYMED_NULL;
    medium->pUnkForRelease = nullptr;
    medium->hGlobal = nullptr;
}

bool matchesFormat(const FORMATETC *requested, CLIPFORMAT format, DWORD tymed)
{
    return requested &&
           requested->cfFormat == format &&
           requested->dwAspect == DVASPECT_CONTENT &&
           (requested->tymed & tymed);
}

class PromiseFileStream : public IStream
{
public:
    PromiseFileStream(const PromiseFile &file, const ClipboardFilePromise::ReadFileChunk &reader)
        : m_file(file),
          m_reader(reader)
    {
        startPromisePrefetch(m_file, m_reader);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
    {
        if (!object)
            return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_ISequentialStream || iid == IID_IStream)
        {
            *object = static_cast<IStream *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++m_refs;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG refs = --m_refs;
        if (refs == 0)
            delete this;
        return refs;
    }

    HRESULT STDMETHODCALLTYPE Read(void *pv, ULONG cb, ULONG *pcbRead) override
    {
        if (pcbRead)
            *pcbRead = 0;
        if (!pv && cb > 0)
            return STG_E_INVALIDPOINTER;
        if (cb == 0)
            return S_OK;

        QMutexLocker streamLocker(&m_mutex);
        if (m_file.item.isDirectory)
            return S_FALSE;
        if (m_pos >= m_file.item.size)
            return S_FALSE;
        if (!m_file.cacheState || m_file.cacheFilePath.isEmpty())
            return STG_E_READFAULT;

        startPromisePrefetch(m_file, m_reader);
        const qint64 requestedEnd = (std::min<qint64>)(m_file.item.size,
                                                       m_pos + static_cast<qint64>(cb));
        qint64 readableEnd = 0;
        {
            QMutexLocker cacheLocker(&m_file.cacheState->mutex);
            while (m_file.cacheState->availableBytes < requestedEnd &&
                   !m_file.cacheState->complete &&
                   !m_file.cacheState->failed &&
                   !m_file.cacheState->cancelled)
            {
                m_file.cacheState->dataReady.wait(&m_file.cacheState->mutex, 1000);
            }

            if ((m_file.cacheState->failed || m_file.cacheState->cancelled) &&
                m_file.cacheState->availableBytes <= m_pos)
            {
                LOG_WARN("Clipboard file promise cache failed: path={}, offset={}, error={}",
                         m_file.item.sourcePath,
                         m_pos,
                         m_file.cacheState->error);
                return STG_E_READFAULT;
            }
            readableEnd = (std::min<qint64>)(requestedEnd, m_file.cacheState->availableBytes);
        }

        if (readableEnd <= m_pos)
            return m_pos >= m_file.item.size ? S_FALSE : STG_E_READFAULT;

        QFile cacheFile(m_file.cacheFilePath);
        if (!cacheFile.open(QIODevice::ReadOnly) || !cacheFile.seek(m_pos))
        {
            LOG_WARN("Cannot read clipboard promise cache: path={}, error={}",
                     m_file.cacheFilePath,
                     cacheFile.errorString());
            return STG_E_READFAULT;
        }

        const qint64 toRead = readableEnd - m_pos;
        const QByteArray chunk = cacheFile.read(toRead);
        if (chunk.isEmpty())
            return STG_E_READFAULT;

        const ULONG copyBytes = static_cast<ULONG>((std::min<qint64>)(chunk.size(), cb));
        std::memcpy(pv, chunk.constData(), copyBytes);
        m_pos += copyBytes;
        if (pcbRead)
            *pcbRead = copyBytes;

        if (m_pos >= m_file.item.size)
            return copyBytes == cb ? S_OK : S_FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Write(const void *, ULONG, ULONG *) override
    {
        return STG_E_ACCESSDENIED;
    }

    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *newPosition) override
    {
        QMutexLocker locker(&m_mutex);
        qint64 target = 0;
        if (origin == STREAM_SEEK_SET)
            target = move.QuadPart;
        else if (origin == STREAM_SEEK_CUR)
            target = m_pos + move.QuadPart;
        else if (origin == STREAM_SEEK_END)
            target = m_file.item.size + move.QuadPart;
        else
            return STG_E_INVALIDFUNCTION;

        if (target < 0)
            return STG_E_INVALIDFUNCTION;
        m_pos = target;
        if (newPosition)
            newPosition->QuadPart = static_cast<ULONGLONG>(m_pos);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override
    {
        return STG_E_ACCESSDENIED;
    }

    HRESULT STDMETHODCALLTYPE CopyTo(IStream *target, ULARGE_INTEGER cb, ULARGE_INTEGER *readBytes, ULARGE_INTEGER *writtenBytes) override
    {
        if (readBytes)
            readBytes->QuadPart = 0;
        if (writtenBytes)
            writtenBytes->QuadPart = 0;
        if (!target)
            return STG_E_INVALIDPOINTER;

        char buffer[64 * 1024];
        ULONGLONG remaining = cb.QuadPart;
        while (remaining > 0)
        {
            ULONG chunk = static_cast<ULONG>((std::min<ULONGLONG>)(remaining, sizeof(buffer)));
            ULONG didRead = 0;
            HRESULT hr = Read(buffer, chunk, &didRead);
            if (FAILED(hr))
                return hr;
            if (didRead == 0)
                break;

            ULONG didWrite = 0;
            hr = target->Write(buffer, didRead, &didWrite);
            if (FAILED(hr))
                return hr;
            if (readBytes)
                readBytes->QuadPart += didRead;
            if (writtenBytes)
                writtenBytes->QuadPart += didWrite;
            remaining -= didRead;
            if (didWrite != didRead)
                return STG_E_WRITEFAULT;
            if (hr == S_FALSE)
                break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Commit(DWORD) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Revert() override
    {
        return STG_E_REVERTED;
    }

    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override
    {
        return STG_E_INVALIDFUNCTION;
    }

    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override
    {
        return STG_E_INVALIDFUNCTION;
    }

    HRESULT STDMETHODCALLTYPE Stat(STATSTG *stat, DWORD) override
    {
        if (!stat)
            return STG_E_INVALIDPOINTER;
        std::memset(stat, 0, sizeof(STATSTG));
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = static_cast<ULONGLONG>((std::max<qint64>)(0, m_file.item.size));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IStream **stream) override
    {
        if (!stream)
            return STG_E_INVALIDPOINTER;
        *stream = new (std::nothrow) PromiseFileStream(m_file, m_reader);
        return *stream ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::atomic<ULONG> m_refs{1};
    PromiseFile m_file;
    ClipboardFilePromise::ReadFileChunk m_reader;
    qint64 m_pos = 0;
    QMutex m_mutex;
};

class FormatEnumerator : public IEnumFORMATETC
{
public:
    explicit FormatEnumerator(const std::vector<FORMATETC> &formats)
        : m_formats(formats)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
    {
        if (!object)
            return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IEnumFORMATETC)
        {
            *object = static_cast<IEnumFORMATETC *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++m_refs;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG refs = --m_refs;
        if (refs == 0)
            delete this;
        return refs;
    }

    HRESULT STDMETHODCALLTYPE Next(ULONG count, FORMATETC *formats, ULONG *fetched) override
    {
        if (fetched)
            *fetched = 0;
        if (!formats)
            return E_POINTER;

        ULONG copied = 0;
        while (copied < count && m_index < m_formats.size())
            formats[copied++] = m_formats[m_index++];

        if (fetched)
            *fetched = copied;
        return copied == count ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override
    {
        m_index = (std::min<size_t>)(m_index + count, m_formats.size());
        return m_index < m_formats.size() ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Reset() override
    {
        m_index = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC **enumerator) override
    {
        if (!enumerator)
            return E_POINTER;
        auto *clone = new (std::nothrow) FormatEnumerator(m_formats);
        if (!clone)
            return E_OUTOFMEMORY;
        clone->m_index = m_index;
        *enumerator = clone;
        return S_OK;
    }

private:
    std::atomic<ULONG> m_refs{1};
    std::vector<FORMATETC> m_formats;
    size_t m_index = 0;
};

class PromiseDropSource : public IDropSource
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
    {
        if (!object)
            return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDropSource)
        {
            *object = static_cast<IDropSource *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++m_refs;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG refs = --m_refs;
        if (refs == 0)
            delete this;
        return refs;
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed, DWORD keyState) override
    {
        if (escapePressed)
            return DRAGDROP_S_CANCEL;
        if (!(keyState & MK_LBUTTON))
            return DRAGDROP_S_DROP;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override
    {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    std::atomic<ULONG> m_refs{1};
};

class PromiseDataObject : public IDataObject, public IDataObjectAsyncCapability
{
public:
    PromiseDataObject(const QList<ClipboardFilePromiseItem> &items,
                      const QString &cacheRoot,
                      const ClipboardFilePromise::ReadFileChunk &reader)
        : m_cacheRoot(cacheRoot),
          m_reader(reader)
    {
        QSet<QString> usedDisplayNames;
        for (const ClipboardFilePromiseItem &item : items)
        {
            PromiseFile file;
            file.item = item;
            file.item.displayName = cleanDisplayName(item);
            const QString originalDisplayName = file.item.displayName;
            int duplicateIndex = 2;
            while (usedDisplayNames.contains(file.item.displayName.toCaseFolded()))
            {
                const int separator = originalDisplayName.lastIndexOf(QLatin1Char('.'));
                const QString base = separator > 0 ? originalDisplayName.left(separator) : originalDisplayName;
                const QString suffix = separator > 0 ? originalDisplayName.mid(separator) : QString();
                file.item.displayName = QStringLiteral("%1 (%2)%3")
                                            .arg(base)
                                            .arg(duplicateIndex++)
                                            .arg(suffix);
            }
            usedDisplayNames.insert(file.item.displayName.toCaseFolded());
            if (!m_cacheRoot.isEmpty() && !file.item.isDirectory)
            {
                // Use an ordinal cache name rather than the display name.  Windows
                // paths are case-insensitive, so two remote files that differ only
                // by case would otherwise share one cache file during a multi-file
                // drag and corrupt each other's stream.
                file.cacheFilePath = QDir(m_cacheRoot).filePath(
                    QStringLiteral("%1.bin").arg(static_cast<qulonglong>(m_files.size()), 8, 10, QLatin1Char('0')));
                file.cacheState = std::make_shared<PromiseCacheState>();
                registerPromiseCacheState(m_cacheRoot, file.cacheState);
            }
            m_files.push_back(file);
        }

        FORMATETC descriptor{};
        descriptor.cfFormat = fileDescriptorFormat();
        descriptor.dwAspect = DVASPECT_CONTENT;
        descriptor.lindex = -1;
        descriptor.tymed = TYMED_HGLOBAL;
        m_formats.push_back(descriptor);

        FORMATETC dropEffect{};
        dropEffect.cfFormat = preferredDropEffectFormat();
        dropEffect.dwAspect = DVASPECT_CONTENT;
        dropEffect.lindex = -1;
        dropEffect.tymed = TYMED_HGLOBAL;
        m_formats.push_back(dropEffect);

        for (size_t i = 0; i < m_files.size(); ++i)
        {
            if (m_files[i].item.isDirectory)
                continue;

            FORMATETC contents{};
            contents.cfFormat = fileContentsFormat();
            contents.dwAspect = DVASPECT_CONTENT;
            contents.lindex = static_cast<LONG>(i);
            contents.tymed = TYMED_ISTREAM;
            m_formats.push_back(contents);
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
    {
        if (!object)
            return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDataObject)
        {
            *object = static_cast<IDataObject *>(this);
            AddRef();
            return S_OK;
        }
        if (iid == IID_IDataObjectAsyncCapability)
        {
            *object = static_cast<IDataObjectAsyncCapability *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++m_refs;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG refs = --m_refs;
        if (refs == 0)
            delete this;
        return refs;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *format, STGMEDIUM *medium) override
    {
        if (!format || !medium)
            return E_POINTER;
        setMediumEmpty(medium);

        if (matchesFormat(format, fileDescriptorFormat(), TYMED_HGLOBAL))
            return getFileDescriptor(medium);

        if (matchesFormat(format, preferredDropEffectFormat(), TYMED_HGLOBAL))
            return getDropEffect(medium);

        if (matchesFormat(format, fileContentsFormat(), TYMED_ISTREAM))
        {
            int index = format->lindex;
            if (index < 0 || index >= static_cast<int>(m_files.size()))
                return DV_E_LINDEX;
            if (m_files[static_cast<size_t>(index)].item.isDirectory)
                return DV_E_FORMATETC;

            PromiseFileStream *stream = new (std::nothrow) PromiseFileStream(m_files[static_cast<size_t>(index)], m_reader);
            if (!stream)
                return E_OUTOFMEMORY;
            medium->tymed = TYMED_ISTREAM;
            medium->pstm = stream;
            medium->pUnkForRelease = nullptr;
            return S_OK;
        }

        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override
    {
        return DATA_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *format) override
    {
        if (matchesFormat(format, fileDescriptorFormat(), TYMED_HGLOBAL))
            return S_OK;
        if (matchesFormat(format, preferredDropEffectFormat(), TYMED_HGLOBAL))
            return S_OK;
        if (matchesFormat(format, fileContentsFormat(), TYMED_ISTREAM))
        {
            if (format->lindex < 0 || format->lindex >= static_cast<LONG>(m_files.size()))
                return DV_E_LINDEX;
            if (m_files[static_cast<size_t>(format->lindex)].item.isDirectory)
                return DV_E_FORMATETC;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *, FORMATETC *out) override
    {
        if (out)
            out->ptd = nullptr;
        return DATA_S_SAMEFORMATETC;
    }

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *format, STGMEDIUM *medium, BOOL release) override
    {
        if (release && medium)
            ReleaseStgMedium(medium);
        if (!format)
            return E_POINTER;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC **enumerator) override
    {
        if (!enumerator)
            return E_POINTER;
        *enumerator = nullptr;
        if (direction != DATADIR_GET)
            return E_NOTIMPL;
        *enumerator = new (std::nothrow) FormatEnumerator(m_formats);
        return *enumerator ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL async) override
    {
        m_asyncMode = async ? true : false;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL *async) override
    {
        if (!async)
            return E_POINTER;
        *async = m_asyncMode ? TRUE : FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx *) override
    {
        // Explorer may show an overwrite prompt and only fetch the stream after
        // that prompt closes. Keep the data object alive until EndOperation.
        if (!m_asyncReferenceHeld)
        {
            AddRef();
            m_asyncReferenceHeld = true;
        }
        m_inOperation = true;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE InOperation(BOOL *inOperation) override
    {
        if (!inOperation)
            return E_POINTER;
        *inOperation = m_inOperation ? TRUE : FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE EndOperation(HRESULT, IBindCtx *, DWORD) override
    {
        m_inOperation = false;
        if (m_asyncReferenceHeld)
        {
            m_asyncReferenceHeld = false;
            Release();
        }
        return S_OK;
    }

private:
    HRESULT getDropEffect(STGMEDIUM *medium)
    {
        HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
        if (!global)
            return STG_E_MEDIUMFULL;

        auto *effect = static_cast<DWORD *>(GlobalLock(global));
        if (!effect)
        {
            GlobalFree(global);
            return STG_E_MEDIUMFULL;
        }
        *effect = DROPEFFECT_COPY;
        GlobalUnlock(global);

        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = global;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    HRESULT getFileDescriptor(STGMEDIUM *medium)
    {
        const size_t count = m_files.size();
        const SIZE_T bytes = sizeof(FILEGROUPDESCRIPTORW) +
                             (count > 0 ? (count - 1) * sizeof(FILEDESCRIPTORW) : 0);
        HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        if (!global)
            return STG_E_MEDIUMFULL;

        auto *group = static_cast<FILEGROUPDESCRIPTORW *>(GlobalLock(global));
        if (!group)
        {
            GlobalFree(global);
            return STG_E_MEDIUMFULL;
        }

        group->cItems = static_cast<UINT>(count);
        for (size_t i = 0; i < count; ++i)
        {
            FILEDESCRIPTORW &descriptor = group->fgd[i];
            descriptor.dwFlags = FD_ATTRIBUTES;
            descriptor.dwFlags |= FD_PROGRESSUI;
            descriptor.dwFileAttributes = m_files[i].item.isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            const quint64 size = static_cast<quint64>((std::max<qint64>)(0, m_files[i].item.size));
            if (!m_files[i].item.isDirectory)
            {
                descriptor.dwFlags |= FD_FILESIZE;
                descriptor.nFileSizeHigh = static_cast<DWORD>((size >> 32) & 0xffffffff);
                descriptor.nFileSizeLow = static_cast<DWORD>(size & 0xffffffff);
            }
            const std::wstring wideName = m_files[i].item.displayName.toStdWString();
            wcsncpy_s(descriptor.cFileName, MAX_PATH, wideName.c_str(), _TRUNCATE);
        }

        GlobalUnlock(global);
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = global;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    std::atomic<ULONG> m_refs{1};
    std::vector<PromiseFile> m_files;
    std::vector<FORMATETC> m_formats;
    QString m_cacheRoot;
    ClipboardFilePromise::ReadFileChunk m_reader;
    bool m_asyncMode = true;
    bool m_inOperation = false;
    bool m_asyncReferenceHeld = false;
};

bool installNativePromise(const QList<ClipboardFilePromiseItem> &items,
                          const QString &cacheRoot,
                          const ClipboardFilePromise::ReadFileChunk &readFileChunk,
                          QString *errorMessage)
{
    if (items.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "No file promises were provided.");
        return false;
    }
    if (cacheRoot.isEmpty() || !QDir().mkpath(cacheRoot))
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "Cannot create clipboard promise cache directory.");
        return false;
    }
    if (!readFileChunk)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "No clipboard promise chunk reader was provided.");
        return false;
    }

    static bool oleReady = false;
    if (!oleReady)
    {
        const HRESULT initResult = OleInitialize(nullptr);
        oleReady = SUCCEEDED(initResult) || initResult == RPC_E_CHANGED_MODE;
    }
    if (!oleReady)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "OleInitialize failed.");
        return false;
    }

    PromiseDataObject *dataObject = new (std::nothrow) PromiseDataObject(items, cacheRoot, readFileChunk);
    if (!dataObject)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "Cannot allocate clipboard file promise data object.");
        return false;
    }

    const HRESULT hr = OleSetClipboard(dataObject);
    dataObject->Release();
    if (FAILED(hr))
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "OleSetClipboard failed.");
        return false;
    }
    LOG_INFO("Installed native Windows clipboard file promise for {} file(s)", items.size());
    return true;
}

bool startNativePromiseDrag(QWidget *dragSource,
                            const QList<ClipboardFilePromiseItem> &items,
                            const QString &cacheRoot,
                            const ClipboardFilePromise::ReadFileChunk &readFileChunk,
                            QString *errorMessage)
{
    Q_UNUSED(dragSource)
    if (items.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "No file promises were provided.");
        return false;
    }
    if (cacheRoot.isEmpty() || !QDir().mkpath(cacheRoot))
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "Cannot create drag promise cache directory.");
        return false;
    }
    if (!readFileChunk)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "No drag promise chunk reader was provided.");
        return false;
    }

    static bool oleReady = false;
    if (!oleReady)
    {
        const HRESULT initResult = OleInitialize(nullptr);
        oleReady = SUCCEEDED(initResult) || initResult == RPC_E_CHANGED_MODE;
    }
    if (!oleReady)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "OleInitialize failed.");
        return false;
    }

    PromiseDataObject *dataObject = new (std::nothrow) PromiseDataObject(items, cacheRoot, readFileChunk);
    PromiseDropSource *dropSource = new (std::nothrow) PromiseDropSource();
    if (!dataObject || !dropSource)
    {
        if (dataObject)
            dataObject->Release();
        if (dropSource)
            dropSource->Release();
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "Cannot allocate drag file promise objects.");
        return false;
    }

    DWORD effect = DROPEFFECT_NONE;
    const HRESULT hr = DoDragDrop(dataObject, dropSource, DROPEFFECT_COPY, &effect);
    dataObject->Release();
    dropSource->Release();

    if (hr == DRAGDROP_S_CANCEL)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "Drag was cancelled.");
        return false;
    }
    if (FAILED(hr))
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "DoDragDrop failed.");
        return false;
    }
    return effect != DROPEFFECT_NONE;
}

bool onGuiThread()
{
    QCoreApplication *app = QCoreApplication::instance();
    return !app || QThread::currentThread() == app->thread();
}

class FilePromiseThreadInvoker : public QObject
{
    Q_OBJECT
public:
    explicit FilePromiseThreadInvoker(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void install(const QList<ClipboardFilePromiseItem> &items,
                 const QString &cacheRoot,
                 const ClipboardFilePromise::ReadFileChunk &readFileChunk,
                 bool *ok,
                 QString *errorMessage)
    {
        if (ok)
            *ok = installNativePromise(items, cacheRoot, readFileChunk, errorMessage);
    }

    void startDrag(QWidget *dragSource,
                   const QList<ClipboardFilePromiseItem> &items,
                   const QString &cacheRoot,
                   const ClipboardFilePromise::ReadFileChunk &readFileChunk,
                   bool *ok,
                   QString *errorMessage)
    {
        if (ok)
            *ok = startNativePromiseDrag(dragSource, items, cacheRoot, readFileChunk, errorMessage);
    }

    void destroySelf()
    {
        delete this;
    }
};

FilePromiseThreadInvoker *filePromiseThreadInvoker()
{
    static QPointer<FilePromiseThreadInvoker> invoker;
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    if (!invoker)
    {
        FilePromiseThreadInvoker *created = new FilePromiseThreadInvoker();
        if (QCoreApplication *app = QCoreApplication::instance())
        {
            created->moveToThread(app->thread());
            QObject::connect(app, SIGNAL(aboutToQuit()),
                             created, SLOT(destroySelf()),
                             Qt::DirectConnection);
        }
        invoker = created;
    }
    return invoker.data();
}
} // namespace

void ClipboardFilePromise::cancelCacheRoot(const QString &cacheRoot)
{
    cancelPromiseCacheRoot(cacheRoot);
}

bool ClipboardFilePromise::isSupported()
{
    return QGuiApplication::instance() != nullptr;
}

bool ClipboardFilePromise::install(const QList<ClipboardFilePromiseItem> &items,
                                   const QString &cacheRoot,
                                   const ReadFileChunk &readFileChunk,
                                   QString *errorMessage)
{
    if (!onGuiThread())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate(
                "ClipboardFilePromise", "Clipboard file promise installation must run on the GUI thread; use installAsync().");
        return false;
    }
    return installNativePromise(items, cacheRoot, readFileChunk, errorMessage);
}

void ClipboardFilePromise::installAsync(QObject *context,
                                        const QList<ClipboardFilePromiseItem> &items,
                                        const QString &cacheRoot,
                                        const ReadFileChunk &readFileChunk,
                                        const InstallCompletion &completion)
{
    const QPointer<QObject> guard(context);
    const bool hasContext = context != nullptr;
    QTimer::singleShot(0, filePromiseThreadInvoker(),
                       [guard, hasContext, items, cacheRoot, readFileChunk, completion]() {
                           QString errorMessage;
                           const bool ok = installNativePromise(items, cacheRoot, readFileChunk, &errorMessage);
                           if (!completion)
                               return;
                           if (guard)
                           {
                               QTimer::singleShot(0, guard.data(),
                                                  [guard, completion, ok, errorMessage]() {
                                                      if (guard)
                                                          completion(ok, errorMessage);
                                                  });
                           }
                           else if (!hasContext)
                           {
                               completion(ok, errorMessage);
                           }
                       });
}

bool ClipboardFilePromise::startDrag(QWidget *dragSource,
                                     const QList<ClipboardFilePromiseItem> &items,
                                     const QString &cacheRoot,
                                     const ReadFileChunk &readFileChunk,
                                     QString *errorMessage)
{
    if (!onGuiThread())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "File promise drag must be started on the GUI thread.");
        return false;
    }
    return startNativePromiseDrag(dragSource, items, cacheRoot, readFileChunk, errorMessage);
}

#include "clipboard_file_promise_win.moc"
