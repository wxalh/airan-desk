#include "webrtc/ctl/webrtc_ctl.h"

#include "common/constant.h"
#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"


bool WebRtcCtl::handleFileTransferTextChannelObject(const QJsonObject &object, const QString &msgType)
{
    if (msgType == Constant::TYPE_UPLOAD_FILE_RES)
    {
        const QString cliPath = JsonUtil::getString(object, Constant::KEY_PATH_CLI);
        const bool status = JsonUtil::getBool(object, "status");
        const QString errorMessage = status ? QString() : JsonUtil::getString(object, "message", tr("Upload failed"));
        LOG_INFO("Upload response: {} - {}, error={}", cliPath, status, errorMessage);
        noteClipboardUploadResult(cliPath, status);
        emit recvUploadFileRes(status, cliPath, errorMessage);
        return true;
    }
    if (msgType == Constant::TYPE_FILE_LIST)
    {
        emit recvGetFileList(object);
        return true;
    }
    if (msgType == Constant::TYPE_FILE_TRANSFER_PROGRESS)
    {
        emitTransferProgress(JsonUtil::getString(object, Constant::KEY_TRANSFER_ID),
                             JsonUtil::getInt64(object, Constant::KEY_TRANSFER_BYTES),
                             JsonUtil::getInt64(object, Constant::KEY_TRANSFER_TOTAL_BYTES),
                             JsonUtil::getInt(object, Constant::KEY_TRANSFER_FILE_COUNT, 0),
                             JsonUtil::getInt(object, Constant::KEY_TRANSFER_TOTAL_FILES, 0));
        return true;
    }
    if (msgType == Constant::TYPE_FILE_TRANSFER_CANCEL)
    {
        const QString transferId = JsonUtil::getString(object, Constant::KEY_TRANSFER_ID);
        markTransferCancelled(transferId);
        if (m_filePacketUtil)
            m_filePacketUtil->cancelTransfer(transferId);
        return true;
    }
    if (msgType == Constant::TYPE_FILE_DOWNLOAD)
    {
        if (object.contains(Constant::KEY_ERROR))
        {
            QString ctlPath = JsonUtil::getString(object, Constant::KEY_PATH_CTL,
                                                  JsonUtil::getString(object, Constant::KEY_PATH));
            LOG_WARN("Download failed remotely: path={}, error={}",
                     ctlPath,
                     JsonUtil::getString(object, Constant::KEY_ERROR));
            noteClipboardDownloadResult(ctlPath, false);
            emit recvDownloadFile(false, ctlPath);
            return true;
        }
        if (object.contains("directoryEnd"))
        {
            QString ctlPath = JsonUtil::getString(object, Constant::KEY_PATH_CTL);
            bool status = JsonUtil::getBool(object, "status", true);
            noteClipboardDownloadResult(ctlPath, status);
            emit recvDownloadFile(status, ctlPath);
            return true;
        }
        return true;
    }
    if (msgType == Constant::TYPE_RUN_FILE)
    {
        LOG_INFO("Run file response: path={}, status={}, error={}",
                 JsonUtil::getString(object, Constant::KEY_PATH_CLI),
                 JsonUtil::getBool(object, Constant::KEY_STATUS, false),
                 JsonUtil::getString(object, Constant::KEY_ERROR));
        return true;
    }
    if (msgType == Constant::TYPE_FILE_DELETE)
    {
        const QString path = JsonUtil::getString(object, Constant::KEY_PATH_CLI);
        const bool status = JsonUtil::getBool(object, Constant::KEY_STATUS, false);
        const QString errorMessage = JsonUtil::getString(object, Constant::KEY_ERROR);
        LOG_INFO("Delete file response: path={}, status={}, error={}",
                 path,
                 status,
                 errorMessage);
        emit recvDeleteFileRes(status, path, errorMessage);
        return true;
    }
    if (msgType == Constant::TYPE_FILE_RENAME)
    {
        emit recvRenameFileRes(JsonUtil::getBool(object, Constant::KEY_STATUS, false),
                               JsonUtil::getString(object, Constant::KEY_PATH_CLI),
                               JsonUtil::getString(object, Constant::KEY_ERROR));
        return true;
    }
    if (msgType == Constant::TYPE_FILE_CREATE)
    {
        emit recvCreateFileRes(JsonUtil::getBool(object, Constant::KEY_STATUS, false),
                               JsonUtil::getString(object, Constant::KEY_PATH_CLI),
                               JsonUtil::getBool(object, Constant::KEY_IS_DIR, false),
                               JsonUtil::getString(object, Constant::KEY_ERROR));
        return true;
    }
    return false;
}
