#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "util/json/json_util.h"
#include "security/audit_session.h"

#include <QFileInfo>

void WebRtcCli::sendFileErrorResponse(const QString &filePath, const QString &error)
{
    QJsonObject errorMsg = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DOWNLOAD)
                               .add(Constant::KEY_PATH, filePath)
                               .add(Constant::KEY_PATH_CLI, filePath)
                               .add(Constant::KEY_ERROR, error)
                               .build();

    sendFileTextChannelMessage(errorMsg);
}


void WebRtcCli::sendUploadResponse(const QString &fileName, bool success, const QString &message)
{
    QJsonObject responseMsg = JsonUtil::createObject()
                                  .add(Constant::KEY_MSGTYPE, Constant::TYPE_UPLOAD_FILE_RES)
                                  .add(Constant::KEY_PATH_CLI, fileName)
                                  .add("status", success)
                                  .add("message", message)
                                  .build();

    sendFileTextChannelMessage(responseMsg);
}


void WebRtcCli::handleFileReceived(bool status, const QString &tempPath, const QString &errorMessage)
{
    LOG_TRACE("Received complete file from FilePacketUtil, status: {}, tempPath: {}", status, tempPath);

    noteClipboardPromisedFileResult(tempPath, status);
    if (m_auditSession)
    {
        const QFileInfo info(tempPath);
        m_auditSession->recordFileTransfer(tempPath,
                                           status && info.exists() ? info.size() : 0,
                                           QStringLiteral("upload"),
                                           status ? AuditSession::sha256ForFile(tempPath) : QString(),
                                           status);
    }
    sendUploadResponse(tempPath,
                       status,
                       status ? tr("Upload successful")
                              : (errorMessage.isEmpty() ? tr("Upload failed") : errorMessage));
}
