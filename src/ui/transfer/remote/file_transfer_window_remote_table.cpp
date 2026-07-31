#include "ui/transfer/file_transfer_window.h"

#include "common/constant.h"
#include "ui_file_transfer_window.h"
#include "util/text/convert_util.h"
#include "util/json/json_util.h"

#include <QTableWidget>


void FileTransferWindow::populateRemoteFiles()
{
    ui->remoteTable->setRowCount(remoteFiles.size() + 1);
    QTableWidgetItem *item_0_0 = ui->remoteTable->takeItem(0, 0);
    if (item_0_0)
        delete item_0_0;

    QTableWidgetItem *parentDir = new QTableWidgetItem("..");
    parentDir->setIcon(dirIcon);
    ui->remoteTable->setItem(0, 0, parentDir);

    for (int i = 0; i < remoteFiles.size(); i++)
    {
        int row = i + 1;
        QTableWidgetItem *item_0 = ui->remoteTable->takeItem(row, 0);
        if (item_0)
            delete item_0;
        QTableWidgetItem *item_1 = ui->remoteTable->takeItem(row, 1);
        if (item_1)
            delete item_1;
        QTableWidgetItem *item_2 = ui->remoteTable->takeItem(row, 2);
        if (item_2)
            delete item_2;
        QTableWidgetItem *item_3 = ui->remoteTable->takeItem(row, 3);
        if (item_3)
            delete item_3;

        QJsonObject obj = remoteFiles.at(i).toObject();
        QString fileName = JsonUtil::getString(obj, Constant::KEY_NAME);
        if (!fileName.isEmpty())
        {
            QTableWidgetItem *nameItem = new QTableWidgetItem(fileName);
            bool isDir = JsonUtil::getBool(obj, Constant::KEY_IS_DIR);
            nameItem->setData(Qt::UserRole, isDir);
            nameItem->setData(Qt::UserRole + 2, obj.contains(Constant::KEY_FILE_EXECUTABLE));
            nameItem->setData(Qt::UserRole + 1, JsonUtil::getBool(obj, Constant::KEY_FILE_EXECUTABLE, false));
            nameItem->setIcon(isDir ? dirIcon : fileIcon);
            ui->remoteTable->setItem(row, 0, nameItem);
        }

        qint64 fileSize = JsonUtil::getInt64(obj, Constant::KEY_FILE_SIZE);
        if (fileSize > 0)
        {
            QString fileSizeStr = ConvertUtil::formatFileSize(fileSize);
            ui->remoteTable->setItem(row, 1, new QTableWidgetItem(fileSizeStr));
        }

        QString lastModTime = JsonUtil::getString(obj, Constant::KEY_FILE_LAST_MOD_TIME);
        if (!lastModTime.isEmpty())
        {
            ui->remoteTable->setItem(row, 2, new QTableWidgetItem(lastModTime));
        }

        QString fileSuffix = JsonUtil::getString(obj, Constant::KEY_FILE_SUFFIX);
        if (!fileSuffix.isEmpty())
        {
            ui->remoteTable->setItem(row, 3, new QTableWidgetItem(fileSuffix));
        }
    }
}
