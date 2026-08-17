#include "webrtc/ctl/webrtc_ctl.h"

#include "util/file/file_packet_util.h"
#include "util/text/convert_util.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

namespace
{
constexpr int kMaxPendingUploadRequests = 1024;
constexpr int kMaxTransferPathChars = 32 * 1024;
constexpr int kMaxTransferIdChars = 256;
}


void WebRtcCtl::uploadFile2CLI(const QString &ctlPath, const QString &cliPath, const QString &transferId)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    LOG_WARN("uploadFile2CLI called: {} -> {}", ctlPath, cliPath);
    if (ctlPath.size() > kMaxTransferPathChars || cliPath.size() > kMaxTransferPathChars ||
        transferId.size() > kMaxTransferIdChars ||
        m_pendingUploads.size() >= kMaxPendingUploadRequests)
    {
        LOG_ERROR("Rejected upload request because the pending queue or fields exceed limits: queued={}, ctlChars={}, cliChars={}, transferIdChars={}",
                  m_pendingUploads.size(), ctlPath.size(), cliPath.size(), transferId.size());
        emit recvUploadFileRes(false, cliPath, tr("Too many pending upload requests."));
        return;
    }
    m_pendingUploads.enqueue({ctlPath, cliPath, transferId});
    processUploadQueue();
}


void WebRtcCtl::processUploadQueue()
{
    if (m_uploadQueueActive)
        return;

    m_uploadQueueActive = true;
    processNextUpload();
}


void WebRtcCtl::processNextUpload()
{
    if (m_shutdownRequested.load())
    {
        m_uploadQueueActive = false;
        return;
    }

    while (!m_pendingUploads.isEmpty() && !m_shutdownRequested.load())
    {
        const PendingUpload upload = m_pendingUploads.dequeue();
        if (isTransferCancelled(upload.transferId))
            continue;

        const QString &ctlPath = upload.ctlPath;
        const QString &cliPath = upload.cliPath;
        const QString &transferId = upload.transferId;

        if (!m_fileChannel || !m_fileChannel->isOpen())
        {
            LOG_ERROR("File channel not available");
            emit recvUploadFileRes(false, ctlPath, tr("File channel is not available."));
            continue;
        }

        QFileInfo fileInfo(ctlPath);
        if (!fileInfo.exists())
        {
            LOG_ERROR("File does not exist: {}", ctlPath);
            emit recvUploadFileRes(false, ctlPath, tr("Source file does not exist."));
            continue;
        }

        if (fileInfo.isFile())
        {
            emit fileTransferStarted(transferId, ctlPath, cliPath, tr("Upload"));
            uploadSingleFileAsync(ctlPath, cliPath, transferId, 0, fileInfo.size(), 1, 1);
            return;
        }
        else if (fileInfo.isDir())
        {
            emit fileTransferStarted(transferId, ctlPath, cliPath, tr("Upload"));
            uploadDirectory(ctlPath, cliPath, transferId);
            return;
        }
        else
        {
            LOG_ERROR("Unknown file type: {}", ctlPath);
            emit recvUploadFileRes(false, ctlPath, tr("Unknown source file type."));
        }
    }
    m_uploadQueueActive = false;
}
