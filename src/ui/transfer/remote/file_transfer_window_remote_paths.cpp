#include "ui/transfer/file_transfer_window.h"

#include "common/constant.h"
#include "ui_file_transfer_window.h"
#include "util/json/json_util.h"

#include <QDir>
#include <QTableWidget>
#include <QUuid>


void FileTransferWindow::on_remotePathCombo_textActivated(const QString &path)
{
    requestRemoteFileList(path);
}


void FileTransferWindow::on_remoteTable_cellDoubleClicked(int row, int column)
{
    Q_UNUSED(column);

    const QTableWidgetItem *item = ui->remoteTable->item(row, 0);
    if (!item)
        return;
    QString path = item->text();
    if (!connected)
    {
        return;
    }

    QString filePath;
    if (path == "..")
    {
        filePath = currentRemotePath.mid(0, currentRemotePath.lastIndexOf('/') + 1);
        if (filePath.isEmpty())
        {
            filePath = Constant::FOLDER_HOME;
        }
    }
    else
    {
        if (!item->data(Qt::UserRole).toBool())
            return;
        filePath = QDir::cleanPath(currentRemotePath + '/' + path);
    }

    requestRemoteFileList(filePath);
}


void FileTransferWindow::requestRemoteFileList(const QString &path)
{
    if (isClosing())
        return;

    const QString targetPath = path.isEmpty() ? Constant::FOLDER_HOME : path;
    m_pendingFileListRequestId = QUuid::createUuid().toString();
    m_pendingFileListRequestId.remove(QLatin1Char('{'));
    m_pendingFileListRequestId.remove(QLatin1Char('}'));
    m_pendingFileListRequestPath = targetPath;
    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_LIST)
                          .add(Constant::KEY_PATH, targetPath)
                          .add(Constant::KEY_REQUEST_ID, m_pendingFileListRequestId)
                          .build();

    QByteArray msg = JsonUtil::toCompactBytes(obj);
    LOG_DEBUG("Sending file list request for path={}", targetPath);

    rtc::message_variant msgStr(msg.toStdString());
    emit fileTextChannelSendMsg(msgStr);
}


void FileTransferWindow::updateRemotePathCombo()
{
    ui->remotePathCombo->clear();

    if (!currentRemotePath.isEmpty())
    {
        ui->remotePathCombo->addItem(currentRemotePath);
        ui->remotePathCombo->setCurrentText(currentRemotePath);
    }
}
