#include "ui/transfer/file_transfer_window.h"
#include "common/constant.h"
#include "ui_file_transfer_window.h"
#include "util/json/json_util.h"


void FileTransferWindow::recvGetFileList(const QJsonObject &object)
{
    LOG_DEBUG("Received file list response for path={}, items={}",
              JsonUtil::getString(object, Constant::KEY_PATH),
              object.value(Constant::KEY_FOLDER_FILES).toArray().size());

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
            QString receivedPath = JsonUtil::getString(object, Constant::KEY_PATH);
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
