#include "ui/transfer/file_transfer_window.h"
#include "ui_file_transfer_window.h"
#include "util/text/convert_util.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTableWidget>


void FileTransferWindow::populateLocalFiles()
{
    currentLocalDir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    currentLocalDir.setSorting(QDir::Name | QDir::DirsFirst);
    QFileInfoList list = currentLocalDir.entryInfoList();

    int listSize = list.size() + 1;
    ui->localTable->setRowCount(listSize);

    QTableWidgetItem *item_0_0 = ui->localTable->takeItem(0, 0);
    if (item_0_0)
        delete item_0_0;

    QTableWidgetItem *parentDir = new QTableWidgetItem("..");
    parentDir->setIcon(dirIcon);
    ui->localTable->setItem(0, 0, parentDir);
    for (int i = 0; i < list.size(); i++)
    {
        int row = i + 1;
        const QFileInfo &info = list.at(i);
        QTableWidgetItem *item_0 = ui->localTable->takeItem(row, 0);
        if (item_0)
            delete item_0;
        QTableWidgetItem *item_1 = ui->localTable->takeItem(row, 1);
        if (item_1)
            delete item_1;
        QTableWidgetItem *item_2 = ui->localTable->takeItem(row, 2);
        if (item_2)
            delete item_2;
        QTableWidgetItem *item_3 = ui->localTable->takeItem(row, 3);
        if (item_3)
            delete item_3;

        QDateTime lastModTime = info.lastModified();
        QString lastModTimeStr = lastModTime.toString("yyyy-MM-dd hh:mm:ss");
        QTableWidgetItem *lastModTimeItem = new QTableWidgetItem(lastModTimeStr);
        ui->localTable->setItem(row, 2, lastModTimeItem);

        QString name = info.fileName();
        QTableWidgetItem *nameItem = new QTableWidgetItem(name);
        ui->localTable->setItem(row, 0, nameItem);

        if (info.isDir())
        {
            nameItem->setIcon(dirIcon);
        }
        else
        {
            nameItem->setIcon(fileIcon);
            qint64 size = info.size();
            QString sizeStr = ConvertUtil::formatFileSize(size);
            QTableWidgetItem *sizeItem = new QTableWidgetItem(sizeStr);
            ui->localTable->setItem(row, 1, sizeItem);

            QString fileType = info.suffix();
            QTableWidgetItem *fileTypeItem = new QTableWidgetItem(fileType);
            ui->localTable->setItem(row, 3, fileTypeItem);
        }
    }
}

void FileTransferWindow::on_localPathCombo_textActivated(const QString &path)
{
    bool status = true;

    if (QDir::isAbsolutePath(path))
    {
        QDir newDir(path);
        if (newDir.exists())
        {
            currentLocalDir = newDir;
        }
        else
        {
            status = false;
        }
    }
    else
    {
        status = currentLocalDir.cd(path);
    }

    if (status)
    {
        populateLocalFiles();
        updateLocalPathCombo();
    }
}

void FileTransferWindow::on_localTable_cellDoubleClicked(int row, int column)
{
    const QTableWidgetItem *item = ui->localTable->item(row, 0);
    if (!item)
        return;
    QString filePath = item->text();

    bool status = false;
    if (filePath == "..")
    {
        status = currentLocalDir.cdUp();
    }
    else
    {
        status = currentLocalDir.cd(filePath);
    }

    if (status)
    {
        populateLocalFiles();
        updateLocalPathCombo();
    }
}


void FileTransferWindow::updateLocalPathCombo()
{
    ui->localPathCombo->clear();

    QString currentPath = currentLocalDir.absolutePath();
    ui->localPathCombo->addItem(currentPath);
    QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &volume : volumes)
    {
        if (volume.isValid() && volume.isReady())
        {
            ui->localPathCombo->addItem(volume.rootPath());
        }
    }

    ui->localPathCombo->setCurrentText(currentPath);
}
