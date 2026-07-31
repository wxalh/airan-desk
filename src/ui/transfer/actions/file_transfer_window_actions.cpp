#include "ui/transfer/file_transfer_window.h"

#include "ui_file_transfer_window.h"
#include "util/config/config_util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QStringList>
#include <QTableWidget>


void FileTransferWindow::deleteSelectedLocalFiles()
{
    const QModelIndexList selectedRows = ui->localTable->selectionModel()->selectedRows();
    if (selectedRows.isEmpty())
        return;

    QStringList paths;
    for (const QModelIndex &index : selectedRows)
    {
        QTableWidgetItem *nameItem = ui->localTable->item(index.row(), 0);
        if (!nameItem || nameItem->text() == QStringLiteral(".."))
            continue;
        paths.append(QDir::cleanPath(currentLocalDir.absoluteFilePath(nameItem->text())));
    }
    paths.removeDuplicates();
    if (paths.isEmpty())
        return;

    if (RuntimeEnvironment::uiAvailable())
    {
        const int ret = QMessageBox::question(this, tr("Delete"),
                                              tr("Delete %1 selected local item(s)?").arg(paths.size()));
        if (ret != QMessageBox::Yes)
            return;
    }

    QStringList failures;
    for (const QString &path : paths)
    {
        QFileInfo info(path);
        bool ok = false;
        if (info.isDir() && !info.isSymLink())
            ok = QDir(path).removeRecursively();
        else
            ok = QFile::remove(path);
        if (!ok)
            failures.append(path);
    }

    populateLocalFiles();
    if (!failures.isEmpty() && RuntimeEnvironment::uiAvailable())
        QMessageBox::warning(this, tr("Delete failed"), failures.join(QLatin1Char('\n')));
}


void FileTransferWindow::deleteSelectedRemoteFiles()
{
    if (!connected)
        return;

    const QModelIndexList selectedRows = ui->remoteTable->selectionModel()->selectedRows();
    if (selectedRows.isEmpty())
        return;

    QStringList paths;
    for (const QModelIndex &index : selectedRows)
    {
        QTableWidgetItem *nameItem = ui->remoteTable->item(index.row(), 0);
        if (!nameItem || nameItem->text() == QStringLiteral(".."))
            continue;
        paths.append(QDir::cleanPath(currentRemotePath + "/" + nameItem->text()));
    }
    paths.removeDuplicates();
    if (paths.isEmpty())
        return;

    if (RuntimeEnvironment::uiAvailable())
    {
        const int ret = QMessageBox::question(this, tr("Delete"),
                                              tr("Delete %1 selected remote item(s)?").arg(paths.size()));
        if (ret != QMessageBox::Yes)
            return;
    }

    for (const QString &path : paths)
        requestRemoteDelete(path);
}
