#include "ui/transfer/file_transfer_window.h"
#include "common/constant.h"
#include "ui_file_transfer_window.h"
#include "util/json/json_util.h"

#include <QDir>
#include <QMessageBox>


namespace
{
QString normalizedRemoteFileListPath(const QString &path)
{
    QString normalized = path.trimmed();
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    normalized = QDir::cleanPath(normalized);
    const bool windowsStyle = normalized.size() >= 2 && normalized.at(1) == QLatin1Char(':') &&
                              normalized.at(0).isLetter();
    const bool uncStyle = normalized.startsWith(QStringLiteral("//"));
    if (windowsStyle || uncStyle)
        normalized = normalized.toLower();
    return normalized;
}
}


void FileTransferWindow::recvGetFileList(const QJsonObject &object)
{
    const QString responseRequestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID).trimmed();
    const QString responsePath = JsonUtil::getString(object, Constant::KEY_PATH);
    if (m_pendingFileListRequestId.isEmpty())
    {
        LOG_DEBUG("Ignoring file-list response without an active request: requestId={}, path={}",
                  responseRequestId, responsePath);
        return;
    }

    const bool requestIdMatches = responseRequestId == m_pendingFileListRequestId;
    const bool legacyResponseMatches = responseRequestId.isEmpty() && !responsePath.isEmpty() &&
                                       normalizedRemoteFileListPath(responsePath) ==
                                           normalizedRemoteFileListPath(m_pendingFileListRequestPath);
    const bool legacyInitialHomeResponse = responseRequestId.isEmpty() &&
                                           m_pendingFileListRequestPath == Constant::FOLDER_HOME &&
                                           currentRemotePath.isEmpty() && !responsePath.isEmpty();
    if (!requestIdMatches && !legacyResponseMatches && !legacyInitialHomeResponse)
    {
        LOG_DEBUG("Ignoring stale file-list response: requestId={}, expected={}, path={}, expectedPath={}",
                  responseRequestId,
                  m_pendingFileListRequestId,
                  responsePath,
                  m_pendingFileListRequestPath);
        return;
    }
    m_pendingFileListRequestId.clear();
    m_pendingFileListRequestPath.clear();

    LOG_DEBUG("Received file list response for path={}, items={}",
              responsePath,
              object.value(Constant::KEY_FOLDER_FILES).toArray().size());

    if (object.contains(Constant::KEY_STATUS) &&
        !JsonUtil::getBool(object, Constant::KEY_STATUS, true))
    {
        connected = true;
        QMessageBox::warning(this,
                             tr("File list failed"),
                             tr("Failed to list remote directory:\n%1")
                                 .arg(JsonUtil::getString(object, Constant::KEY_ERROR, tr("Unknown"))));
        return;
    }

    if (!connected)
    {
        connected = true;
    }

    ui->remotePathCombo->clear();
    if (object.contains(Constant::KEY_FOLDER_FILES))
    {
        remoteFiles = object.value(Constant::KEY_FOLDER_FILES).toArray();
        populateRemoteFiles();

        if (object.contains(Constant::KEY_PATH))
        {
            QString receivedPath = responsePath;
            if (!receivedPath.isEmpty())
            {
                currentRemotePath = receivedPath;
                updateRemotePathCombo();
            }
        }
    }

    if (object.contains(Constant::KEY_FOLDER_MOUNTED))
    {
        QJsonArray mountedList = object.value(Constant::KEY_FOLDER_MOUNTED).toArray();
        for (const QJsonValue &value : mountedList)
        {
            if (value.isString())
            {
                QString mountPath = value.toString();
                if (mountPath != currentRemotePath)
                {
                    ui->remotePathCombo->addItem(mountPath);
                }
            }
        }
        ui->remotePathCombo->setCurrentText(currentRemotePath);
    }
    else
    {
        updateRemotePathCombo();
    }
}
