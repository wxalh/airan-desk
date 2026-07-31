#include "util/clipboard/clipboard_util.h"

#include "common/constant.h"
#include "common/logger_manager.h"
#include "util/clipboard/clipboard_image_codec.h"
#include "util/json/json_util.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QMetaObject>
#include <QMimeData>
#include <QPointer>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace
{
constexpr const char *kGnomeCopiedFilesMime = "x-special/gnome-copied-files";
constexpr const char *kKdeCutSelectionMime = "application/x-kde-cutselection";
constexpr const char *kKdeUriListMime = "application/x-kde4-urilist";

QByteArray normalizedClipboardLine(QByteArray line)
{
    line = line.trimmed();
    while (!line.isEmpty() && line.endsWith('\0'))
        line.chop(1);
    return line.trimmed();
}

QStringList urlsToLocalFiles(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &url : urls)
    {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (!path.isEmpty())
            paths.append(QDir::cleanPath(path));
    }
    paths.removeDuplicates();
    return paths;
}

QList<QUrl> localFileUrlsFromPaths(const QStringList &paths)
{
    QList<QUrl> urls;
    for (const QString &path : paths)
    {
        if (!path.isEmpty())
            urls.append(QUrl::fromLocalFile(QDir::cleanPath(path)));
    }
    return urls;
}

QByteArray uriListDataFromUrls(const QList<QUrl> &urls)
{
    QByteArray data;
    for (const QUrl &url : urls)
    {
        if (url.isEmpty())
            continue;
        data.append(url.toEncoded());
        data.append("\r\n");
    }
    return data;
}

QByteArray gnomeCopiedFilesDataFromUrls(const QList<QUrl> &urls)
{
    QByteArray data("copy\n");
    for (const QUrl &url : urls)
    {
        if (url.isEmpty())
            continue;
        data.append(url.toEncoded());
        data.append('\n');
    }
    if (data.endsWith('\n'))
        data.chop(1);
    return data;
}

QStringList localFilesFromUriListData(const QByteArray &data)
{
    QStringList paths;
    const QList<QByteArray> lines = data.split('\n');
    for (QByteArray line : lines)
    {
        line = normalizedClipboardLine(line);
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QUrl url = QUrl::fromEncoded(line);
        const QString path = url.isLocalFile() ? url.toLocalFile() : QString();
        if (!path.isEmpty())
            paths.append(QDir::cleanPath(path));
    }
    paths.removeDuplicates();
    return paths;
}

QStringList gnomeCopiedFilesToLocalFiles(const QByteArray &data)
{
    QStringList paths;
    const QList<QByteArray> lines = data.split('\n');
    for (int i = 0; i < lines.size(); ++i)
    {
        QByteArray line = normalizedClipboardLine(lines.at(i));
        if (line.isEmpty())
            continue;
        if (line == "copy" || line == "cut")
            continue;
        const QUrl url = QUrl::fromEncoded(line);
        const QString path = url.isLocalFile() ? url.toLocalFile() : QString();
        if (!path.isEmpty())
            paths.append(QDir::cleanPath(path));
    }
    paths.removeDuplicates();
    return paths;
}

QJsonArray fileArrayFromPaths(const QStringList &paths)
{
    QJsonArray files;
    for (const QString &path : paths)
    {
        QFileInfo info(path);
        QJsonObject file = JsonUtil::createObject()
                               .add(Constant::KEY_PATH, QDir::cleanPath(path))
                               .add(Constant::KEY_NAME, info.fileName())
                               .add(Constant::KEY_IS_DIR, info.isDir())
                               .add(Constant::KEY_FILE_SIZE, static_cast<double>(info.exists() ? info.size() : 0))
                               .build();
        files.append(file);
    }
    return files;
}

QJsonObject payloadFromMimeData(const QMimeData *mimeData)
{
    QJsonObject payload;
    if (mimeData && mimeData->hasText())
    {
        const QString text = mimeData->text();
        if (!text.isEmpty())
            payload.insert(Constant::KEY_TEXT, text);
    }

    QStringList files;
    if (mimeData && mimeData->hasFormat(QString::fromLatin1(kGnomeCopiedFilesMime)))
        files = gnomeCopiedFilesToLocalFiles(mimeData->data(QString::fromLatin1(kGnomeCopiedFilesMime)));
    if (files.isEmpty() && mimeData && mimeData->hasUrls())
        files = urlsToLocalFiles(mimeData->urls());
    if (files.isEmpty() && mimeData && mimeData->hasFormat(QStringLiteral("text/uri-list")))
        files = localFilesFromUriListData(mimeData->data(QStringLiteral("text/uri-list")));
    if (files.isEmpty() && mimeData && mimeData->hasFormat(QString::fromLatin1(kKdeUriListMime)))
        files = localFilesFromUriListData(mimeData->data(QString::fromLatin1(kKdeUriListMime)));
    if (!files.isEmpty())
        payload.insert(Constant::KEY_FILES, fileArrayFromPaths(files));

    const QByteArray pngData = ClipboardImageCodec::pngDataFromMimeData(mimeData);
    if (!pngData.isEmpty())
    {
        payload.insert(Constant::KEY_IMAGE, QString::fromLatin1(pngData.toBase64()));
        payload.insert(Constant::KEY_IMAGE_FORMAT, QStringLiteral("png"));
    }

    return payload;
}

bool payloadHasText(const QJsonObject &payload)
{
    return !JsonUtil::getString(payload, Constant::KEY_TEXT).isEmpty();
}

bool payloadHasFiles(const QJsonObject &payload)
{
    return payload.contains(Constant::KEY_FILES) &&
           !JsonUtil::getArray(payload, Constant::KEY_FILES).isEmpty();
}

bool payloadHasImage(const QJsonObject &payload)
{
    return !JsonUtil::getString(payload, Constant::KEY_IMAGE_FORMAT).isEmpty() &&
           !JsonUtil::getString(payload, Constant::KEY_IMAGE).isEmpty();
}

void mergeMissingPayloadFields(QJsonObject *target, const QJsonObject &fallback)
{
    if (!target)
        return;
    if (!payloadHasText(*target) && payloadHasText(fallback))
        target->insert(Constant::KEY_TEXT, JsonUtil::getString(fallback, Constant::KEY_TEXT));
    if (!payloadHasFiles(*target) && payloadHasFiles(fallback))
        target->insert(Constant::KEY_FILES, JsonUtil::getArray(fallback, Constant::KEY_FILES));
    if (!payloadHasImage(*target) && payloadHasImage(fallback))
    {
        target->insert(Constant::KEY_IMAGE, JsonUtil::getString(fallback, Constant::KEY_IMAGE));
        target->insert(Constant::KEY_IMAGE_FORMAT, JsonUtil::getString(fallback, Constant::KEY_IMAGE_FORMAT));
    }
}

bool populateMimeDataFromPayload(QMimeData *mimeData,
                                 const QJsonObject &payload,
                                 QString *errorMessage)
{
    if (!mimeData)
        return false;

    bool populated = false;
    const QString text = JsonUtil::getString(payload, Constant::KEY_TEXT);
    if (!text.isEmpty())
    {
        mimeData->setText(text);
        populated = true;
    }

    if (payloadHasImage(payload))
    {
        const QByteArray encodedImage = QByteArray::fromBase64(
            JsonUtil::getString(payload, Constant::KEY_IMAGE).toLatin1());
        const QImage image = ClipboardImageCodec::imageFromEncodedData(encodedImage);
        if (image.isNull())
        {
            if (errorMessage)
                *errorMessage = QCoreApplication::translate("ClipboardUtil", "The clipboard image is invalid.");
            return false;
        }
        mimeData->setImageData(image);
        populated = true;
    }

    const QList<QUrl> urls = localFileUrlsFromPaths(ClipboardUtil::payloadFilePaths(payload));
    if (!urls.isEmpty())
    {
        mimeData->setUrls(urls);
#if defined(Q_OS_LINUX)
        mimeData->setData(QString::fromLatin1(kGnomeCopiedFilesMime), gnomeCopiedFilesDataFromUrls(urls));
        mimeData->setData(QString::fromLatin1(kKdeCutSelectionMime), QByteArray("0"));
        mimeData->setData(QString::fromLatin1(kKdeUriListMime), uriListDataFromUrls(urls));
        mimeData->setData(QStringLiteral("text/uri-list"), uriListDataFromUrls(urls));
#endif
        populated = true;
    }

    if (!populated && errorMessage)
        *errorMessage = QCoreApplication::translate("ClipboardUtil", "The clipboard payload is empty.");
    return populated;
}

QClipboard *clipboard(QString *errorMessage)
{
    QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!app)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardUtil", "No GUI application is available.");
        return nullptr;
    }

    QClipboard *cb = QGuiApplication::clipboard();
    if (!cb && errorMessage)
        *errorMessage = QCoreApplication::translate("ClipboardUtil", "The system clipboard is unavailable.");
    return cb;
}

bool onGuiThread()
{
    QCoreApplication *app = QCoreApplication::instance();
    return !app || QThread::currentThread() == app->thread();
}

QJsonObject readPayloadOnGuiThread()
{
    QString error;
    QClipboard *cb = clipboard(&error);
    if (!cb)
    {
        LOG_WARN("Cannot read clipboard: {}", error);
        return QJsonObject();
    }

    QJsonObject payload = payloadFromMimeData(cb->mimeData(QClipboard::Clipboard));
#if defined(Q_OS_LINUX)
    if (cb->supportsSelection())
        mergeMissingPayloadFields(&payload, payloadFromMimeData(cb->mimeData(QClipboard::Selection)));
#endif

    return payload;
}

bool writeTextOnGuiThread(const QString &text, QString *errorMessage)
{
    QString error;
    QClipboard *cb = clipboard(&error);
    if (!cb)
    {
        if (errorMessage)
            *errorMessage = error;
        return false;
    }
    cb->setText(text, QClipboard::Clipboard);
#if defined(Q_OS_LINUX)
    if (cb->supportsSelection())
        cb->setText(text, QClipboard::Selection);
#endif
    return true;
}

bool writeFilesOnGuiThread(const QStringList &paths, QString *errorMessage)
{
    QString error;
    QClipboard *cb = clipboard(&error);
    if (!cb)
    {
        if (errorMessage)
            *errorMessage = error;
        return false;
    }

    const QList<QUrl> urls = localFileUrlsFromPaths(paths);

    if (urls.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardUtil", "No file paths were provided.");
        return false;
    }

    auto *mimeData = new QMimeData();
    mimeData->setUrls(urls);
#if defined(Q_OS_LINUX)
    mimeData->setData(QString::fromLatin1(kGnomeCopiedFilesMime), gnomeCopiedFilesDataFromUrls(urls));
    mimeData->setData(QString::fromLatin1(kKdeCutSelectionMime), QByteArray("0"));
    mimeData->setData(QString::fromLatin1(kKdeUriListMime), uriListDataFromUrls(urls));
    mimeData->setData(QStringLiteral("text/uri-list"), uriListDataFromUrls(urls));
#endif
    cb->setMimeData(mimeData, QClipboard::Clipboard);
#if defined(Q_OS_LINUX)
    if (cb->supportsSelection())
    {
        auto *selectionMimeData = new QMimeData();
        selectionMimeData->setUrls(urls);
        selectionMimeData->setData(QString::fromLatin1(kGnomeCopiedFilesMime), gnomeCopiedFilesDataFromUrls(urls));
        selectionMimeData->setData(QString::fromLatin1(kKdeCutSelectionMime), QByteArray("0"));
        selectionMimeData->setData(QString::fromLatin1(kKdeUriListMime), uriListDataFromUrls(urls));
        selectionMimeData->setData(QStringLiteral("text/uri-list"), uriListDataFromUrls(urls));
        cb->setMimeData(selectionMimeData, QClipboard::Selection);
    }
#endif
    return true;
}

bool writePayloadOnGuiThread(const QJsonObject &payload, QString *errorMessage)
{
    const QStringList files = ClipboardUtil::payloadFilePaths(payload);
    const bool hasImage = payloadHasImage(payload);
    const QString text = JsonUtil::getString(payload, Constant::KEY_TEXT);
    if (!files.isEmpty() && !hasImage && text.isEmpty())
        return writeFilesOnGuiThread(files, errorMessage);

    if (files.isEmpty() && !hasImage)
        return writeTextOnGuiThread(text, errorMessage);

    QString error;
    QClipboard *cb = clipboard(&error);
    if (!cb)
    {
        if (errorMessage)
            *errorMessage = error;
        return false;
    }

    auto *mimeData = new QMimeData();
    if (!populateMimeDataFromPayload(mimeData, payload, errorMessage))
    {
        delete mimeData;
        return false;
    }
    cb->setMimeData(mimeData, QClipboard::Clipboard);
#if defined(Q_OS_LINUX)
    if (cb->supportsSelection())
    {
        auto *selectionMimeData = new QMimeData();
        if (populateMimeDataFromPayload(selectionMimeData, payload, nullptr))
            cb->setMimeData(selectionMimeData, QClipboard::Selection);
        else
            delete selectionMimeData;
    }
#endif
    return true;
}

QString uuidWithoutBraces()
{
    return QUuid::createUuid().toString().remove('{').remove('}');
}

class ClipboardThreadInvoker : public QObject
{
    Q_OBJECT
public:
    explicit ClipboardThreadInvoker(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void read(QJsonObject *out)
    {
        if (out)
            *out = readPayloadOnGuiThread();
    }

    void writePayload(const QJsonObject &payload, bool *ok, QString *errorMessage)
    {
        if (ok)
            *ok = writePayloadOnGuiThread(payload, errorMessage);
    }

    void writeFiles(const QStringList &paths, bool *ok, QString *errorMessage)
    {
        if (ok)
            *ok = writeFilesOnGuiThread(paths, errorMessage);
    }

    void destroySelf()
    {
        delete this;
    }
};

ClipboardThreadInvoker *clipboardThreadInvoker()
{
    static QPointer<ClipboardThreadInvoker> invoker;
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    if (!invoker)
    {
        ClipboardThreadInvoker *created = new ClipboardThreadInvoker();
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

class ClipboardCacheCleanupWorker
{
public:
    ~ClipboardCacheCleanupWorker()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
            m_queue.clear();
        }
        m_condition.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    void enqueue(const QString &cacheRoot)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping)
            return;
        m_queue.push_back(cacheRoot);
        if (!m_thread.joinable())
            m_thread = std::thread(&ClipboardCacheCleanupWorker::run, this);
        m_condition.notify_all();
    }

private:
    void run()
    {
        for (;;)
        {
            QString cacheRoot;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(lock, [this]() { return m_stopping || !m_queue.empty(); });
                if (m_stopping)
                    return;
                cacheRoot = m_queue.front();
                m_queue.pop_front();
            }

            constexpr int kCleanupAttempts = 20;
            bool cleaned = false;
            for (int attempt = 0; attempt < kCleanupAttempts; ++attempt)
            {
                if (ClipboardUtil::cleanupCacheRoot(cacheRoot))
                {
                    cleaned = true;
                    break;
                }

                std::unique_lock<std::mutex> lock(m_mutex);
                if (m_condition.wait_for(lock,
                                         std::chrono::milliseconds(250),
                                         [this]() { return m_stopping; }))
                {
                    return;
                }
            }
            if (!cleaned)
                LOG_WARN("Clipboard cache cleanup exhausted retries: {}", cacheRoot);
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<QString> m_queue;
    std::thread m_thread;
    bool m_stopping = false;
};

ClipboardCacheCleanupWorker &clipboardCacheCleanupWorker()
{
    static ClipboardCacheCleanupWorker worker;
    return worker;
}
} // namespace

QJsonObject ClipboardUtil::readClipboardPayload()
{
    if (onGuiThread())
        return readPayloadOnGuiThread();
    LOG_WARN("Synchronous clipboard read rejected outside the GUI thread; use readClipboardPayloadAsync");
    return QJsonObject();
}

void ClipboardUtil::readClipboardPayloadAsync(QObject *context, const ReadCompletion &completion)
{
    if (!completion)
        return;
    const QPointer<QObject> guard(context);
    const bool hasContext = context != nullptr;
    QTimer::singleShot(0, clipboardThreadInvoker(), [guard, hasContext, completion]() {
        const QJsonObject payload = readPayloadOnGuiThread();
        if (guard)
        {
            QTimer::singleShot(0, guard.data(), [guard, completion, payload]() {
                if (guard)
                    completion(payload);
            });
        }
        else if (!hasContext)
        {
            completion(payload);
        }
    });
}

bool ClipboardUtil::writeClipboardPayload(const QJsonObject &payload, QString *errorMessage)
{
    if (onGuiThread())
        return writePayloadOnGuiThread(payload, errorMessage);
    if (errorMessage)
        *errorMessage = QCoreApplication::translate("ClipboardUtil", "Synchronous clipboard write is only allowed on the GUI thread.");
    return false;
}

void ClipboardUtil::writeClipboardPayloadAsync(QObject *context,
                                               const QJsonObject &payload,
                                               const WriteCompletion &completion)
{
    const QPointer<QObject> guard(context);
    const bool hasContext = context != nullptr;
    QTimer::singleShot(0, clipboardThreadInvoker(), [guard, hasContext, payload, completion]() {
        QString errorMessage;
        const bool ok = writePayloadOnGuiThread(payload, &errorMessage);
        if (!completion)
            return;
        if (guard)
        {
            QTimer::singleShot(0, guard.data(), [guard, completion, ok, errorMessage]() {
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

bool ClipboardUtil::writeFilePaths(const QStringList &paths, QString *errorMessage)
{
    if (onGuiThread())
        return writeFilesOnGuiThread(paths, errorMessage);
    if (errorMessage)
        *errorMessage = QCoreApplication::translate("ClipboardUtil", "Synchronous clipboard write is only allowed on the GUI thread.");
    return false;
}

void ClipboardUtil::writeFilePathsAsync(QObject *context,
                                        const QStringList &paths,
                                        const WriteCompletion &completion)
{
    const QPointer<QObject> guard(context);
    const bool hasContext = context != nullptr;
    QTimer::singleShot(0, clipboardThreadInvoker(), [guard, hasContext, paths, completion]() {
        QString errorMessage;
        const bool ok = writeFilesOnGuiThread(paths, &errorMessage);
        if (!completion)
            return;
        if (guard)
        {
            QTimer::singleShot(0, guard.data(), [guard, completion, ok, errorMessage]() {
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

bool ClipboardUtil::payloadHasFiles(const QJsonObject &payload)
{
    return !ClipboardUtil::payloadFilePaths(payload).isEmpty();
}

bool ClipboardUtil::payloadHasImage(const QJsonObject &payload)
{
    return ::payloadHasImage(payload);
}

QStringList ClipboardUtil::payloadFilePaths(const QJsonObject &payload)
{
    QStringList paths;
    const QJsonArray files = JsonUtil::getArray(payload, Constant::KEY_FILES);
    for (const QJsonValue &value : files)
    {
        const QJsonObject file = value.toObject();
        const QString path = JsonUtil::getString(file, Constant::KEY_PATH);
        if (!path.isEmpty())
            paths.append(QDir::cleanPath(path));
    }
    paths.removeDuplicates();
    return paths;
}

QJsonObject ClipboardUtil::payloadWithFilePaths(const QStringList &paths)
{
    return JsonUtil::createObject()
        .add(Constant::KEY_FILES, fileArrayFromPaths(paths))
        .build();
}

QString ClipboardUtil::cacheRoot(const QString &sessionId)
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (base.isEmpty())
        base = QDir::tempPath();

    QString root = QDir(base).filePath(QStringLiteral("airan-desk/clipboard"));
    const QString suffix = sessionId.isEmpty()
                               ? uuidWithoutBraces()
                               : sessionId;
    root = QDir(root).filePath(suffix);
    QDir().mkpath(root);
    return QDir::cleanPath(root);
}

bool ClipboardUtil::cleanupCacheRoot(const QString &path)
{
    if (path.isEmpty())
        return true;

    QDir dir(QDir::cleanPath(path));
    if (!dir.exists())
        return true;

    const bool ok = dir.removeRecursively();
    if (!ok)
        LOG_WARN("Failed to remove clipboard cache root: {}", path);
    return ok;
}

void ClipboardUtil::cleanupCacheRootAsync(const QString &path)
{
    const QString cacheRoot = QDir::cleanPath(path);
    if (cacheRoot.isEmpty())
        return;

    clipboardCacheCleanupWorker().enqueue(cacheRoot);
}

#include "clipboard_util.moc"
