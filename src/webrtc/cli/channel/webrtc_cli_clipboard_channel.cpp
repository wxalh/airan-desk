#include "webrtc/cli/webrtc_cli.h"
#include "security/audit_session.h"

#include "common/qt_rtc_metatypes.h"
#include "util/clipboard/native/clipboard_file_promise.h"
#include "util/clipboard/clipboard_util.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

#include <QByteArray>
#include <QDateTime>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <mutex>

namespace
{
constexpr int kClipboardInlineTextLimitBytes = 128 * 1024;
constexpr int kClipboardTextChunkBytes = 48 * 1024;
constexpr qint64 kMaxClipboardTextBytes = 64LL * 1024 * 1024;
constexpr int kMaxClipboardTextRequests = 8;
constexpr int kClipboardPayloadChunkBytes = 48 * 1024;
constexpr qint64 kMaxClipboardPayloadBytes = 64LL * 1024 * 1024;
constexpr int kMaxClipboardPayloadRequests = 8;
constexpr size_t kMaxClipboardChannelMessageBytes = 16 * 1024 * 1024;
constexpr qint64 kMaxClipboardFileChunkBytes = 64 * 1024;
constexpr int kMaxClipboardTextChunkEncodedBytes = ((kClipboardTextChunkBytes + 2) / 3) * 4;
constexpr int kMaxClipboardPayloadChunkEncodedBytes = ((kClipboardPayloadChunkBytes + 2) / 3) * 4;
constexpr int kMaxClipboardFileChunkEncodedBytes = ((kMaxClipboardFileChunkBytes + 2) / 3) * 4;
constexpr quint64 kClipboardChunkSendHighWatermark = 1536 * 1024;
constexpr qint64 kClipboardChunkSendMaxQueuedBytes = 64LL * 1024 * 1024;
constexpr int kClipboardChunkSendBatchMessages = 32;
constexpr int kClipboardChunkSendPollMs = 5;
constexpr int kClipboardChunkSendTimeoutMs = 45000;
constexpr qint64 kMaxClipboardInboundPayloadBytes = 64LL * 1024 * 1024;
constexpr int kClipboardPayloadReceiveTimeoutMs = 45000;

std::mutex g_remoteAppliedClipboardSignatureMutex;
QByteArray g_lastRemoteAppliedClipboardPayloadSignature;
std::mutex g_clipboardInputOwnerMutex;
QString g_clipboardInputOwnerId;

QByteArray lastRemoteAppliedClipboardPayloadSignature()
{
    std::lock_guard<std::mutex> lock(g_remoteAppliedClipboardSignatureMutex);
    return g_lastRemoteAppliedClipboardPayloadSignature;
}

void setLastRemoteAppliedClipboardPayloadSignature(const QByteArray &signature)
{
    std::lock_guard<std::mutex> lock(g_remoteAppliedClipboardSignatureMutex);
    g_lastRemoteAppliedClipboardPayloadSignature = signature;
}

QString clipboardInputOwnerId()
{
    std::lock_guard<std::mutex> lock(g_clipboardInputOwnerMutex);
    return g_clipboardInputOwnerId;
}

void setClipboardInputOwnerId(const QString &ownerId)
{
    std::lock_guard<std::mutex> lock(g_clipboardInputOwnerMutex);
    g_clipboardInputOwnerId = ownerId;
}

QString uuidWithoutBraces()
{
    return QUuid::createUuid().toString().remove('{').remove('}');
}

QString promiseRelativePath(const QString &path)
{
    QString relative = QDir::fromNativeSeparators(path);
    while (relative.startsWith(QLatin1Char('/')))
        relative.remove(0, 1);
    return relative;
}

void appendPromiseFile(QJsonArray *files, const QString &path, const QString &relativePath, const QFileInfo &info)
{
    if (!files || relativePath.isEmpty())
        return;

    QJsonObject file = JsonUtil::createObject()
                           .add(Constant::KEY_PATH, QDir::cleanPath(path))
                           .add(Constant::KEY_NAME, promiseRelativePath(relativePath))
                           .add(Constant::KEY_RELATIVE_PATH, promiseRelativePath(relativePath))
                           .add(Constant::KEY_IS_DIR, info.isDir())
                           .add(Constant::KEY_FILE_SIZE, static_cast<double>(info.exists() && info.isFile() ? info.size() : 0))
                           .build();
    files->append(file);
}

QJsonObject payloadWithExpandedPromiseFiles(const QJsonObject &payload)
{
    QJsonArray promiseFiles;
    const QJsonArray files = JsonUtil::getArray(payload, Constant::KEY_FILES);
    for (const QJsonValue &value : files)
    {
        const QJsonObject file = value.toObject();
        const QString path = JsonUtil::getString(file, Constant::KEY_PATH);
        if (path.isEmpty())
            continue;

        QFileInfo info(path);
        if (!info.exists())
            continue;

        const QString rootName = JsonUtil::getString(file, Constant::KEY_NAME, info.fileName());
        if (!info.isDir())
        {
            appendPromiseFile(&promiseFiles, path, rootName, info);
            continue;
        }

        appendPromiseFile(&promiseFiles, path, rootName, info);
        QDir rootDir(path);
        QDirIterator it(path,
                        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString childPath = it.next();
            const QFileInfo childInfo = it.fileInfo();
            const QString relative = rootName + QLatin1Char('/') + rootDir.relativeFilePath(childPath);
            appendPromiseFile(&promiseFiles, childPath, relative, childInfo);
        }
    }

    QJsonObject expanded = payload;
    if (!promiseFiles.isEmpty())
        expanded.insert(Constant::KEY_PROMISE_FILES, promiseFiles);
    return expanded;
}

QList<ClipboardFilePromiseItem> filePromiseItemsFromPayload(const QJsonObject &payload, bool *hasDirectories)
{
    if (hasDirectories)
        *hasDirectories = false;

    QList<ClipboardFilePromiseItem> items;
    QJsonArray files = JsonUtil::getArray(payload, Constant::KEY_PROMISE_FILES);
    if (files.isEmpty())
        files = JsonUtil::getArray(payload, Constant::KEY_FILES);
    for (const QJsonValue &value : files)
    {
        const QJsonObject file = value.toObject();
        const QString path = JsonUtil::getString(file, Constant::KEY_PATH);
        if (path.isEmpty())
            continue;

        const bool isDir = JsonUtil::getBool(file, Constant::KEY_IS_DIR, false);
        if (isDir)
        {
            if (hasDirectories)
                *hasDirectories = true;
        }

        ClipboardFilePromiseItem item;
        item.sourcePath = QDir::cleanPath(path);
        item.displayName = JsonUtil::getString(file,
                                               Constant::KEY_RELATIVE_PATH,
                                               JsonUtil::getString(file, Constant::KEY_NAME, QFileInfo(path).fileName()));
        item.size = JsonUtil::getInt64(file, Constant::KEY_FILE_SIZE, 0);
        item.isDirectory = isDir;
        items.append(item);
    }
    return items;
}

QByteArray readLocalFileChunk(const QString &path, qint64 offset, qint64 maxBytes, bool *ok, QString *errorMessage)
{
    if (ok)
        *ok = false;
    if (path.isEmpty() || offset < 0 || maxBytes <= 0 || maxBytes > kMaxClipboardFileChunkBytes)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCli", "Invalid clipboard stream read range.");
        return QByteArray();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCli", "Cannot open source file.");
        return QByteArray();
    }
    if (!file.seek(offset))
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCli", "Cannot seek source file.");
        return QByteArray();
    }

    const QByteArray data = file.read(maxBytes);
    if (ok)
        *ok = true;
    return data;
}
} // namespace

void WebRtcCli::onClipboardChannelOpen()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onClipboardChannelOpen();
        });
        return;
    }
    LOG_INFO("Clipboard channel opened");
    clearClipboardPayloadTransferState();
    m_remoteSupportsClipboardPayloadV2 = false;
    sendClipboardChannelMessage(JsonUtil::createObject()
                                    .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_CAPABILITIES)
                                    .add(Constant::KEY_CLIPBOARD_PAYLOAD_V2, true)
                                    .build());
    connectClipboardMonitor();
}

void WebRtcCli::onClipboardChannelMessage(rtc::message_variant data)
{
    if (m_shutdownStarted.load())
        return;
    noteSessionInboundActivity();
    if (!std::holds_alternative<std::string>(data))
    {
        LOG_WARN("Clipboard channel received binary data, ignoring");
        return;
    }
    if (std::get<std::string>(data).size() > kMaxClipboardChannelMessageBytes)
    {
        LOG_WARN("Rejected oversized clipboard channel message: size={} bytes", std::get<std::string>(data).size());
        return;
    }
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard, data = std::move(data)]() mutable {
            if (guard)
                guard->onClipboardChannelMessage(std::move(data));
        });
        return;
    }
    const std::string &message = std::get<std::string>(data);
    const QJsonObject object = JsonUtil::safeParseObject(
        QByteArray::fromRawData(message.data(), static_cast<int>(message.size())));
    if (!JsonUtil::isValidObject(object))
    {
        LOG_WARN("Clipboard channel received invalid JSON");
        return;
    }
    handleClipboardMessage(object);
}

void WebRtcCli::onClipboardChannelError(std::string error)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard, error = std::move(error)]() mutable {
            if (guard)
                guard->onClipboardChannelError(std::move(error));
        });
        return;
    }
    LOG_ERROR("Clipboard channel error: {}", error);
    m_remoteSupportsClipboardPayloadV2 = false;
    failClipboardChunkSendQueue();
    clearClipboardPayloadTransferState();
}

void WebRtcCli::onClipboardChannelClosed()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onClipboardChannelClosed();
        });
        return;
    }
    LOG_INFO("Clipboard channel closed");
    m_remoteSupportsClipboardPayloadV2 = false;
    failClipboardChunkSendQueue();
    clearClipboardPayloadTransferState();
    if (m_clipboardSnapshotTimer)
        m_clipboardSnapshotTimer->stop();
}

void WebRtcCli::connectClipboardMonitor()
{
    if (m_clipboardMonitorConnected)
        return;
    QObject *guiContext = QCoreApplication::instance();
    if (!guiContext)
        return;
    m_clipboardMonitorConnected = true;

    const QPointer<WebRtcCli> guard(this);
    QTimer::singleShot(0, guiContext, [guard]() {
        if (!guard)
            return;
        QClipboard *clipboard = QGuiApplication::clipboard();
        if (!clipboard)
        {
            QTimer::singleShot(0, guard.data(), [guard]() {
                if (guard)
                    guard->m_clipboardMonitorConnected = false;
            });
            return;
        }

        QObject::connect(clipboard, &QClipboard::dataChanged, guard.data(), [guard]() {
            if (guard)
                guard->scheduleLocalClipboardSnapshot(QStringLiteral("clipboard"));
        }, Qt::QueuedConnection);
#if defined(Q_OS_LINUX)
        if (clipboard->supportsSelection())
        {
            QObject::connect(clipboard, &QClipboard::selectionChanged, guard.data(), [guard]() {
                if (guard)
                    guard->scheduleLocalClipboardSnapshot(QStringLiteral("selection"));
            }, Qt::QueuedConnection);
        }
#endif
    });
}

void WebRtcCli::scheduleLocalClipboardSnapshot(const QString &reason)
{
    if (m_destroying || !m_connected || !m_clipboardChannel || !m_clipboardChannel->isOpen())
        return;
    const QString ownerId = m_sessionId.isEmpty() ? m_subscriberId : m_sessionId;
    if (clipboardInputOwnerId() != ownerId)
    {
        LOG_TRACE("Skipped local clipboard snapshot for non-owner session: {}", reason);
        return;
    }
    if (QDateTime::currentMSecsSinceEpoch() > m_clipboardAutoSyncAllowedUntilMs)
    {
        LOG_TRACE("Skipped local clipboard snapshot without recent input: {}", reason);
        return;
    }

    if (m_clipboardSnapshotTimer)
    {
        m_clipboardSnapshotTimer->start(250);
        LOG_TRACE("Scheduled local clipboard snapshot: {}", reason);
    }
}

void WebRtcCli::sendLocalClipboardSnapshot(const QString &requestId)
{
    if (m_destroying || !m_connected || !m_clipboardChannel || !m_clipboardChannel->isOpen())
        return;

    const QPointer<WebRtcCli> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    ClipboardUtil::readClipboardPayloadAsync(
        this,
        [guard, callbackLifetime, requestId](const QJsonObject &payload) {
            auto permit = callbackLifetime->tryEnter();
            if (!permit)
                return;
            if (!guard || guard->m_destroying)
                return;
            guard->sendLocalClipboardSnapshotPayload(requestId, payload);
        });
}


bool WebRtcCli::sendClipboardPayloadInChunks(const QString &requestId,
                                             const QJsonObject &payload,
                                             bool pasteAfterApply,
                                             const std::function<void(bool)> &completion)
{
    if (!m_remoteSupportsClipboardPayloadV2)
        return false;

    const QByteArray payloadBytes = JsonUtil::toCompactBytes(payload);
    if (payloadBytes.size() <= kClipboardInlineTextLimitBytes)
        return false;
    if (payloadBytes.size() > kMaxClipboardPayloadBytes)
    {
        LOG_WARN("Skipped clipboard payload larger than protocol limit: bytes={}", payloadBytes.size());
        if (completion)
            completion(false);
        return true;
    }
    if (!enqueueClipboardChunkTransfer(true, requestId, payloadBytes, pasteAfterApply, completion) && completion)
        completion(false);
    return true;
}


bool WebRtcCli::enqueueClipboardChunkTransfer(bool payloadTransfer,
                                              const QString &requestId,
                                              const QByteArray &bytes,
                                              bool pasteAfterApply,
                                              const std::function<void(bool)> &completion)
{
    if (requestId.isEmpty() || bytes.isEmpty() ||
        bytes.size() > kClipboardChunkSendMaxQueuedBytes - m_clipboardChunkSendQueuedBytes)
    {
        LOG_WARN("Rejected clipboard chunk transfer queue request: bytes={}, queued={}",
                 bytes.size(), m_clipboardChunkSendQueuedBytes);
        return false;
    }

    ClipboardChunkSendState state;
    state.payloadTransfer = payloadTransfer;
    state.requestId = requestId;
    state.bytes = bytes;
    state.pasteAfterApply = pasteAfterApply;
    const int chunkBytes = payloadTransfer ? kClipboardPayloadChunkBytes : kClipboardTextChunkBytes;
    state.chunkCount = qMax(1, (bytes.size() + chunkBytes - 1) / chunkBytes);
    state.deadlineMs = 0;
    state.completion = completion;
    m_clipboardChunkSendQueuedBytes += bytes.size();
    m_clipboardChunkSendQueue.enqueue(std::move(state));
    scheduleClipboardChunkSend();
    return true;
}


QJsonObject WebRtcCli::currentClipboardChunkMessage(const ClipboardChunkSendState &state) const
{
    const int chunkBytes = state.payloadTransfer ? kClipboardPayloadChunkBytes : kClipboardTextChunkBytes;
    if (state.nextStep == 0)
    {
        JsonObjectBuilder builder = JsonUtil::createObject()
                                        .add(Constant::KEY_MSGTYPE,
                                             state.payloadTransfer
                                                 ? Constant::TYPE_CLIPBOARD_PAYLOAD_BEGIN
                                                 : Constant::TYPE_CLIPBOARD_TEXT_BEGIN)
                                        .add(Constant::KEY_REQUEST_ID, state.requestId)
                                        .add(Constant::KEY_CHUNK_COUNT, state.chunkCount)
                                        .add(Constant::KEY_PASTE_AFTER_APPLY, state.pasteAfterApply);
        if (state.payloadTransfer)
            builder.add(Constant::KEY_PAYLOAD_SIZE, static_cast<qint64>(state.bytes.size()));
        else
            builder.add(Constant::KEY_TEXT_SIZE, static_cast<qint64>(state.bytes.size()));
        return builder.build();
    }

    if (state.nextStep <= state.chunkCount)
    {
        const int chunkIndex = state.nextStep - 1;
        const QByteArray chunk = state.bytes.mid(chunkIndex * chunkBytes, chunkBytes);
        return JsonUtil::createObject()
            .add(Constant::KEY_MSGTYPE,
                 state.payloadTransfer
                     ? Constant::TYPE_CLIPBOARD_PAYLOAD_CHUNK
                     : Constant::TYPE_CLIPBOARD_TEXT_CHUNK)
            .add(Constant::KEY_REQUEST_ID, state.requestId)
            .add(Constant::KEY_CHUNK_INDEX, chunkIndex)
            .add(Constant::KEY_CHUNK_COUNT, state.chunkCount)
            .add(Constant::KEY_CHUNK_DATA, QString::fromLatin1(chunk.toBase64()))
            .build();
    }

    return JsonUtil::createObject()
        .add(Constant::KEY_MSGTYPE,
             state.payloadTransfer
                 ? Constant::TYPE_CLIPBOARD_PAYLOAD_END
                 : Constant::TYPE_CLIPBOARD_TEXT_END)
        .add(Constant::KEY_REQUEST_ID, state.requestId)
        .add(Constant::KEY_CHUNK_COUNT, state.chunkCount)
        .build();
}


void WebRtcCli::scheduleClipboardChunkSend(int delayMs)
{
    if (m_clipboardChunkSendScheduled || m_clipboardChunkSendQueue.isEmpty())
        return;
    m_clipboardChunkSendScheduled = true;
    const QPointer<WebRtcCli> guard(this);
    QTimer::singleShot(qMax(0, delayMs), this, [guard]() {
        if (!guard)
            return;
        guard->m_clipboardChunkSendScheduled = false;
        guard->drainClipboardChunkSendQueue();
    });
}


void WebRtcCli::drainClipboardChunkSendQueue()
{
    if (!m_clipboardChannel || !m_clipboardChannel->isOpen())
    {
        failClipboardChunkSendQueue();
        return;
    }

    while (!m_clipboardChunkSendQueue.isEmpty())
    {
        ClipboardChunkSendState &state = m_clipboardChunkSendQueue.head();
        if (state.deadlineMs == 0)
            state.deadlineMs = QDateTime::currentMSecsSinceEpoch() + kClipboardChunkSendTimeoutMs;
        if (QDateTime::currentMSecsSinceEpoch() <= state.deadlineMs)
            break;
        const auto completion = state.completion;
        m_clipboardChunkSendQueuedBytes -= state.bytes.size();
        m_clipboardChunkSendQueue.dequeue();
        LOG_WARN("Clipboard chunk transfer timed out before send completion");
        if (completion)
            completion(false);
    }

    int sentMessages = 0;
    while (!m_clipboardChunkSendQueue.isEmpty() &&
           sentMessages < kClipboardChunkSendBatchMessages &&
           m_clipboardChannel->bufferedAmount() < kClipboardChunkSendHighWatermark)
    {
        ClipboardChunkSendState &state = m_clipboardChunkSendQueue.head();
        if (!sendClipboardChannelMessage(currentClipboardChunkMessage(state)))
        {
            if (!m_clipboardChannel || !m_clipboardChannel->isOpen())
                failClipboardChunkSendQueue();
            else
                scheduleClipboardChunkSend(kClipboardChunkSendPollMs);
            return;
        }

        ++state.nextStep;
        state.deadlineMs = QDateTime::currentMSecsSinceEpoch() + kClipboardChunkSendTimeoutMs;
        ++sentMessages;
        if (state.nextStep > state.chunkCount + 1)
        {
            const auto completion = state.completion;
            m_clipboardChunkSendQueuedBytes -= state.bytes.size();
            m_clipboardChunkSendQueue.dequeue();
            if (completion)
                completion(true);
        }
    }

    if (!m_clipboardChunkSendQueue.isEmpty())
        scheduleClipboardChunkSend(kClipboardChunkSendPollMs);
}


void WebRtcCli::failClipboardChunkSendQueue()
{
    m_clipboardChunkSendScheduled = false;
    while (!m_clipboardChunkSendQueue.isEmpty())
    {
        const auto completion = m_clipboardChunkSendQueue.head().completion;
        m_clipboardChunkSendQueue.dequeue();
        if (completion)
            completion(false);
    }
    m_clipboardChunkSendQueuedBytes = 0;
}


void WebRtcCli::sendLocalClipboardSnapshotPayload(const QString &requestId, const QJsonObject &rawPayload)
{
    const bool explicitRequest = !requestId.isEmpty();
    QJsonObject payload = explicitRequest
                              ? payloadWithExpandedPromiseFiles(rawPayload)
                              : rawPayload;
    if (!m_remoteSupportsClipboardPayloadV2 && ClipboardUtil::payloadHasImage(payload))
    {
        payload.remove(Constant::KEY_IMAGE);
        payload.remove(Constant::KEY_IMAGE_FORMAT);
        LOG_INFO("Skipped clipboard image for a peer without clipboard payload v2 support");
    }
    if (JsonUtil::getString(payload, Constant::KEY_TEXT).isEmpty() &&
        !ClipboardUtil::payloadHasFiles(payload) &&
        !ClipboardUtil::payloadHasImage(payload))
    {
        return;
    }
    const QByteArray signature = JsonUtil::toCompactBytes(payload);
    if (signature.isEmpty() || (!explicitRequest && signature == m_lastSentClipboardPayloadSignature))
        return;
    if (!explicitRequest && signature == m_lastRemoteAppliedClipboardPayloadSignature)
    {
        m_lastSentClipboardPayloadSignature = signature;
        return;
    }
    if (!explicitRequest && signature == lastRemoteAppliedClipboardPayloadSignature())
    {
        m_lastSentClipboardPayloadSignature = signature;
        return;
    }

    const QString effectiveRequestId = requestId.isEmpty() ? uuidWithoutBraces() : requestId;
    const QPointer<WebRtcCli> guard(this);
    const auto markSent = [guard, signature](bool ok) {
        if (!guard)
            return;
        if (!ok)
        {
            LOG_WARN("Failed to send clipboard snapshot");
            return;
        }
        guard->m_lastSentClipboardPayloadSignature = signature;
        LOG_TRACE("Sent local clipboard snapshot: bytes={}", signature.size());
    };
    if (ClipboardUtil::payloadHasImage(payload) &&
        sendClipboardPayloadInChunks(effectiveRequestId, payload, false, markSent))
    {
        return;
    }
    const QString text = JsonUtil::getString(payload, Constant::KEY_TEXT);
    QJsonObject payloadWithoutText = payload;
    payloadWithoutText.remove(Constant::KEY_TEXT);
    const QByteArray textUtf8 = text.toUtf8();
    if (!text.isEmpty() && payloadWithoutText.isEmpty() && textUtf8.size() > kClipboardInlineTextLimitBytes)
    {
        if (textUtf8.size() > kMaxClipboardTextBytes)
        {
            LOG_WARN("Skipped clipboard text larger than protocol limit: bytes={}", textUtf8.size());
            return;
        }
        if (!enqueueClipboardChunkTransfer(false, effectiveRequestId, textUtf8, false, markSent))
            markSent(false);
        return;
    }
    else
    {
        QJsonObject response = JsonUtil::createObject()
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_SNAPSHOT)
                                   .add(Constant::KEY_REQUEST_ID, effectiveRequestId)
                                   .add(Constant::KEY_PAYLOAD, payload)
                                   .build();
        markSent(sendClipboardChannelMessage(response));
        return;
    }
}


bool WebRtcCli::sendClipboardChannelMessage(const QJsonObject &message)
{
    if (!m_clipboardChannel || !m_clipboardChannel->isOpen())
    {
        LOG_WARN("Clipboard channel is not ready");
        return false;
    }

    try
    {
        const QByteArray messageBytes = JsonUtil::toCompactBytes(message);
        if (messageBytes.size() > static_cast<qint64>(kMaxClipboardChannelMessageBytes))
        {
            LOG_WARN("Rejected oversized outbound clipboard message: size={}", messageBytes.size());
            return false;
        }
        if (!m_clipboardChannel->send(rtc::message_variant(messageBytes.toStdString())))
        {
            LOG_TRACE("Clipboard channel send buffer rejected a message");
            return false;
        }
        noteSessionOutboundActivity();
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to send clipboard channel message: {}", e.what());
        return false;
    }
    catch (...)
    {
        LOG_ERROR("Failed to send clipboard channel message: unknown error");
        return false;
    }
}

void WebRtcCli::handleClipboardMessage(const QJsonObject &object)
{
    const QString msgType = JsonUtil::getString(object, Constant::KEY_MSGTYPE);
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);

    if (msgType == Constant::TYPE_CLIPBOARD_CAPABILITIES)
    {
        m_remoteSupportsClipboardPayloadV2 = JsonUtil::getBool(
            object, Constant::KEY_CLIPBOARD_PAYLOAD_V2, false);
        return;
    }

    if (msgType == Constant::TYPE_CLIPBOARD_PREPARE_UPLOAD)
    {
        const QString cacheRoot = ClipboardUtil::cacheRoot(requestId);
        {
            QMutexLocker locker(&m_transferMutex);
            m_clipboardCacheRoots.insert(cacheRoot);
        }
        QJsonObject response = JsonUtil::createObject()
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_PREPARE_UPLOAD_RESULT)
                                   .add(Constant::KEY_REQUEST_ID, requestId)
                                   .add(Constant::KEY_STATUS, !cacheRoot.isEmpty())
                                   .add(Constant::KEY_CACHE_ROOT, cacheRoot)
                                   .add(Constant::KEY_NATIVE_FILE_PROMISE, ClipboardFilePromise::isSupported())
                                   .build();
        sendClipboardChannelMessage(response);
        return;
    }

    if (msgType == Constant::TYPE_CLIPBOARD_SET)
    {
        const QJsonObject payload = JsonUtil::getObject(object, Constant::KEY_PAYLOAD);
        const bool pasteAfterApply = JsonUtil::getBool(object, Constant::KEY_PASTE_AFTER_APPLY, false);
        applyClipboardPayload(requestId, payload, pasteAfterApply);
        return;
    }

    if (msgType == Constant::TYPE_CLIPBOARD_PROMISE_FILE_RESULT)
    {
        const QString path = JsonUtil::getString(object, Constant::KEY_PATH_CLI);
        const bool status = JsonUtil::getBool(object, Constant::KEY_STATUS, false);
        noteClipboardPromisedFileResult(path, status);
        return;
    }

    if (msgType == Constant::TYPE_CLIPBOARD_STREAM_READ_REQUEST)
    {
        handleClipboardStreamReadRequest(object);
        return;
    }

    if (msgType == Constant::TYPE_CLIPBOARD_STREAM_READ_RESULT)
    {
        handleClipboardStreamReadResult(object);
        return;
    }

    if (msgType == Constant::TYPE_CLIPBOARD_TEXT_BEGIN)
    {
        handleClipboardTextBegin(object);
        return;
    }

    if (msgType == Constant::TYPE_CLIPBOARD_TEXT_CHUNK)
    {
        handleClipboardTextChunk(object);
        return;
    }

    if (msgType == Constant::TYPE_CLIPBOARD_TEXT_END)
    {
        handleClipboardTextEnd(object);
        return;
    }

    if (msgType == Constant::TYPE_CLIPBOARD_PAYLOAD_BEGIN)
    {
        handleClipboardPayloadBegin(object);
        return;
    }

    if (msgType == Constant::TYPE_CLIPBOARD_PAYLOAD_CHUNK)
    {
        handleClipboardPayloadChunk(object);
        return;
    }

    if (msgType == Constant::TYPE_CLIPBOARD_PAYLOAD_END)
    {
        handleClipboardPayloadEnd(object);
        return;
    }

    if (msgType == Constant::TYPE_CLIPBOARD_REQUEST)
    {
        sendLocalClipboardSnapshot(requestId);
        return;
    }

    LOG_WARN("Unknown clipboard message type: {}", msgType);
}

void WebRtcCli::applyClipboardPayload(const QString &requestId,
                                      const QJsonObject &payload,
                                      bool pasteAfterApply)
{
    const QList<ClipboardFilePromiseItem> promiseItems = filePromiseItemsFromPayload(payload, nullptr);
    const QPointer<WebRtcCli> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    const auto finishApply = [guard, callbackLifetime, requestId, payload, pasteAfterApply](bool ok, const QString &errorMessage) {
        auto permit = callbackLifetime->tryEnter();
        if (!permit || !guard || guard->m_destroying)
            return;
        if (ok)
        {
            guard->m_lastRemoteAppliedClipboardPayloadSignature = JsonUtil::toCompactBytes(payload);
            setLastRemoteAppliedClipboardPayloadSignature(guard->m_lastRemoteAppliedClipboardPayloadSignature);
        }
        guard->sendClipboardChannelMessage(JsonUtil::createObject()
                                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_APPLY_RESULT)
                                               .add(Constant::KEY_REQUEST_ID, requestId)
                                               .add(Constant::KEY_STATUS, ok)
                                               .add(Constant::KEY_ERROR, errorMessage)
                                               .add(Constant::KEY_PASTE_AFTER_APPLY, pasteAfterApply)
                                               .build());
    };

    const bool hasAdditionalFormats = ClipboardUtil::payloadHasImage(payload) ||
                                      !JsonUtil::getString(payload, Constant::KEY_TEXT).isEmpty();
    if (!promiseItems.isEmpty() && ClipboardFilePromise::isSupported() && !hasAdditionalFormats)
    {
        const QString cacheRoot = ClipboardUtil::cacheRoot(requestId);
        {
            QMutexLocker locker(&m_transferMutex);
            m_clipboardCacheRoots.insert(cacheRoot);
        }
        ClipboardFilePromise::installAsync(
            this,
            promiseItems,
            cacheRoot,
            [guard, callbackLifetime, requestId](const QString &sourcePath, qint64 offset, qint64 maxBytes, bool *ok, QString *error) {
                auto permit = callbackLifetime->tryEnter();
                if (!permit || !guard || guard->m_shutdownStarted.load())
                {
                    if (ok)
                        *ok = false;
                    if (error)
                        *error = QCoreApplication::translate("WebRtcCli", "Remote session is closed.");
                    return QByteArray();
                }
                return guard->readControllerClipboardFileChunk(requestId, sourcePath, offset, maxBytes, ok, error);
            },
            [guard, callbackLifetime, payload, finishApply](bool installed, const QString &installError) {
                auto permit = callbackLifetime->tryEnter();
                if (!permit || !guard || guard->m_destroying)
                    return;
                if (installed)
                {
                    finishApply(true, QString());
                    return;
                }
                ClipboardUtil::writeClipboardPayloadAsync(
                    guard.data(),
                    payload,
                    [finishApply, installError](bool fallbackOk, const QString &fallbackError) {
                        finishApply(fallbackOk, fallbackError.isEmpty() ? installError : fallbackError);
                    });
            });
        return;
    }

    ClipboardUtil::writeClipboardPayloadAsync(this, payload, finishApply);
}


void WebRtcCli::noteClipboardInputActivity()
{
    setClipboardInputOwnerId(m_sessionId.isEmpty() ? m_subscriberId : m_sessionId);
    m_clipboardAutoSyncAllowedUntilMs = QDateTime::currentMSecsSinceEpoch() + 10000;
}

bool WebRtcCli::requestClipboardPromisedFile(const QString &requestId,
                                             const QString &sourcePath,
                                             const QString &localPath,
                                             QString *errorMessage)
{
    const QString cleanedLocalPath = QDir::cleanPath(localPath);
    if (sourcePath.isEmpty() || cleanedLocalPath.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCli", "Clipboard promise file path is empty.");
        return false;
    }

    if (QFileInfo(cleanedLocalPath).exists())
        return true;

    const QString fileRequestId = QStringLiteral("%1-%2").arg(requestId, uuidWithoutBraces());
    {
        QMutexLocker locker(&m_transferMutex);
        m_pendingClipboardPromiseTargets.insert(cleanedLocalPath);
        m_clipboardPromiseFileResults.remove(cleanedLocalPath);
    }

    const QJsonObject request = JsonUtil::createObject()
                                    .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_PROMISE_FILE_REQUEST)
                                    .add(Constant::KEY_REQUEST_ID, fileRequestId)
                                    .add(Constant::KEY_PATH_CTL, sourcePath)
                                    .add(Constant::KEY_PATH_CLI, cleanedLocalPath)
                                    .build();
    bool queued = true;
    if (QThread::currentThread() == thread())
    {
        sendClipboardChannelMessage(request);
    }
    else
    {
        queued = QMetaObject::invokeMethod(this,
                                           "sendClipboardChannelMessage",
                                           Qt::QueuedConnection,
                                           Q_ARG(QJsonObject, request));
    }
    if (!queued)
    {
        QMutexLocker locker(&m_transferMutex);
        m_pendingClipboardPromiseTargets.remove(cleanedLocalPath);
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCli", "Failed to queue clipboard promise file request.");
        return false;
    }

    bool status = false;
    bool hasResult = false;
    {
        QMutexLocker locker(&m_transferMutex);
        const qint64 deadlineMs = QDateTime::currentMSecsSinceEpoch() + 15000;
        while (!m_clipboardPromiseFileResults.contains(cleanedLocalPath) && !m_shutdownStarted.load())
        {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (nowMs >= deadlineMs)
                break;
            m_clipboardPromiseFileWait.wait(
                &m_transferMutex,
                static_cast<unsigned long>(qMin<qint64>(1000, deadlineMs - nowMs)));
        }

        hasResult = m_clipboardPromiseFileResults.contains(cleanedLocalPath);
        if (hasResult)
            status = m_clipboardPromiseFileResults.take(cleanedLocalPath);
        m_pendingClipboardPromiseTargets.remove(cleanedLocalPath);
    }

    if (!hasResult || !status || !QFileInfo(cleanedLocalPath).exists())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCli", "Failed to receive clipboard promise file.");
        return false;
    }
    return true;
}

void WebRtcCli::noteClipboardPromisedFileResult(const QString &path, bool status)
{
    const QString cleanedPath = QDir::cleanPath(path);
    QMutexLocker locker(&m_transferMutex);
    if (!m_pendingClipboardPromiseTargets.contains(cleanedPath))
        return;

    m_clipboardPromiseFileResults.insert(cleanedPath, status);
    m_clipboardPromiseFileWait.wakeAll();
}

QByteArray WebRtcCli::readControllerClipboardFileChunk(const QString &baseRequestId,
                                                       const QString &sourcePath,
                                                       qint64 offset,
                                                       qint64 maxBytes,
                                                       bool *ok,
                                                       QString *errorMessage)
{
    if (ok)
        *ok = false;
    if (sourcePath.isEmpty() || offset < 0 || maxBytes <= 0)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCli", "Invalid clipboard stream read request.");
        return QByteArray();
    }

    QByteArray result;
    qint64 currentOffset = offset;
    qint64 remaining = maxBytes;

    while (remaining > 0)
    {
        const qint64 requestBytes = (std::min<qint64>)(remaining, kMaxClipboardFileChunkBytes);
        const QString requestId = QStringLiteral("%1-%2").arg(baseRequestId, uuidWithoutBraces());
        const QJsonObject request = JsonUtil::createObject()
                                        .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_STREAM_READ_REQUEST)
                                        .add(Constant::KEY_REQUEST_ID, requestId)
                                        .add(Constant::KEY_PATH, sourcePath)
                                        .add(Constant::KEY_OFFSET, static_cast<double>(currentOffset))
                                        .add(Constant::KEY_LENGTH, static_cast<double>(requestBytes))
                                        .build();

        {
            QMutexLocker locker(&m_transferMutex);
            m_pendingClipboardStreamRequests.insert(requestId, requestBytes);
        }

        bool queued = true;
        if (QThread::currentThread() == thread())
        {
            sendClipboardChannelMessage(request);
        }
        else
        {
            queued = QMetaObject::invokeMethod(this,
                                               "sendClipboardChannelMessage",
                                               Qt::QueuedConnection,
                                               Q_ARG(QJsonObject, request));
        }
        if (!queued)
        {
            QMutexLocker locker(&m_transferMutex);
            m_pendingClipboardStreamRequests.remove(requestId);
            if (errorMessage)
                *errorMessage = QCoreApplication::translate("WebRtcCli", "Failed to queue clipboard stream read request.");
            return result;
        }

        QByteArray chunk;
        QString chunkError;
        bool hasResult = false;
        {
            QMutexLocker locker(&m_transferMutex);
            const qint64 deadlineMs = QDateTime::currentMSecsSinceEpoch() + 15000;
            while (!m_clipboardStreamChunks.contains(requestId) &&
                   !m_clipboardStreamErrors.contains(requestId) &&
                   !m_shutdownStarted.load())
            {
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                if (nowMs >= deadlineMs)
                    break;
                m_clipboardStreamWait.wait(&m_transferMutex, static_cast<unsigned long>(qMin<qint64>(1000, deadlineMs - nowMs)));
            }

            if (m_clipboardStreamChunks.contains(requestId))
            {
                chunk = m_clipboardStreamChunks.take(requestId);
                hasResult = true;
            }
            if (m_clipboardStreamErrors.contains(requestId))
                chunkError = m_clipboardStreamErrors.take(requestId);
            m_pendingClipboardStreamRequests.remove(requestId);
        }

        if (!hasResult)
        {
            if (errorMessage)
                *errorMessage = chunkError.isEmpty()
                                    ? QStringLiteral("Clipboard stream read timed out or session closed.")
                                    : chunkError;
            return result;
        }

        if (chunk.isEmpty())
            break;

        result.append(chunk);
        currentOffset += chunk.size();
        remaining -= chunk.size();
        if (chunk.size() < requestBytes)
            break;
    }

    if (ok)
        *ok = true;
    return result;
}

void WebRtcCli::handleClipboardStreamReadRequest(const QJsonObject &object)
{
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
    const QString path = JsonUtil::getString(object, Constant::KEY_PATH);
    const qint64 offset = JsonUtil::getInt64(object, Constant::KEY_OFFSET, 0);
    const qint64 length = JsonUtil::getInt64(object, Constant::KEY_LENGTH, 0);

    bool ok = false;
    QString errorMessage;
    const QByteArray data = readLocalFileChunk(path, offset, length, &ok, &errorMessage);
    const QFileInfo fileInfo(path);
    const QString auditTransferKey = requestId + QLatin1Char('\n') + fileInfo.absoluteFilePath();
    if (ok && fileInfo.exists() && offset + data.size() >= fileInfo.size() &&
        !m_auditedClipboardDownloads.contains(auditTransferKey))
    {
        m_auditedClipboardDownloads.insert(auditTransferKey);
        if (m_auditSession)
            m_auditSession->recordFileTransfer(path, fileInfo.size(), QStringLiteral("download"),
                                               AuditSession::sha256ForFile(path), true);
    }
    QJsonObject response = JsonUtil::createObject()
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_STREAM_READ_RESULT)
                               .add(Constant::KEY_REQUEST_ID, requestId)
                               .add(Constant::KEY_STATUS, ok)
                               .add(Constant::KEY_ERROR, errorMessage)
                               .add(Constant::KEY_DATA, QString::fromLatin1(data.toBase64()))
                               .build();
    sendClipboardChannelMessage(response);
}

void WebRtcCli::handleClipboardStreamReadResult(const QJsonObject &object)
{
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
    if (requestId.isEmpty())
        return;

    const bool ok = JsonUtil::getBool(object, Constant::KEY_STATUS, false);
    const QString errorMessage = JsonUtil::getString(object, Constant::KEY_ERROR);
    const QString encodedData = JsonUtil::getString(object, Constant::KEY_DATA);
    const bool encodedDataTooLarge = encodedData.size() > kMaxClipboardFileChunkEncodedBytes;
    const QByteArray data = encodedDataTooLarge ? QByteArray() : QByteArray::fromBase64(encodedData.toLatin1());

    QMutexLocker locker(&m_transferMutex);
    const auto pending = m_pendingClipboardStreamRequests.constFind(requestId);
    if (pending == m_pendingClipboardStreamRequests.cend() ||
        m_clipboardStreamChunks.contains(requestId) || m_clipboardStreamErrors.contains(requestId))
        return;
    if (ok && !encodedDataTooLarge && data.size() <= pending.value())
        m_clipboardStreamChunks.insert(requestId, data);
    else
        m_clipboardStreamErrors.insert(requestId, !ok && !errorMessage.isEmpty()
                                                      ? errorMessage
                                                      : encodedDataTooLarge || data.size() > pending.value()
                                                            ? QStringLiteral("Clipboard stream response exceeded requested length.")
                                                            : QStringLiteral("Clipboard stream read failed."));
    m_clipboardStreamWait.wakeAll();
}


void WebRtcCli::handleClipboardPayloadBegin(const QJsonObject &object)
{
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
    if (requestId.isEmpty())
        return;

    const qint64 expectedBytes = JsonUtil::getInt64(object, Constant::KEY_PAYLOAD_SIZE, 0);
    const int chunkCount = JsonUtil::getInt(object, Constant::KEY_CHUNK_COUNT, 0);
    const qint64 expectedChunkCount = (expectedBytes + kClipboardPayloadChunkBytes - 1) / kClipboardPayloadChunkBytes;
    if (expectedBytes <= 0 || expectedBytes > kMaxClipboardPayloadBytes ||
        chunkCount <= 0 || chunkCount != expectedChunkCount)
    {
        LOG_WARN("Rejected invalid clipboard payload begin: requestId={}, bytes={}, chunks={}",
                 requestId, expectedBytes, chunkCount);
        return;
    }

    QMutexLocker locker(&m_transferMutex);
    if (m_clipboardInboundPayloadChunks.contains(requestId))
    {
        LOG_WARN("Rejected duplicate clipboard payload begin: requestId={}", requestId);
        return;
    }
    if (m_clipboardInboundPayloadChunks.size() >= kMaxClipboardPayloadRequests ||
        expectedBytes > kMaxClipboardInboundPayloadBytes - m_clipboardInboundPayloadReservedBytes)
    {
        LOG_WARN("Rejected clipboard payload begin: request or memory limit exceeded");
        return;
    }
    m_clipboardInboundPayloadChunks.insert(requestId, QByteArray());
    m_clipboardInboundPayloadChunkCounts.insert(requestId, chunkCount);
    m_clipboardInboundPayloadNextIndexes.insert(requestId, 0);
    m_clipboardInboundPayloadExpectedBytes.insert(requestId, expectedBytes);
    m_clipboardInboundPayloadPasteAfterApply.insert(
        requestId, JsonUtil::getBool(object, Constant::KEY_PASTE_AFTER_APPLY, false));
    m_clipboardInboundPayloadDeadlinesMs.insert(
        requestId, QDateTime::currentMSecsSinceEpoch() + kClipboardPayloadReceiveTimeoutMs);
    m_clipboardInboundPayloadReservedBytes += expectedBytes;
    const QPointer<WebRtcCli> guard(this);
    QTimer::singleShot(kClipboardPayloadReceiveTimeoutMs, this, [guard]() {
        if (guard)
            guard->expireClipboardPayloadRequests();
    });
}


void WebRtcCli::handleClipboardPayloadChunk(const QJsonObject &object)
{
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
    if (requestId.isEmpty())
        return;

    const int chunkIndex = JsonUtil::getInt(object, Constant::KEY_CHUNK_INDEX, -1);
    const int chunkCount = JsonUtil::getInt(object, Constant::KEY_CHUNK_COUNT, 0);
    const QString encodedChunk = JsonUtil::getString(object, Constant::KEY_CHUNK_DATA);
    const QByteArray chunk = encodedChunk.size() <= kMaxClipboardPayloadChunkEncodedBytes
                                 ? QByteArray::fromBase64(encodedChunk.toLatin1())
                                 : QByteArray();

    QMutexLocker locker(&m_transferMutex);
    if (!m_clipboardInboundPayloadChunks.contains(requestId))
        return;
    const int expectedCount = m_clipboardInboundPayloadChunkCounts.value(requestId);
    const int nextIndex = m_clipboardInboundPayloadNextIndexes.value(requestId);
    const qint64 expectedBytes = m_clipboardInboundPayloadExpectedBytes.value(requestId);
    if (chunkIndex != nextIndex || chunkCount != expectedCount || chunk.isEmpty() ||
        chunk.size() > kClipboardPayloadChunkBytes ||
        m_clipboardInboundPayloadChunks[requestId].size() + static_cast<qint64>(chunk.size()) > expectedBytes)
    {
        LOG_WARN("Rejected invalid clipboard payload chunk: requestId={}, index={}, expected={}, size={}",
                 requestId, chunkIndex, nextIndex, chunk.size());
        removeClipboardInboundPayloadLocked(requestId);
        return;
    }
    m_clipboardInboundPayloadChunks[requestId].append(chunk);
    m_clipboardInboundPayloadNextIndexes[requestId] = nextIndex + 1;
    m_clipboardInboundPayloadDeadlinesMs[requestId] =
        QDateTime::currentMSecsSinceEpoch() + kClipboardPayloadReceiveTimeoutMs;
}


void WebRtcCli::handleClipboardPayloadEnd(const QJsonObject &object)
{
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
    if (requestId.isEmpty())
        return;

    QByteArray payloadBytes;
    bool pasteAfterApply = false;
    bool complete = false;
    {
        QMutexLocker locker(&m_transferMutex);
        const int expectedCount = m_clipboardInboundPayloadChunkCounts.value(requestId, -1);
        const qint64 expectedBytes = m_clipboardInboundPayloadExpectedBytes.value(requestId, -1);
        complete = expectedCount > 0 &&
                   JsonUtil::getInt(object, Constant::KEY_CHUNK_COUNT, 0) == expectedCount &&
                   m_clipboardInboundPayloadNextIndexes.value(requestId, -1) == expectedCount &&
                   m_clipboardInboundPayloadChunks.value(requestId).size() == expectedBytes;
        payloadBytes = m_clipboardInboundPayloadChunks.value(requestId);
        pasteAfterApply = m_clipboardInboundPayloadPasteAfterApply.value(requestId);
        removeClipboardInboundPayloadLocked(requestId);
    }

    const QJsonObject payload = complete ? JsonUtil::safeParseObject(payloadBytes) : QJsonObject();
    if (!complete || !JsonUtil::isValidObject(payload) ||
        (JsonUtil::getString(payload, Constant::KEY_TEXT).isEmpty() &&
         !ClipboardUtil::payloadHasFiles(payload) && !ClipboardUtil::payloadHasImage(payload)))
    {
        LOG_WARN("Rejected incomplete clipboard payload: requestId={}", requestId);
        return;
    }

    applyClipboardPayload(requestId, payload, pasteAfterApply);
}


void WebRtcCli::removeClipboardInboundPayloadLocked(const QString &requestId)
{
    const qint64 reservedBytes = m_clipboardInboundPayloadExpectedBytes.value(requestId, 0);
    m_clipboardInboundPayloadReservedBytes = qMax<qint64>(0, m_clipboardInboundPayloadReservedBytes - reservedBytes);
    m_clipboardInboundPayloadChunks.remove(requestId);
    m_clipboardInboundPayloadChunkCounts.remove(requestId);
    m_clipboardInboundPayloadNextIndexes.remove(requestId);
    m_clipboardInboundPayloadExpectedBytes.remove(requestId);
    m_clipboardInboundPayloadPasteAfterApply.remove(requestId);
    m_clipboardInboundPayloadDeadlinesMs.remove(requestId);
}


void WebRtcCli::expireClipboardPayloadRequests()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    qint64 nextDeadlineMs = 0;
    {
        QMutexLocker locker(&m_transferMutex);
        const QStringList requestIds = m_clipboardInboundPayloadDeadlinesMs.keys();
        for (const QString &requestId : requestIds)
        {
            const qint64 deadlineMs = m_clipboardInboundPayloadDeadlinesMs.value(requestId);
            if (deadlineMs <= nowMs)
            {
                LOG_WARN("Expired incomplete clipboard payload: requestId={}", requestId);
                removeClipboardInboundPayloadLocked(requestId);
                continue;
            }
            if (nextDeadlineMs == 0 || deadlineMs < nextDeadlineMs)
                nextDeadlineMs = deadlineMs;
        }
    }
    if (nextDeadlineMs > 0)
    {
        const QPointer<WebRtcCli> guard(this);
        QTimer::singleShot(static_cast<int>(qMax<qint64>(1, nextDeadlineMs - nowMs)), this, [guard]() {
            if (guard)
                guard->expireClipboardPayloadRequests();
        });
    }
}


void WebRtcCli::clearClipboardPayloadTransferState()
{
    QMutexLocker locker(&m_transferMutex);
    m_clipboardInboundPayloadChunks.clear();
    m_clipboardInboundPayloadChunkCounts.clear();
    m_clipboardInboundPayloadNextIndexes.clear();
    m_clipboardInboundPayloadExpectedBytes.clear();
    m_clipboardInboundPayloadPasteAfterApply.clear();
    m_clipboardInboundPayloadDeadlinesMs.clear();
    m_clipboardInboundPayloadReservedBytes = 0;
}


void WebRtcCli::handleClipboardTextBegin(const QJsonObject &object)
{
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
    if (requestId.isEmpty())
        return;

    const qint64 expectedBytes = JsonUtil::getInt64(object, Constant::KEY_TEXT_SIZE, 0);
    const int chunkCount = JsonUtil::getInt(object, Constant::KEY_CHUNK_COUNT, 0);
    if (expectedBytes <= 0 || expectedBytes > kMaxClipboardTextBytes || chunkCount <= 0)
    {
        LOG_WARN("Rejected invalid clipboard text begin: requestId={}, bytes={}, chunks={}",
                 requestId, expectedBytes, chunkCount);
        return;
    }
    const qint64 expectedChunkCount = (expectedBytes + kClipboardTextChunkBytes - 1) / kClipboardTextChunkBytes;
    if (chunkCount != expectedChunkCount)
    {
        LOG_WARN("Rejected invalid clipboard text begin: requestId={}, bytes={}, chunks={}",
                 requestId, expectedBytes, chunkCount);
        return;
    }

    QMutexLocker locker(&m_transferMutex);
    if (!m_clipboardInboundTextChunks.contains(requestId) &&
        m_clipboardInboundTextChunks.size() >= kMaxClipboardTextRequests)
    {
        LOG_WARN("Rejected clipboard text begin: too many active requests");
        return;
    }
    m_clipboardInboundTextChunks.insert(requestId, QByteArray());
    m_clipboardInboundTextChunkCounts.insert(requestId, chunkCount);
    m_clipboardInboundTextNextIndexes.insert(requestId, 0);
    m_clipboardInboundTextExpectedBytes.insert(requestId, expectedBytes);
    m_clipboardInboundTextPasteAfterApply.insert(requestId, JsonUtil::getBool(object, Constant::KEY_PASTE_AFTER_APPLY, false));
}

void WebRtcCli::handleClipboardTextChunk(const QJsonObject &object)
{
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
    if (requestId.isEmpty())
        return;

    const int chunkIndex = JsonUtil::getInt(object, Constant::KEY_CHUNK_INDEX, -1);
    const int chunkCount = JsonUtil::getInt(object, Constant::KEY_CHUNK_COUNT, 0);
    const QString encodedChunk = JsonUtil::getString(object, Constant::KEY_CHUNK_DATA);
    const QByteArray chunk = encodedChunk.size() <= kMaxClipboardTextChunkEncodedBytes
                                 ? QByteArray::fromBase64(encodedChunk.toLatin1())
                                 : QByteArray();
    QMutexLocker locker(&m_transferMutex);
    if (!m_clipboardInboundTextChunks.contains(requestId))
        return;
    const int expectedCount = m_clipboardInboundTextChunkCounts.value(requestId);
    const int nextIndex = m_clipboardInboundTextNextIndexes.value(requestId);
    const qint64 expectedBytes = m_clipboardInboundTextExpectedBytes.value(requestId);
    if (chunkIndex != nextIndex || chunkCount != expectedCount || chunk.isEmpty() ||
        chunk.size() > kClipboardTextChunkBytes ||
        m_clipboardInboundTextChunks[requestId].size() + static_cast<qint64>(chunk.size()) > expectedBytes)
    {
        LOG_WARN("Rejected invalid clipboard text chunk: requestId={}, index={}, expected={}, size={}",
                 requestId, chunkIndex, nextIndex, chunk.size());
        m_clipboardInboundTextChunks.remove(requestId);
        m_clipboardInboundTextChunkCounts.remove(requestId);
        m_clipboardInboundTextNextIndexes.remove(requestId);
        m_clipboardInboundTextExpectedBytes.remove(requestId);
        m_clipboardInboundTextPasteAfterApply.remove(requestId);
        return;
    }
    m_clipboardInboundTextChunks[requestId].append(chunk);
    m_clipboardInboundTextNextIndexes[requestId] = nextIndex + 1;
}

void WebRtcCli::handleClipboardTextEnd(const QJsonObject &object)
{
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
    if (requestId.isEmpty())
        return;

    QByteArray textBytes;
    bool pasteAfterApply = false;
    bool complete = false;
    {
        QMutexLocker locker(&m_transferMutex);
        const int expectedCount = m_clipboardInboundTextChunkCounts.value(requestId, -1);
        const qint64 expectedBytes = m_clipboardInboundTextExpectedBytes.value(requestId, -1);
        complete = expectedCount > 0 &&
                   JsonUtil::getInt(object, Constant::KEY_CHUNK_COUNT, 0) == expectedCount &&
                   m_clipboardInboundTextNextIndexes.value(requestId, -1) == expectedCount &&
                   m_clipboardInboundTextChunks.value(requestId).size() == expectedBytes;
        textBytes = m_clipboardInboundTextChunks.take(requestId);
        m_clipboardInboundTextChunkCounts.remove(requestId);
        m_clipboardInboundTextNextIndexes.remove(requestId);
        m_clipboardInboundTextExpectedBytes.remove(requestId);
        pasteAfterApply = m_clipboardInboundTextPasteAfterApply.take(requestId);
    }

    if (!complete || textBytes.isEmpty())
    {
        LOG_WARN("Rejected incomplete clipboard text: requestId={}", requestId);
        return;
    }

    const QJsonObject payload = JsonUtil::createObject()
                                    .add(Constant::KEY_TEXT, QString::fromUtf8(textBytes))
                                    .build();
    const QPointer<WebRtcCli> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    ClipboardUtil::writeClipboardPayloadAsync(
        this,
        payload,
        [guard, callbackLifetime, requestId, payload, pasteAfterApply](bool ok, const QString &errorMessage) {
            auto permit = callbackLifetime->tryEnter();
            if (!permit)
                return;
            if (!guard || guard->m_destroying)
                return;
            if (ok)
            {
                guard->m_lastRemoteAppliedClipboardPayloadSignature = JsonUtil::toCompactBytes(payload);
                setLastRemoteAppliedClipboardPayloadSignature(guard->m_lastRemoteAppliedClipboardPayloadSignature);
            }
            QJsonObject response = JsonUtil::createObject()
                                       .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_APPLY_RESULT)
                                       .add(Constant::KEY_REQUEST_ID, requestId)
                                       .add(Constant::KEY_STATUS, ok)
                                       .add(Constant::KEY_ERROR, errorMessage)
                                       .add(Constant::KEY_PASTE_AFTER_APPLY, pasteAfterApply)
                                       .build();
            guard->sendClipboardChannelMessage(response);
        });
}
