#include "webrtc/ctl/webrtc_ctl.h"

#include <QFileInfo>


void WebRtcCtl::uploadFile2CLI(const QString &ctlPath, const QString &cliPath, const QString &transferId)
{
    LOG_WARN("uploadFile2CLI called: {} -> {}", ctlPath, cliPath);
    m_pendingUploads.enqueue({ctlPath, cliPath, transferId});
    processUploadQueue();
}


void WebRtcCtl::processUploadQueue()
{
    if (m_uploadQueueActive)
        return;

    m_uploadQueueActive = true;
    while (!m_pendingUploads.isEmpty())
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
            uploadSingleFile(ctlPath, cliPath, transferId, 0, fileInfo.size(), 1, 1);
        }
        else if (fileInfo.isDir())
        {
            emit fileTransferStarted(transferId, ctlPath, cliPath, tr("Upload"));
            uploadDirectory(ctlPath, cliPath, transferId);
        }
        else
        {
            LOG_ERROR("Unknown file type: {}", ctlPath);
            emit recvUploadFileRes(false, ctlPath, tr("Unknown source file type."));
        }
    }
    m_uploadQueueActive = false;
}
