#include "ui/transfer/file_transfer_window.h"
#include "ui/transfer/task/transfer_batch_progress.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QUuid>

#include <limits>

QString FileTransferWindow::createTransferId() const
{
    QString uuid = QUuid::createUuid().toString();
    if (uuid.startsWith(QLatin1Char('{')) && uuid.endsWith(QLatin1Char('}')))
        return uuid.mid(1, uuid.size() - 2);
    return uuid;
}


qint64 FileTransferWindow::collectDirectoryStats(const QString &path, int *fileCount) const
{
    if (fileCount)
        *fileCount = 0;

    QFileInfo info(path);
    if (!info.exists())
        return 0;

    if (info.isFile())
    {
        if (fileCount)
            *fileCount = 1;
        return info.size();
    }

    qint64 totalBytes = 0;
    int totalFiles = 0;
    QDirIterator it(path,
                    QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        QFileInfo fileInfo(it.next());
        const qint64 fileSize = qMax<qint64>(0, fileInfo.size());
        totalBytes = totalBytes <= (std::numeric_limits<qint64>::max)() - fileSize
                         ? totalBytes + fileSize
                         : (std::numeric_limits<qint64>::max)();
        if (totalFiles < (std::numeric_limits<int>::max)())
            ++totalFiles;
    }

    if (fileCount)
        *fileCount = totalFiles;
    return totalBytes;
}


FileTransferWindow::TransferTask *FileTransferWindow::findTransferTaskById(const QString &transferId)
{
    if (transferId.isEmpty() || !m_transferTasks.contains(transferId))
        return nullptr;
    return &m_transferTasks[transferId];
}


FileTransferWindow::TransferTask *FileTransferWindow::findTransferTaskByPath(const QString &path)
{
    const QString cleanPath = QDir::cleanPath(path);
    auto mappedIt = m_transferPathIds.find(cleanPath);
    if (mappedIt != m_transferPathIds.end())
    {
        QStringList &mappedTransferIds = mappedIt.value();
        while (!mappedTransferIds.isEmpty())
        {
            TransferTask *mappedTask = findTransferTaskById(mappedTransferIds.first());
            if (mappedTask && !mappedTask->completed && !mappedTask->canceled)
                return mappedTask;
            mappedTransferIds.removeFirst();
        }
        m_transferPathIds.erase(mappedIt);
    }

    TransferTask *completedMatch = nullptr;
    int completedRow = -1;
    TransferTask *pendingMatch = nullptr;
    int pendingRow = -1;

    for (auto it = m_transferTasks.begin(); it != m_transferTasks.end(); ++it)
    {
        if (it->sourcePath == path || it->destinationPath == path)
        {
            TransferTask *task = &it.value();
            if (!task->completed && !task->canceled)
            {
                if (task->row >= pendingRow)
                {
                    pendingRow = task->row;
                    pendingMatch = task;
                }
            }
            else if (task->row >= completedRow)
            {
                completedRow = task->row;
                completedMatch = task;
            }
        }
    }

    return pendingMatch ? pendingMatch : completedMatch;
}


void FileTransferWindow::registerTransferPath(const QString &path, const QString &transferId)
{
    if (path.isEmpty() || transferId.isEmpty())
        return;

    QStringList &transferIds = m_transferPathIds[QDir::cleanPath(path)];
    if (!transferIds.contains(transferId))
        transferIds.append(transferId);
}


void FileTransferWindow::unregisterTransferPaths(const TransferTask &task)
{
    const QStringList paths{QDir::cleanPath(task.sourcePath), QDir::cleanPath(task.destinationPath)};
    for (const QString &path : paths)
    {
        auto mappedIt = m_transferPathIds.find(path);
        if (mappedIt == m_transferPathIds.end())
            continue;

        mappedIt.value().removeAll(task.transferId);
        if (mappedIt.value().isEmpty())
            m_transferPathIds.erase(mappedIt);
    }
}


void FileTransferWindow::updateBatchTask(TransferTask &task)
{
    if (task.batchExpectedItems <= 0 || !task.batchParentId.isEmpty())
        return;

    QVector<TransferBatchProgressItem> children;
    for (auto it = m_transferTasks.cbegin(); it != m_transferTasks.cend(); ++it)
    {
        const TransferTask &child = it.value();
        if (child.batchParentId != task.transferId)
            continue;
        children.append({child.batchItemIndex,
                         child.completed,
                         child.transferredBytes,
                         child.totalBytes,
                         child.transferredFiles,
                         child.totalFiles});
    }

    const TransferBatchProgress progress = aggregateTransferBatchProgress(children);
    task.transferredBytes = progress.transferredBytes;
    task.totalBytes = progress.totalBytes;
    task.transferredFiles = progress.transferredFiles;
    task.totalFiles = progress.totalFiles;
    task.currentFile = progress.currentFile;
    updateProgressCell(task);
}
