#include "webrtc/ctl/webrtc_ctl.h"

#include "common/qt_rtc_metatypes.h"
#include "util/clipboard/native/clipboard_file_promise.h"
#include "util/clipboard/clipboard_util.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

#include <QDir>
#include <QCoreApplication>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QList>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVector>

#include <algorithm>
#include <limits>

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
constexpr int kMaxPendingClipboardControlMessages = 128;
constexpr qint64 kMaxPendingClipboardControlMessageBytes = 2LL * 1024 * 1024;
constexpr qint64 kMaxClipboardInboundTextBytes = 64LL * 1024 * 1024;
constexpr int kClipboardTextReceiveTimeoutMs = 15000;
constexpr qint64 kMaxClipboardInboundPayloadBytes = 64LL * 1024 * 1024;
constexpr int kClipboardPayloadReceiveTimeoutMs = 45000;
constexpr int kMaxClipboardStreamReadRequests = 32;
constexpr int kMaxClipboardPromiseExpansionFiles = 100000;
constexpr int kMaxClipboardRequestIdChars = 256;
constexpr int kMaxClipboardPathChars = 32 * 1024;

QString cleanPath(const QString &path)
{
    return QDir::cleanPath(path);
}

QString uniqueChildPath(const QString &root, const QString &name, QSet<QString> *used)
{
    QFileInfo info(name);
    QString base = info.completeBaseName();
    const QString suffix = info.suffix();
    if (base.isEmpty())
        base = info.fileName().isEmpty() ? QStringLiteral("item") : info.fileName();

    QString candidate = QDir(root).filePath(info.fileName().isEmpty() ? base : info.fileName());
    int index = 2;
    while (used && used->contains(cleanPath(candidate)))
    {
        const QString numbered = suffix.isEmpty()
                                     ? QStringLiteral("%1 (%2)").arg(base).arg(index)
                                     : QStringLiteral("%1 (%2).%3").arg(base).arg(index).arg(suffix);
        candidate = QDir(root).filePath(numbered);
        ++index;
    }
    if (used)
        used->insert(cleanPath(candidate));
    return cleanPath(candidate);
}

QJsonObject fileTransferMessage(const QString &msgType,
                                const QString &ctlPath,
                                const QString &cliPath,
                                const QString &transferId)
{
    return JsonUtil::createObject()
        .add(Constant::KEY_MSGTYPE, msgType)
        .add(Constant::KEY_PATH_CTL, ctlPath)
        .add(Constant::KEY_PATH_CLI, cliPath)
        .add(Constant::KEY_TRANSFER_ID, transferId)
        .build();
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
    bool expansionOverflowed = false;
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
            if (promiseFiles.size() >= kMaxClipboardPromiseExpansionFiles)
            {
                expansionOverflowed = true;
                break;
            }
            appendPromiseFile(&promiseFiles, path, rootName, info);
            continue;
        }

        if (promiseFiles.size() >= kMaxClipboardPromiseExpansionFiles)
        {
            expansionOverflowed = true;
            break;
        }
        appendPromiseFile(&promiseFiles, path, rootName, info);
        QDir rootDir(path);
        QDirIterator it(path,
                        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            if (promiseFiles.size() >= kMaxClipboardPromiseExpansionFiles)
            {
                expansionOverflowed = true;
                break;
            }
            const QString childPath = it.next();
            const QFileInfo childInfo = it.fileInfo();
            const QString relative = rootName + QLatin1Char('/') + rootDir.relativeFilePath(childPath);
            appendPromiseFile(&promiseFiles, childPath, relative, childInfo);
        }
        if (expansionOverflowed)
            break;
    }

    if (expansionOverflowed)
    {
        LOG_WARN("Clipboard promise expansion exceeded the safe file limit: limit={}",
                 kMaxClipboardPromiseExpansionFiles);
        return QJsonObject();
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

QJsonObject payloadWithMappedFilePaths(const QJsonObject &sourcePayload, const QStringList &targetPaths)
{
    QJsonArray files;
    const QJsonArray sourceFiles = JsonUtil::getArray(sourcePayload, Constant::KEY_FILES);
    const int count = (std::min)(sourceFiles.size(), targetPaths.size());
    for (int i = 0; i < count; ++i)
    {
        const QJsonObject sourceFile = sourceFiles.at(i).toObject();
        const QString targetPath = QDir::cleanPath(targetPaths.at(i));
        const QString sourcePath = JsonUtil::getString(sourceFile, Constant::KEY_PATH);
        QFileInfo targetInfo(targetPath);
        QFileInfo sourceInfo(sourcePath);
        const QString name = JsonUtil::getString(sourceFile,
                                                 Constant::KEY_NAME,
                                                 targetInfo.fileName().isEmpty() ? sourceInfo.fileName() : targetInfo.fileName());
        QJsonObject file = JsonUtil::createObject()
                               .add(Constant::KEY_PATH, targetPath)
                               .add(Constant::KEY_NAME, name)
                               .add(Constant::KEY_IS_DIR, JsonUtil::getBool(sourceFile, Constant::KEY_IS_DIR, false))
                               .add(Constant::KEY_FILE_SIZE, static_cast<double>(JsonUtil::getInt64(sourceFile, Constant::KEY_FILE_SIZE, 0)))
                               .build();
        files.append(file);
    }

    QJsonObject payload = JsonUtil::createObject()
                              .add(Constant::KEY_FILES, files)
                              .build();
    const QString text = JsonUtil::getString(sourcePayload, Constant::KEY_TEXT);
    if (!text.isEmpty())
        payload.insert(Constant::KEY_TEXT, text);
    if (ClipboardUtil::payloadHasImage(sourcePayload))
    {
        payload.insert(Constant::KEY_IMAGE, JsonUtil::getString(sourcePayload, Constant::KEY_IMAGE));
        payload.insert(Constant::KEY_IMAGE_FORMAT, JsonUtil::getString(sourcePayload, Constant::KEY_IMAGE_FORMAT));
    }
    return payload;
}

QByteArray readLocalFileChunk(const QString &path, qint64 offset, qint64 maxBytes, bool *ok, QString *errorMessage)
{
    if (ok)
        *ok = false;
    if (path.isEmpty() || offset < 0 || maxBytes <= 0 || maxBytes > kMaxClipboardFileChunkBytes)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCtl", "Invalid clipboard stream read range.");
        return QByteArray();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCtl", "Cannot open source file.");
        return QByteArray();
    }
    if (!file.seek(offset))
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCtl", "Cannot seek source file.");
        return QByteArray();
    }

    const QByteArray data = file.read(maxBytes);
    if (data.size() < 0)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCtl", "Cannot read source file.");
        return QByteArray();
    }
    if (ok)
        *ok = true;
    return data;
}
} // namespace

void WebRtcCtl::onClipboardChannelOpen()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onClipboardChannelOpen();
        });
        return;
    }
    LOG_INFO("Clipboard channel opened");
    clearClipboardPayloadTransferState();
    m_remoteSupportsClipboardPayloadV2 = false;
    flushPendingClipboardControlMessages();
    sendClipboardChannelObject(JsonUtil::createObject()
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_CAPABILITIES)
                                   .add(Constant::KEY_CLIPBOARD_PAYLOAD_V2, true)
                                   .build());
}

void WebRtcCtl::onClipboardChannelClosed()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onClipboardChannelClosed();
        });
        return;
    }
    LOG_INFO("Clipboard channel closed, reconnecting");
    m_pendingClipboardControlMessages.clear();
    m_pendingClipboardControlMessageBytes = 0;
    m_remoteSupportsClipboardPayloadV2 = false;
    failClipboardChunkSendQueue();
    clearClipboardPayloadTransferState();
    requestSessionReconnect(tr("Clipboard channel closed, reconnecting..."));
}

void WebRtcCtl::onClipboardChannelError(const std::string &error)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const std::string errorCopy = error;
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, errorCopy]() {
            if (guard)
                guard->onClipboardChannelError(errorCopy);
        });
        return;
    }
    LOG_ERROR("Clipboard channel error, reconnecting: {}", error);
    m_pendingClipboardControlMessages.clear();
    m_pendingClipboardControlMessageBytes = 0;
    m_remoteSupportsClipboardPayloadV2 = false;
    failClipboardChunkSendQueue();
    clearClipboardPayloadTransferState();
    requestSessionReconnect(tr("Clipboard channel error, reconnecting..."));
}

void WebRtcCtl::onClipboardChannelMessage(const rtc::message_variant &message)
{
    if (m_shutdownStarted.load())
        return;
    noteSessionInboundActivity();
    if (!std::holds_alternative<std::string>(message))
    {
        LOG_WARN("Clipboard channel received binary data, ignoring");
        return;
    }
    if (std::get<std::string>(message).size() > kMaxClipboardChannelMessageBytes)
    {
        LOG_WARN("Rejected oversized clipboard channel message: size={} bytes", std::get<std::string>(message).size());
        return;
    }
    if (QThread::currentThread() != thread())
    {
        const rtc::message_variant messageCopy = message;
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, messageCopy]() {
            if (guard)
                guard->onClipboardChannelMessage(messageCopy);
        });
        return;
    }
    const std::string &data = std::get<std::string>(message);
    const QJsonObject object = JsonUtil::safeParseObject(
        QByteArray::fromRawData(data.data(), static_cast<int>(data.size())));
    if (!JsonUtil::isValidObject(object))
    {
        LOG_WARN("Clipboard channel received invalid JSON");
        return;
    }
    handleClipboardChannelObject(object);
}

void WebRtcCtl::handleClipboardChannelObject(const QJsonObject &object)
{
    const QString msgType = JsonUtil::getString(object, Constant::KEY_MSGTYPE);
    if (msgType == Constant::TYPE_CLIPBOARD_CAPABILITIES)
    {
        m_remoteSupportsClipboardPayloadV2 = JsonUtil::getBool(
            object, Constant::KEY_CLIPBOARD_PAYLOAD_V2, false);
    }
    else if (msgType == Constant::TYPE_CLIPBOARD_PREPARE_UPLOAD_RESULT)
    {
        handleClipboardPrepareUploadResult(object);
    }
    else if (msgType == Constant::TYPE_CLIPBOARD_SNAPSHOT)
    {
        handleClipboardSnapshot(object);
    }
    else if (msgType == Constant::TYPE_CLIPBOARD_APPLY_RESULT)
    {
        handleClipboardApplyResult(object);
    }
    else if (msgType == Constant::TYPE_CLIPBOARD_PROMISE_FILE_REQUEST)
    {
        handleClipboardPromiseFileRequest(object);
    }
    else if (msgType == Constant::TYPE_CLIPBOARD_STREAM_READ_REQUEST)
    {
        handleClipboardStreamReadRequest(object);
    }
    else if (msgType == Constant::TYPE_CLIPBOARD_STREAM_READ_RESULT)
    {
        handleClipboardStreamReadResult(object);
    }
    else if (msgType == Constant::TYPE_CLIPBOARD_TEXT_BEGIN)
    {
        handleClipboardTextBegin(object);
    }
    else if (msgType == Constant::TYPE_CLIPBOARD_TEXT_CHUNK)
    {
        handleClipboardTextChunk(object);
    }
    else if (msgType == Constant::TYPE_CLIPBOARD_TEXT_END)
    {
        handleClipboardTextEnd(object);
    }
    else if (msgType == Constant::TYPE_CLIPBOARD_PAYLOAD_BEGIN)
    {
        handleClipboardPayloadBegin(object);
    }
    else if (msgType == Constant::TYPE_CLIPBOARD_PAYLOAD_CHUNK)
    {
        handleClipboardPayloadChunk(object);
    }
    else if (msgType == Constant::TYPE_CLIPBOARD_PAYLOAD_END)
    {
        handleClipboardPayloadEnd(object);
    }
    else
    {
        LOG_WARN("Unknown clipboard channel message type: {}", msgType);
    }
}

void WebRtcCtl::queueClipboardControlMessage(const QJsonObject &object)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    const qint64 bytes = JsonUtil::toCompactBytes(object).size();
    if (bytes > static_cast<qint64>(kMaxClipboardChannelMessageBytes) ||
        m_pendingClipboardControlMessages.size() >= kMaxPendingClipboardControlMessages ||
        bytes > kMaxPendingClipboardControlMessageBytes - m_pendingClipboardControlMessageBytes)
    {
        LOG_ERROR("Clipboard control queue overflow; reconnecting instead of dropping clipboard protocol data: size={}, queuedBytes={}, queuedMessages={}",
                  bytes, m_pendingClipboardControlMessageBytes, m_pendingClipboardControlMessages.size());
        requestSessionReconnect(tr("Clipboard control queue is full, reconnecting..."));
        return;
    }
    m_pendingClipboardControlMessages.enqueue(object);
    m_pendingClipboardControlMessageBytes += bytes;
}


void WebRtcCtl::flushPendingClipboardControlMessages()
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    if (!m_clipboardChannel || !m_clipboardChannel->isOpen())
        return;

    while (!m_pendingClipboardControlMessages.isEmpty())
    {
        const QJsonObject &object = m_pendingClipboardControlMessages.head();
        if (!trySendClipboardChannelObject(object))
            return;
        m_pendingClipboardControlMessageBytes -= JsonUtil::toCompactBytes(object).size();
        m_pendingClipboardControlMessages.dequeue();
    }
}


bool WebRtcCtl::sendClipboardChannelObject(const QJsonObject &object)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return false;

    if (!m_clipboardChannel || !m_clipboardChannel->isOpen())
    {
        queueClipboardControlMessage(object);
        return false;
    }

    flushPendingClipboardControlMessages();
    if (!m_pendingClipboardControlMessages.isEmpty())
    {
        queueClipboardControlMessage(object);
        return false;
    }
    if (trySendClipboardChannelObject(object))
        return true;
    queueClipboardControlMessage(object);
    return false;
}


bool WebRtcCtl::trySendClipboardChannelObject(const QJsonObject &object)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return false;

    if (!m_clipboardChannel || !m_clipboardChannel->isOpen())
    {
        LOG_WARN("Clipboard channel is not ready");
        return false;
    }

    try
    {
        const QByteArray messageBytes = JsonUtil::toCompactBytes(object);
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

void WebRtcCtl::pasteClipboardPayloadToRemote(const QJsonObject &payload)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    sendClipboardPayloadToRemote(payload, true);
}

void WebRtcCtl::syncClipboardPayloadToRemote(const QJsonObject &payload)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    sendClipboardPayloadToRemote(payload, false);
}

void WebRtcCtl::sendClipboardPayloadToRemote(const QJsonObject &payload, bool pasteAfterApply)
{
    QJsonObject compatiblePayload = payload;
    if (!m_remoteSupportsClipboardPayloadV2 && ClipboardUtil::payloadHasImage(compatiblePayload))
    {
        compatiblePayload.remove(Constant::KEY_IMAGE);
        compatiblePayload.remove(Constant::KEY_IMAGE_FORMAT);
        LOG_INFO("Skipped clipboard image for a peer without clipboard payload v2 support");
    }
    if (JsonUtil::getString(compatiblePayload, Constant::KEY_TEXT).isEmpty() &&
        !ClipboardUtil::payloadHasFiles(compatiblePayload) &&
        !ClipboardUtil::payloadHasImage(compatiblePayload))
    {
        return;
    }

    const QString requestId = uuidWithoutBraces();
    if (ClipboardUtil::payloadHasFiles(compatiblePayload))
    {
        if (m_pendingClipboardUploadPayloads.size() >= kMaxClipboardPayloadRequests)
        {
            LOG_WARN("Dropping stale pending clipboard upload payloads: count={}",
                     m_pendingClipboardUploadPayloads.size());
            m_pendingClipboardUploadPayloads.clear();
            m_pendingClipboardUploadPasteAfterApply.clear();
            m_pendingClipboardUploadTargets.clear();
        }
        m_pendingClipboardUploadPayloads.insert(requestId, compatiblePayload);
        m_pendingClipboardUploadPasteAfterApply.insert(requestId, pasteAfterApply);
        QJsonObject request = JsonUtil::createObject()
                                  .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_PREPARE_UPLOAD)
                                  .add(Constant::KEY_REQUEST_ID, requestId)
                                  .build();
        sendClipboardChannelObject(request);
        return;
    }

    if (sendClipboardTextPayloadInChunks(requestId, compatiblePayload, pasteAfterApply))
        return;

    sendClipboardSetPayload(requestId, compatiblePayload, pasteAfterApply);
}

void WebRtcCtl::requestRemoteClipboardSnapshot()
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    QJsonObject request = JsonUtil::createObject()
                              .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_REQUEST)
                              .add(Constant::KEY_REQUEST_ID, uuidWithoutBraces())
                              .build();
    sendClipboardChannelObject(request);
}

bool WebRtcCtl::startRemoteFileDrag(QWidget *dragSource,
                                    const QJsonArray &files,
                                    const QString &requestId,
                                    QString *errorMessage)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCtl", "Remote session is closed.");
        return false;
    }

    if (files.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCtl", "No remote files were selected.");
        return false;
    }
    if (!ClipboardFilePromise::isSupported())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCtl", "Native file promise drag is not supported.");
        return false;
    }

    const QString baseRequestId = requestId.isEmpty() ? uuidWithoutBraces() : requestId;
    QJsonObject payload = JsonUtil::createObject()
                              .add(Constant::KEY_PROMISE_FILES, files)
                              .build();
    const QList<ClipboardFilePromiseItem> promiseItems = filePromiseItemsFromPayload(payload, nullptr);
    if (promiseItems.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCtl", "No valid remote file promises were selected.");
        return false;
    }

    const QString cacheRoot = ClipboardUtil::cacheRoot(baseRequestId);
    {
        QMutexLocker locker(&m_transferMutex);
        m_clipboardCacheRoots.insert(cacheRoot);
    }
    for (const ClipboardFilePromiseItem &item : promiseItems)
    {
        if (item.isDirectory || item.sourcePath.isEmpty())
            continue;

        const QString remotePath = cleanPath(item.sourcePath);
        const QString transferId = QStringLiteral("%1-%2").arg(baseRequestId, uuidWithoutBraces());
        QMutexLocker locker(&m_transferMutex);
        m_clipboardPromiseRemotePathToTransferId.insert(remotePath, transferId);
        m_clipboardPromiseRemotePathToTotalBytes.insert(remotePath, qMax<qint64>(0, item.size));
        m_clipboardPromiseRemotePathToTransferredBytes.insert(remotePath, 0);
    }

    const QPointer<WebRtcCtl> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    const bool started = ClipboardFilePromise::startDrag(
        dragSource,
        promiseItems,
        cacheRoot,
        [guard, callbackLifetime, baseRequestId](const QString &remotePath, qint64 offset, qint64 maxBytes, bool *ok, QString *error) {
            auto permit = callbackLifetime->tryEnter();
            if (!permit)
            {
                if (ok)
                    *ok = false;
                if (error)
                    *error = QCoreApplication::translate("WebRtcCtl", "Remote control session is closed.");
                return QByteArray();
            }
            if (!guard || guard->m_shutdownStarted.load())
            {
                if (ok)
                    *ok = false;
                if (error)
                    *error = QCoreApplication::translate("WebRtcCtl", "Remote control session is closed.");
                return QByteArray();
            }
            return guard->readRemoteClipboardFileChunk(baseRequestId, remotePath, offset, maxBytes, ok, error);
        },
        errorMessage);

    if (!started)
    {
        QMutexLocker locker(&m_transferMutex);
        for (const ClipboardFilePromiseItem &item : promiseItems)
        {
            const QString remotePath = cleanPath(item.sourcePath);
            m_clipboardPromiseRemotePathToTransferId.remove(remotePath);
            m_clipboardPromiseRemotePathToTotalBytes.remove(remotePath);
            m_clipboardPromiseRemotePathToTransferredBytes.remove(remotePath);
            m_startedClipboardPromiseRemotePaths.remove(remotePath);
        }
    }
    return started;
}

void WebRtcCtl::handleClipboardPrepareUploadResult(const QJsonObject &object)
{
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
    const QString cacheRoot = JsonUtil::getString(object, Constant::KEY_CACHE_ROOT);
    const bool status = JsonUtil::getBool(object, Constant::KEY_STATUS, false);
    const bool nativeFilePromise = JsonUtil::getBool(object, Constant::KEY_NATIVE_FILE_PROMISE, false);
    if (!status || requestId.isEmpty() || cacheRoot.isEmpty() || !m_pendingClipboardUploadPayloads.contains(requestId))
    {
        LOG_WARN("Clipboard upload prepare failed: requestId={}, cacheRoot={}, status={}",
                 requestId, cacheRoot, status);
        m_pendingClipboardUploadPayloads.remove(requestId);
        m_pendingClipboardUploadPasteAfterApply.remove(requestId);
        return;
    }

    const QJsonObject sourcePayload = m_pendingClipboardUploadPayloads.take(requestId);
    const bool pasteAfterApply = m_pendingClipboardUploadPasteAfterApply.value(requestId, true);
    const QStringList localPaths = ClipboardUtil::payloadFilePaths(sourcePayload);
    const bool hasAdditionalFormats = ClipboardUtil::payloadHasImage(sourcePayload) ||
                                      !JsonUtil::getString(sourcePayload, Constant::KEY_TEXT).isEmpty();
    if (nativeFilePromise && !hasAdditionalFormats)
    {
        bool allFilesValid = true;
        const QJsonObject promisePayload = payloadWithExpandedPromiseFiles(sourcePayload);
        const QList<ClipboardFilePromiseItem> promiseItems = filePromiseItemsFromPayload(promisePayload, nullptr);
        for (const ClipboardFilePromiseItem &item : promiseItems)
        {
            QFileInfo info(item.sourcePath);
            if (!info.exists() || (item.isDirectory ? !info.isDir() : !info.isFile()))
            {
                allFilesValid = false;
                break;
            }
        }
        if (allFilesValid && !promiseItems.isEmpty())
        {
            for (const ClipboardFilePromiseItem &item : promiseItems)
            {
                if (item.isDirectory || item.sourcePath.isEmpty())
                    continue;

                const QString cleanedCtlPath = cleanPath(item.sourcePath);
                const QString transferId = QStringLiteral("%1-%2").arg(requestId, uuidWithoutBraces());
                const QString targetPath = item.displayName.isEmpty()
                                               ? QFileInfo(cleanedCtlPath).fileName()
                                               : item.displayName;
                {
                    QMutexLocker locker(&m_transferMutex);
                    m_clipboardPromiseUploadSourceToTransferId.insert(cleanedCtlPath, transferId);
                    m_clipboardPromiseUploadSourceToTargetPath.insert(cleanedCtlPath, targetPath);
                    m_clipboardPromiseUploadSourceToTotalBytes.insert(cleanedCtlPath, qMax<qint64>(0, item.size));
                    m_clipboardPromiseUploadSourceToTransferredBytes.insert(cleanedCtlPath, 0);
                    m_startedClipboardPromiseUploadSources.insert(cleanedCtlPath);
                }
                emit fileTransferStarted(transferId, cleanedCtlPath, targetPath, tr("Upload"));
            }
            m_pendingClipboardUploadPasteAfterApply.remove(requestId);
            sendClipboardSetPayload(requestId, promisePayload, pasteAfterApply);
            return;
        }
    }

    QStringList validLocalPaths;
    QStringList remotePaths;
    QSet<QString> expectedTargets;
    QSet<QString> usedRemotePaths;

    for (const QString &localPath : localPaths)
    {
        QFileInfo info(localPath);
        if (!info.exists())
        {
            LOG_WARN("Skipping missing clipboard file: {}", localPath);
            continue;
        }
        const QString remotePath = uniqueChildPath(cacheRoot, info.fileName(), &usedRemotePaths);
        validLocalPaths.append(localPath);
        remotePaths.append(remotePath);
        expectedTargets.insert(remotePath);
    }

    if (remotePaths.isEmpty())
    {
        LOG_WARN("Clipboard paste had no transferable files");
        m_pendingClipboardUploadPasteAfterApply.remove(requestId);
        return;
    }

    m_pendingClipboardUploadPayloads.insert(requestId, payloadWithMappedFilePaths(sourcePayload, remotePaths));
    m_pendingClipboardUploadTargets.insert(requestId, expectedTargets);

    for (int i = 0; i < validLocalPaths.size() && i < remotePaths.size(); ++i)
        uploadFile2CLI(validLocalPaths.at(i), remotePaths.at(i), requestId);
}

void WebRtcCtl::noteClipboardUploadResult(const QString &path, bool status)
{
    const QString cleanedPath = cleanPath(path);
    const QList<QString> requestIds = m_pendingClipboardUploadTargets.keys();
    for (const QString &requestId : requestIds)
    {
        QSet<QString> targets = m_pendingClipboardUploadTargets.value(requestId);
        if (!targets.contains(cleanedPath))
            continue;

        if (!status)
        {
            LOG_WARN("Clipboard upload failed: requestId={}, path={}", requestId, cleanedPath);
            m_pendingClipboardUploadTargets.remove(requestId);
            m_pendingClipboardUploadPayloads.remove(requestId);
            m_pendingClipboardUploadPasteAfterApply.remove(requestId);
            return;
        }

        targets.remove(cleanedPath);
        if (targets.isEmpty())
        {
            const QJsonObject payload = m_pendingClipboardUploadPayloads.take(requestId);
            const bool pasteAfterApply = m_pendingClipboardUploadPasteAfterApply.take(requestId);
            m_pendingClipboardUploadTargets.remove(requestId);
            sendClipboardSetPayload(requestId, payload, pasteAfterApply);
        }
        else
        {
            m_pendingClipboardUploadTargets.insert(requestId, targets);
        }
        return;
    }

    const QString promiseRequestId = m_pendingClipboardPromiseUploadTargets.take(cleanedPath);
    if (!promiseRequestId.isEmpty())
    {
        QJsonObject response = JsonUtil::createObject()
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_PROMISE_FILE_RESULT)
                                   .add(Constant::KEY_REQUEST_ID, promiseRequestId)
                                   .add(Constant::KEY_PATH_CLI, cleanedPath)
                                   .add(Constant::KEY_STATUS, status)
                                   .build();
        sendClipboardChannelObject(response);
    }
}

void WebRtcCtl::sendClipboardSetPayload(const QString &requestId, const QJsonObject &payload, bool pasteAfterApply)
{
    const QPointer<WebRtcCtl> guard(this);
    const auto completion = [guard, requestId](bool ok) {
        if (guard && !ok)
            LOG_WARN("Failed to send chunked clipboard payload: requestId={}", requestId);
    };
    if (sendClipboardPayloadInChunks(requestId, payload, pasteAfterApply, completion))
        return;

    QJsonObject message = JsonUtil::createObject()
                              .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_SET)
                              .add(Constant::KEY_REQUEST_ID, requestId)
                              .add(Constant::KEY_PAYLOAD, payload)
                              .add(Constant::KEY_PASTE_AFTER_APPLY, pasteAfterApply)
                              .build();
    if (!sendClipboardChannelObject(message))
        LOG_WARN("Failed to send clipboard payload: requestId={}", requestId);
}


bool WebRtcCtl::sendClipboardPayloadInChunks(const QString &requestId,
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


bool WebRtcCtl::enqueueClipboardChunkTransfer(bool payloadTransfer,
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


QJsonObject WebRtcCtl::currentClipboardChunkMessage(const ClipboardChunkSendState &state) const
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


void WebRtcCtl::scheduleClipboardChunkSend(int delayMs)
{
    if (m_clipboardChunkSendScheduled || m_clipboardChunkSendQueue.isEmpty())
        return;
    m_clipboardChunkSendScheduled = true;
    const QPointer<WebRtcCtl> guard(this);
    QTimer::singleShot(qMax(0, delayMs), this, [guard]() {
        if (!guard)
            return;
        guard->m_clipboardChunkSendScheduled = false;
        guard->drainClipboardChunkSendQueue();
    });
}


void WebRtcCtl::drainClipboardChunkSendQueue()
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
        flushPendingClipboardControlMessages();
        if (!m_pendingClipboardControlMessages.isEmpty())
        {
            scheduleClipboardChunkSend(kClipboardChunkSendPollMs);
            return;
        }
        if (!trySendClipboardChannelObject(currentClipboardChunkMessage(state)))
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


void WebRtcCtl::failClipboardChunkSendQueue()
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


bool WebRtcCtl::sendClipboardTextPayloadInChunks(const QString &requestId, const QJsonObject &payload, bool pasteAfterApply)
{
    const QString text = JsonUtil::getString(payload, Constant::KEY_TEXT);
    if (text.isEmpty())
        return false;

    QJsonObject payloadWithoutText = payload;
    payloadWithoutText.remove(Constant::KEY_TEXT);
    if (!payloadWithoutText.isEmpty())
        return false;

    const QByteArray textUtf8 = text.toUtf8();
    if (textUtf8.size() <= kClipboardInlineTextLimitBytes)
        return false;
    if (textUtf8.size() > kMaxClipboardTextBytes)
    {
        LOG_WARN("Skipped clipboard text larger than protocol limit: bytes={}", textUtf8.size());
        return true;
    }

    if (!enqueueClipboardChunkTransfer(false, requestId, textUtf8, pasteAfterApply, {}))
        LOG_WARN("Failed to queue chunked clipboard text: bytes={}", textUtf8.size());
    return true;
}

void WebRtcCtl::handleClipboardTextBegin(const QJsonObject &object)
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
    if (m_clipboardInboundTextChunks.contains(requestId))
    {
        LOG_WARN("Rejected duplicate clipboard text begin: requestId={}", requestId);
        return;
    }
    if (m_clipboardInboundTextChunks.size() >= kMaxClipboardTextRequests ||
        expectedBytes > kMaxClipboardInboundTextBytes - m_clipboardInboundTextReservedBytes)
    {
        LOG_WARN("Rejected clipboard text begin: request or memory limit exceeded");
        return;
    }
    m_clipboardInboundTextChunks.insert(requestId, QByteArray());
    m_clipboardInboundTextChunkCounts.insert(requestId, chunkCount);
    m_clipboardInboundTextNextIndexes.insert(requestId, 0);
    m_clipboardInboundTextExpectedBytes.insert(requestId, expectedBytes);
    m_clipboardInboundTextPasteAfterApply.insert(requestId, JsonUtil::getBool(object, Constant::KEY_PASTE_AFTER_APPLY, false));
    m_clipboardInboundTextDeadlinesMs.insert(
        requestId, QDateTime::currentMSecsSinceEpoch() + kClipboardTextReceiveTimeoutMs);
    m_clipboardInboundTextReservedBytes += expectedBytes;
    if (!m_clipboardTextExpiryTimer)
    {
        m_clipboardTextExpiryTimer = new QTimer(this);
        m_clipboardTextExpiryTimer->setSingleShot(true);
        connect(m_clipboardTextExpiryTimer, &QTimer::timeout,
                this, &WebRtcCtl::expireClipboardTextRequests);
    }
    if (!m_clipboardTextExpiryTimer->isActive())
        m_clipboardTextExpiryTimer->start(kClipboardTextReceiveTimeoutMs);
}

void WebRtcCtl::handleClipboardTextChunk(const QJsonObject &object)
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
        removeClipboardInboundTextLocked(requestId);
        return;
    }
    m_clipboardInboundTextChunks[requestId].append(chunk);
    m_clipboardInboundTextNextIndexes[requestId] = nextIndex + 1;
    m_clipboardInboundTextDeadlinesMs[requestId] =
        QDateTime::currentMSecsSinceEpoch() + kClipboardTextReceiveTimeoutMs;
}

void WebRtcCtl::handleClipboardTextEnd(const QJsonObject &object)
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
        textBytes = m_clipboardInboundTextChunks.value(requestId);
        pasteAfterApply = m_clipboardInboundTextPasteAfterApply.value(requestId);
        removeClipboardInboundTextLocked(requestId);
    }
    if (!complete || textBytes.isEmpty())
    {
        LOG_WARN("Rejected incomplete clipboard text: requestId={}", requestId);
        return;
    }

    QJsonObject payload = JsonUtil::createObject()
                              .add(Constant::KEY_TEXT, QString::fromUtf8(textBytes))
                              .build();
    emit localClipboardPayloadReceived(payload);
    if (pasteAfterApply && !m_remoteDesktopLocked)
        sendRemotePasteShortcut();
}

void WebRtcCtl::removeClipboardInboundTextLocked(const QString &requestId)
{
    const qint64 reservedBytes = m_clipboardInboundTextExpectedBytes.value(requestId, 0);
    m_clipboardInboundTextReservedBytes = qMax<qint64>(0, m_clipboardInboundTextReservedBytes - reservedBytes);
    m_clipboardInboundTextChunks.remove(requestId);
    m_clipboardInboundTextChunkCounts.remove(requestId);
    m_clipboardInboundTextNextIndexes.remove(requestId);
    m_clipboardInboundTextExpectedBytes.remove(requestId);
    m_clipboardInboundTextPasteAfterApply.remove(requestId);
    m_clipboardInboundTextDeadlinesMs.remove(requestId);
}

void WebRtcCtl::expireClipboardTextRequests()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    qint64 nextDeadlineMs = 0;
    {
        QMutexLocker locker(&m_transferMutex);
        const QStringList requestIds = m_clipboardInboundTextDeadlinesMs.keys();
        for (const QString &requestId : requestIds)
        {
            const qint64 deadlineMs = m_clipboardInboundTextDeadlinesMs.value(requestId);
            if (deadlineMs <= nowMs)
            {
                LOG_WARN("Expired incomplete clipboard text: requestId={}", requestId);
                removeClipboardInboundTextLocked(requestId);
                continue;
            }
            if (nextDeadlineMs == 0 || deadlineMs < nextDeadlineMs)
                nextDeadlineMs = deadlineMs;
        }
    }
    if (nextDeadlineMs > 0 && m_clipboardTextExpiryTimer)
        m_clipboardTextExpiryTimer->start(static_cast<int>(qMax<qint64>(1, nextDeadlineMs - nowMs)));
}

void WebRtcCtl::handleClipboardPayloadBegin(const QJsonObject &object)
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
    if (!m_clipboardPayloadExpiryTimer)
    {
        m_clipboardPayloadExpiryTimer = new QTimer(this);
        m_clipboardPayloadExpiryTimer->setSingleShot(true);
        connect(m_clipboardPayloadExpiryTimer, &QTimer::timeout,
                this, &WebRtcCtl::expireClipboardPayloadRequests);
    }
    if (!m_clipboardPayloadExpiryTimer->isActive())
        m_clipboardPayloadExpiryTimer->start(kClipboardPayloadReceiveTimeoutMs);
}


void WebRtcCtl::handleClipboardPayloadChunk(const QJsonObject &object)
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


void WebRtcCtl::handleClipboardPayloadEnd(const QJsonObject &object)
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

    handleClipboardSnapshot(JsonUtil::createObject()
                                .add(Constant::KEY_REQUEST_ID, requestId)
                                .add(Constant::KEY_PAYLOAD, payload)
                                .build());
    if (pasteAfterApply && !m_remoteDesktopLocked)
        sendRemotePasteShortcut();
}


void WebRtcCtl::removeClipboardInboundPayloadLocked(const QString &requestId)
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


void WebRtcCtl::expireClipboardPayloadRequests()
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
    if (nextDeadlineMs > 0 && m_clipboardPayloadExpiryTimer)
        m_clipboardPayloadExpiryTimer->start(static_cast<int>(qMax<qint64>(1, nextDeadlineMs - nowMs)));
}


void WebRtcCtl::clearClipboardPayloadTransferState()
{
    QMutexLocker locker(&m_transferMutex);
    if (m_clipboardTextExpiryTimer)
        m_clipboardTextExpiryTimer->stop();
    if (m_clipboardPayloadExpiryTimer)
        m_clipboardPayloadExpiryTimer->stop();
    m_clipboardInboundTextChunks.clear();
    m_clipboardInboundTextChunkCounts.clear();
    m_clipboardInboundTextNextIndexes.clear();
    m_clipboardInboundTextExpectedBytes.clear();
    m_clipboardInboundTextPasteAfterApply.clear();
    m_clipboardInboundTextDeadlinesMs.clear();
    m_clipboardInboundTextReservedBytes = 0;
    m_clipboardInboundPayloadChunks.clear();
    m_clipboardInboundPayloadChunkCounts.clear();
    m_clipboardInboundPayloadNextIndexes.clear();
    m_clipboardInboundPayloadExpectedBytes.clear();
    m_clipboardInboundPayloadPasteAfterApply.clear();
    m_clipboardInboundPayloadDeadlinesMs.clear();
    m_clipboardInboundPayloadReservedBytes = 0;
}


void WebRtcCtl::handleClipboardApplyResult(const QJsonObject &object)
{
    const bool status = JsonUtil::getBool(object, Constant::KEY_STATUS, false);
    const bool pasteAfterApply = JsonUtil::getBool(object, Constant::KEY_PASTE_AFTER_APPLY, false);
    if (!status)
    {
        LOG_WARN("Remote clipboard apply failed: {}", JsonUtil::getString(object, Constant::KEY_ERROR));
        return;
    }

    if (pasteAfterApply && !m_remoteDesktopLocked)
        sendRemotePasteShortcut();
    else if (pasteAfterApply)
        LOG_INFO("Remote clipboard was updated, but paste shortcut was deferred because the remote desktop is locked");
}

void WebRtcCtl::handleClipboardPromiseFileRequest(const QJsonObject &object)
{
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID,
                                                 uuidWithoutBraces());
    const QString ctlPath = JsonUtil::getString(object, Constant::KEY_PATH_CTL);
    const QString cliPath = JsonUtil::getString(object, Constant::KEY_PATH_CLI);
    if (requestId.isEmpty() || ctlPath.isEmpty() || cliPath.isEmpty())
    {
        LOG_WARN("Invalid clipboard promise file request: requestId={}, ctlPath={}, cliPath={}",
                 requestId, ctlPath, cliPath);
        return;
    }

    QFileInfo sourceInfo(ctlPath);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
    {
        QJsonObject response = JsonUtil::createObject()
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_PROMISE_FILE_RESULT)
                                   .add(Constant::KEY_REQUEST_ID, requestId)
                                   .add(Constant::KEY_PATH_CLI, cliPath)
                                   .add(Constant::KEY_STATUS, false)
                                   .add(Constant::KEY_ERROR, QStringLiteral("Source file does not exist."))
                                   .build();
        sendClipboardChannelObject(response);
        return;
    }

    const QString cleanedCtlPath = cleanPath(ctlPath);
    const QString cleanedCliPath = cleanPath(cliPath);
    const QString transferId = QStringLiteral("%1-%2").arg(requestId, uuidWithoutBraces());
    {
        QMutexLocker locker(&m_transferMutex);
        m_clipboardPromiseUploadSourceToTransferId.insert(cleanedCtlPath, transferId);
        m_clipboardPromiseUploadSourceToTargetPath.insert(cleanedCtlPath, cleanedCliPath);
        m_clipboardPromiseUploadSourceToTotalBytes.insert(cleanedCtlPath, qMax<qint64>(0, sourceInfo.size()));
        m_clipboardPromiseUploadSourceToTransferredBytes.insert(cleanedCtlPath, 0);
    }
    m_pendingClipboardPromiseUploadTargets.insert(cleanedCliPath, requestId);
    emit fileTransferStarted(transferId, cleanedCtlPath, cleanedCliPath, tr("Upload"));
    uploadFile2CLI(ctlPath, cliPath, transferId);
}

void WebRtcCtl::handleClipboardStreamReadRequest(const QJsonObject &object)
{
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
    const QString path = JsonUtil::getString(object, Constant::KEY_PATH);
    const qint64 offset = JsonUtil::getInt64(object, Constant::KEY_OFFSET, 0);
    const qint64 length = JsonUtil::getInt64(object, Constant::KEY_LENGTH, 0);

    const auto sendFailure = [this, requestId](const QString &error) {
        sendClipboardChannelObject(JsonUtil::createObject()
                                       .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_STREAM_READ_RESULT)
                                       .add(Constant::KEY_REQUEST_ID, requestId.left(kMaxClipboardRequestIdChars))
                                       .add(Constant::KEY_STATUS, false)
                                       .add(Constant::KEY_ERROR, error)
                                       .add(Constant::KEY_DATA, QString())
                                       .build());
    };
    if (requestId.isEmpty() || requestId.size() > kMaxClipboardRequestIdChars ||
        path.isEmpty() || path.size() > kMaxClipboardPathChars || offset < 0 ||
        length <= 0 || length > kMaxClipboardFileChunkBytes)
    {
        sendFailure(QStringLiteral("Invalid clipboard stream read request."));
        return;
    }

    const QString cleanedCtlPath = cleanPath(path);
    QString transferId;
    QString targetPath;
    qint64 totalBytes = 0;
    bool shouldEmitStarted = false;
    {
        QMutexLocker locker(&m_transferMutex);
        transferId = m_clipboardPromiseUploadSourceToTransferId.value(cleanedCtlPath);
        targetPath = m_clipboardPromiseUploadSourceToTargetPath.value(cleanedCtlPath);
        totalBytes = m_clipboardPromiseUploadSourceToTotalBytes.value(cleanedCtlPath, 0);
        if (!transferId.isEmpty() && !m_startedClipboardPromiseUploadSources.contains(cleanedCtlPath))
        {
            m_startedClipboardPromiseUploadSources.insert(cleanedCtlPath);
            shouldEmitStarted = true;
        }
    }
    if (transferId.isEmpty())
    {
        LOG_WARN("Rejected unauthorized clipboard stream read request: path={}, requestId={}",
                 cleanedCtlPath, requestId);
        sendFailure(QStringLiteral("Clipboard stream source is not authorized."));
        return;
    }
    if (shouldEmitStarted && !transferId.isEmpty())
    {
        emit fileTransferStarted(transferId, cleanedCtlPath, targetPath, tr("Upload"));
        emit fileTransferProgress(transferId, 0, totalBytes, 0, 1);
    }

    bool ok = false;
    QString errorMessage;
    const QByteArray data = readLocalFileChunk(path, offset, length, &ok, &errorMessage);
    if (!transferId.isEmpty())
    {
        const qint64 chunkEnd = offset <= (std::numeric_limits<qint64>::max)() - data.size()
                                    ? offset + data.size()
                                    : (std::numeric_limits<qint64>::max)();
        qint64 transferredBytes = chunkEnd;
        {
            QMutexLocker locker(&m_transferMutex);
            transferredBytes = qMax(m_clipboardPromiseUploadSourceToTransferredBytes.value(cleanedCtlPath, 0),
                                    chunkEnd);
            m_clipboardPromiseUploadSourceToTransferredBytes.insert(cleanedCtlPath, transferredBytes);
        }
        if (totalBytes > 0)
            transferredBytes = qMin(totalBytes, transferredBytes);
        emit fileTransferProgress(transferId, transferredBytes, totalBytes, transferredBytes >= totalBytes && totalBytes > 0 ? 1 : 0, 1);
        if (!ok || (totalBytes > 0 && transferredBytes >= totalBytes))
        {
            emit recvUploadFileRes(ok,
                                   targetPath.isEmpty() ? cleanedCtlPath : targetPath,
                                   ok ? QString() : QStringLiteral("Clipboard file upload failed."));
            QMutexLocker locker(&m_transferMutex);
            m_clipboardPromiseUploadSourceToTransferId.remove(cleanedCtlPath);
            m_clipboardPromiseUploadSourceToTargetPath.remove(cleanedCtlPath);
            m_clipboardPromiseUploadSourceToTotalBytes.remove(cleanedCtlPath);
            m_clipboardPromiseUploadSourceToTransferredBytes.remove(cleanedCtlPath);
            m_startedClipboardPromiseUploadSources.remove(cleanedCtlPath);
        }
    }

    QJsonObject response = JsonUtil::createObject()
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_STREAM_READ_RESULT)
                               .add(Constant::KEY_REQUEST_ID, requestId)
                               .add(Constant::KEY_STATUS, ok)
                               .add(Constant::KEY_ERROR, errorMessage)
                               .add(Constant::KEY_DATA, QString::fromLatin1(data.toBase64()))
                               .build();
    sendClipboardChannelObject(response);
}

void WebRtcCtl::handleClipboardStreamReadResult(const QJsonObject &object)
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

void WebRtcCtl::handleClipboardSnapshot(const QJsonObject &object)
{
    const QString requestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID,
                                                 uuidWithoutBraces());
    const QJsonObject payload = JsonUtil::getObject(object, Constant::KEY_PAYLOAD);
    const QStringList remotePaths = ClipboardUtil::payloadFilePaths(payload);
    if (remotePaths.isEmpty())
    {
        emit localClipboardPayloadReceived(payload);
        return;
    }

    const QString cacheRoot = ClipboardUtil::cacheRoot(requestId);
    {
        QMutexLocker locker(&m_transferMutex);
        m_clipboardCacheRoots.insert(cacheRoot);
    }
    const QPointer<WebRtcCtl> guard(this);
    const auto callbackLifetime = m_callbackLifetime;

    const auto startCachedFallback = [guard, callbackLifetime, requestId, payload, remotePaths, cacheRoot]() {
        auto permit = callbackLifetime->tryEnter();
        if (!permit)
            return;
        if (!guard || guard->m_shutdownDone)
            return;

        QStringList localPaths;
        QSet<QString> expectedTargets;
        QSet<QString> usedLocalPaths;
        QVector<QJsonObject> downloadRequests;
        for (const QString &remotePath : remotePaths)
        {
            QFileInfo info(remotePath);
            const QString localPath = uniqueChildPath(cacheRoot, info.fileName(), &usedLocalPaths);
            localPaths.append(localPath);
            expectedTargets.insert(localPath);
            downloadRequests.append(fileTransferMessage(Constant::TYPE_FILE_DOWNLOAD,
                                                        localPath,
                                                        remotePath,
                                                        requestId));
        }

        if (guard->m_pendingClipboardDownloadPayloads.size() >= kMaxClipboardPayloadRequests)
        {
            LOG_WARN("Dropping stale pending clipboard download payloads: count={}",
                     guard->m_pendingClipboardDownloadPayloads.size());
            guard->m_pendingClipboardDownloadPayloads.clear();
            guard->m_pendingClipboardDownloadTargets.clear();
        }
        guard->m_pendingClipboardDownloadPayloads.insert(
            requestId, payloadWithMappedFilePaths(payload, localPaths));
        guard->m_pendingClipboardDownloadTargets.insert(requestId, expectedTargets);
        for (const QJsonObject &request : downloadRequests)
        {
            emit guard->fileTransferStarted(JsonUtil::getString(request, Constant::KEY_TRANSFER_ID),
                                            JsonUtil::getString(request, Constant::KEY_PATH_CLI),
                                            JsonUtil::getString(request, Constant::KEY_PATH_CTL),
                                            QCoreApplication::translate("WebRtcCtl", "Download"));
            guard->fileTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(request).toStdString()));
        }
    };

    const QList<ClipboardFilePromiseItem> promiseItems = filePromiseItemsFromPayload(payload, nullptr);
    const bool hasAdditionalFormats = ClipboardUtil::payloadHasImage(payload) ||
                                      !JsonUtil::getString(payload, Constant::KEY_TEXT).isEmpty();
    if (!promiseItems.isEmpty() && ClipboardFilePromise::isSupported() && !hasAdditionalFormats)
    {
        ClipboardFilePromise::installAsync(
            this,
            promiseItems,
            cacheRoot,
            [guard, callbackLifetime, requestId](const QString &remotePath, qint64 offset, qint64 maxBytes, bool *ok, QString *error) {
                auto permit = callbackLifetime->tryEnter();
                if (!permit)
                {
                    if (ok)
                        *ok = false;
                    if (error)
                        *error = QCoreApplication::translate("WebRtcCtl", "Remote control session is closed.");
                    return QByteArray();
                }
                if (!guard || guard->m_shutdownStarted.load())
                {
                    if (ok)
                        *ok = false;
                    if (error)
                        *error = QCoreApplication::translate("WebRtcCtl", "Remote control session is closed.");
                    return QByteArray();
                }
                return guard->readRemoteClipboardFileChunk(requestId, remotePath, offset, maxBytes, ok, error);
            },
            [guard, callbackLifetime, promiseItems, requestId, startCachedFallback](bool installed, const QString &errorMessage) {
                auto permit = callbackLifetime->tryEnter();
                if (!permit)
                    return;
                if (!guard || guard->m_shutdownDone)
                    return;
                if (!installed)
                {
                    LOG_WARN("Native clipboard file promise unavailable, falling back to cached clipboard files: {}", errorMessage);
                    startCachedFallback();
                    return;
                }

                for (const ClipboardFilePromiseItem &item : promiseItems)
                {
                    if (item.isDirectory || item.sourcePath.isEmpty())
                        continue;
                    const QString remotePath = cleanPath(item.sourcePath);
                    const QString transferId = QStringLiteral("%1-%2").arg(requestId, uuidWithoutBraces());
                    QMutexLocker locker(&guard->m_transferMutex);
                    guard->m_clipboardPromiseRemotePathToTransferId.insert(remotePath, transferId);
                    guard->m_clipboardPromiseRemotePathToTotalBytes.insert(remotePath, qMax<qint64>(0, item.size));
                    guard->m_clipboardPromiseRemotePathToTransferredBytes.insert(remotePath, 0);
                }
            });
        return;
    }

    startCachedFallback();
}

void WebRtcCtl::noteClipboardDownloadResult(const QString &path, bool status)
{
    const QString cleanedPath = cleanPath(path);
    {
        QMutexLocker locker(&m_transferMutex);
        if (m_pendingClipboardPromiseTargets.contains(cleanedPath))
        {
            m_clipboardPromiseDownloadResults.insert(cleanedPath, status);
            m_clipboardPromiseDownloadWait.wakeAll();
            return;
        }
    }
    const QList<QString> requestIds = m_pendingClipboardDownloadTargets.keys();
    for (const QString &requestId : requestIds)
    {
        QSet<QString> targets = m_pendingClipboardDownloadTargets.value(requestId);
        if (!targets.contains(cleanedPath))
            continue;

        if (!status)
        {
            LOG_WARN("Clipboard download failed: requestId={}, path={}", requestId, cleanedPath);
            m_pendingClipboardDownloadTargets.remove(requestId);
            m_pendingClipboardDownloadPayloads.remove(requestId);
            return;
        }

        targets.remove(cleanedPath);
        if (targets.isEmpty())
        {
            const QJsonObject payload = m_pendingClipboardDownloadPayloads.take(requestId);
            m_pendingClipboardDownloadTargets.remove(requestId);
            emit localClipboardPayloadReceived(payload);
        }
        else
        {
            m_pendingClipboardDownloadTargets.insert(requestId, targets);
        }
        return;
    }
}

bool WebRtcCtl::downloadClipboardPromisedFile(const QString &baseRequestId,
                                              const QString &remotePath,
                                              const QString &localPath,
                                              QString *errorMessage)
{
    const QString cleanedLocalPath = cleanPath(localPath);
    if (remotePath.isEmpty() || cleanedLocalPath.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCtl", "Clipboard promise file path is empty.");
        return false;
    }

    if (QFileInfo(cleanedLocalPath).exists())
        return true;

    const QString transferId = QStringLiteral("%1-%2").arg(baseRequestId, uuidWithoutBraces());
    {
        QMutexLocker locker(&m_transferMutex);
        m_pendingClipboardPromiseTargets.insert(cleanedLocalPath);
        m_clipboardPromiseDownloadResults.remove(cleanedLocalPath);
    }

    const QJsonObject request = fileTransferMessage(Constant::TYPE_FILE_DOWNLOAD,
                                                    cleanedLocalPath,
                                                    remotePath,
                                                    transferId);
    emit fileTransferStarted(transferId, remotePath, cleanedLocalPath, tr("Download"));
    const rtc::message_variant requestMessage(JsonUtil::toCompactBytes(request).toStdString());
    bool queued = true;
    if (QThread::currentThread() == thread())
    {
        fileTextChannelSendMsg(requestMessage);
    }
    else
    {
        queued = QMetaObject::invokeMethod(this,
                                           "fileTextChannelSendMsg",
                                           Qt::QueuedConnection,
                                           Q_ARG(rtc::message_variant, requestMessage));
    }
    if (!queued)
    {
        QMutexLocker locker(&m_transferMutex);
        m_pendingClipboardPromiseTargets.remove(cleanedLocalPath);
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCtl", "Failed to queue clipboard promise download request.");
        return false;
    }

    bool status = false;
    bool hasResult = false;
    {
        QMutexLocker locker(&m_transferMutex);
        const qint64 deadlineMs = QDateTime::currentMSecsSinceEpoch() + 15000;
        while (!m_clipboardPromiseDownloadResults.contains(cleanedLocalPath) && !m_shutdownStarted.load())
        {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (nowMs >= deadlineMs)
                break;
            m_clipboardPromiseDownloadWait.wait(&m_transferMutex, static_cast<unsigned long>(qMin<qint64>(1000, deadlineMs - nowMs)));
        }

        hasResult = m_clipboardPromiseDownloadResults.contains(cleanedLocalPath);
        if (hasResult)
            status = m_clipboardPromiseDownloadResults.take(cleanedLocalPath);
        m_pendingClipboardPromiseTargets.remove(cleanedLocalPath);
    }

    if (!hasResult || !status || !QFileInfo(cleanedLocalPath).exists())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCtl", "Failed to download clipboard promise file.");
        return false;
    }
    return true;
}

QByteArray WebRtcCtl::readRemoteClipboardFileChunk(const QString &baseRequestId,
                                                   const QString &remotePath,
                                                   qint64 offset,
                                                   qint64 maxBytes,
                                                   bool *ok,
                                                   QString *errorMessage)
{
    if (ok)
        *ok = false;
    if (remotePath.isEmpty() || offset < 0 || maxBytes <= 0)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("WebRtcCtl", "Invalid clipboard stream read request.");
        return QByteArray();
    }

    constexpr qint64 kMaxChunkBytes = 64 * 1024;
    QByteArray result;
    qint64 currentOffset = offset;
    qint64 remaining = maxBytes;

    while (remaining > 0)
    {
        const qint64 requestBytes = (std::min<qint64>)(remaining, kMaxChunkBytes);
        const QString requestId = QStringLiteral("%1-%2").arg(baseRequestId, uuidWithoutBraces());
        const QJsonObject request = JsonUtil::createObject()
                                        .add(Constant::KEY_MSGTYPE, Constant::TYPE_CLIPBOARD_STREAM_READ_REQUEST)
                                        .add(Constant::KEY_REQUEST_ID, requestId)
                                        .add(Constant::KEY_PATH, remotePath)
                                        .add(Constant::KEY_OFFSET, static_cast<double>(currentOffset))
                                        .add(Constant::KEY_LENGTH, static_cast<double>(requestBytes))
                                        .build();

        {
            QMutexLocker locker(&m_transferMutex);
            if (m_pendingClipboardStreamRequests.size() >= kMaxClipboardStreamReadRequests)
            {
                if (errorMessage)
                    *errorMessage = QCoreApplication::translate(
                        "WebRtcCtl", "Too many clipboard stream reads are pending.");
                return result;
            }
            m_pendingClipboardStreamRequests.insert(requestId, requestBytes);
        }

        bool queued = true;
        if (QThread::currentThread() == thread())
        {
            sendClipboardChannelObject(request);
        }
        else
        {
            queued = QMetaObject::invokeMethod(this,
                                               "sendClipboardChannelObject",
                                               Qt::QueuedConnection,
                                               Q_ARG(QJsonObject, request));
        }
        if (!queued)
        {
            QMutexLocker locker(&m_transferMutex);
            m_pendingClipboardStreamRequests.remove(requestId);
            if (errorMessage)
                *errorMessage = QCoreApplication::translate("WebRtcCtl", "Failed to queue clipboard stream read request.");
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
        const QString cleanedRemotePath = cleanPath(remotePath);
        QString transferId;
        qint64 totalBytes = 0;
        bool shouldEmitStarted = false;
        {
            QMutexLocker locker(&m_transferMutex);
            transferId = m_clipboardPromiseRemotePathToTransferId.value(cleanedRemotePath);
            totalBytes = m_clipboardPromiseRemotePathToTotalBytes.value(cleanedRemotePath, 0);
            if (!transferId.isEmpty() && !m_startedClipboardPromiseRemotePaths.contains(cleanedRemotePath))
            {
                m_startedClipboardPromiseRemotePaths.insert(cleanedRemotePath);
                shouldEmitStarted = true;
            }
        }
        if (!transferId.isEmpty())
        {
            if (shouldEmitStarted)
            {
                emit fileTransferStarted(transferId, cleanedRemotePath, QFileInfo(cleanedRemotePath).fileName(), tr("Download"));
                emit fileTransferProgress(transferId, 0, totalBytes, 0, 1);
            }
            qint64 transferredBytes = currentOffset;
            {
                QMutexLocker locker(&m_transferMutex);
                transferredBytes = qMax(m_clipboardPromiseRemotePathToTransferredBytes.value(cleanedRemotePath, 0),
                                        currentOffset);
                m_clipboardPromiseRemotePathToTransferredBytes.insert(cleanedRemotePath, transferredBytes);
            }
            if (totalBytes > 0)
                transferredBytes = qMin(totalBytes, transferredBytes);
            emit fileTransferProgress(transferId, transferredBytes, totalBytes, transferredBytes >= totalBytes && totalBytes > 0 ? 1 : 0, 1);
            if (totalBytes > 0 && transferredBytes >= totalBytes)
            {
                emit recvDownloadFile(true, cleanedRemotePath);
                QMutexLocker locker(&m_transferMutex);
                m_clipboardPromiseRemotePathToTransferId.remove(cleanedRemotePath);
                m_clipboardPromiseRemotePathToTotalBytes.remove(cleanedRemotePath);
                m_clipboardPromiseRemotePathToTransferredBytes.remove(cleanedRemotePath);
                m_startedClipboardPromiseRemotePaths.remove(cleanedRemotePath);
            }
        }
        if (chunk.size() < requestBytes)
            break;
    }

    if (ok)
        *ok = true;
    return result;
}

void WebRtcCtl::sendRemotePasteShortcut()
{
    const int modifierKey = m_remoteOsName == QStringLiteral("macos") ? 0x5B : 0x11;
    const QList<int> keys{modifierKey, 0x56};
    for (int key : keys)
    {
        QJsonObject down = JsonUtil::createObject()
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_KEYBOARD)
                               .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                               .add(Constant::KEY_RECEIVER, m_remoteId)
                               .add(Constant::KEY_RECEIVER_PWD, m_remotePwdMd5)
                               .add(Constant::KEY_KEY, key)
                               .add(Constant::KEY_DWFLAGS, Constant::KEY_DOWN)
                               .build();
        inputChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(down).toStdString()));
    }
    for (int i = keys.size() - 1; i >= 0; --i)
    {
        QJsonObject up = JsonUtil::createObject()
                             .add(Constant::KEY_MSGTYPE, Constant::TYPE_KEYBOARD)
                             .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                             .add(Constant::KEY_RECEIVER, m_remoteId)
                             .add(Constant::KEY_RECEIVER_PWD, m_remotePwdMd5)
                             .add(Constant::KEY_KEY, keys.at(i))
                             .add(Constant::KEY_DWFLAGS, Constant::KEY_UP)
                             .build();
        inputChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(up).toStdString()));
    }
}
