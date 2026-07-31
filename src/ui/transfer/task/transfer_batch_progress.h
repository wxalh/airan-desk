#ifndef TRANSFER_BATCH_PROGRESS_H
#define TRANSFER_BATCH_PROGRESS_H

#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <limits>

struct TransferBatchProgressItem
{
    int index{0};
    bool completed{false};
    qint64 transferredBytes{0};
    qint64 totalBytes{0};
    int transferredFiles{0};
    int totalFiles{0};
};

struct TransferBatchProgress
{
    qint64 transferredBytes{0};
    qint64 totalBytes{0};
    int transferredFiles{0};
    int totalFiles{0};
    int currentFile{0};
};

inline TransferBatchProgress aggregateTransferBatchProgress(QVector<TransferBatchProgressItem> items)
{
    std::sort(items.begin(), items.end(), [](const TransferBatchProgressItem &left,
                                             const TransferBatchProgressItem &right) {
        return left.index < right.index;
    });

    const auto addBytes = [](qint64 current, qint64 value) {
        value = qMax<qint64>(0, value);
        return current <= (std::numeric_limits<qint64>::max)() - value
                   ? current + value
                   : (std::numeric_limits<qint64>::max)();
    };
    const auto addFiles = [](int current, int value) {
        value = qMax(0, value);
        return current <= (std::numeric_limits<int>::max)() - value
                   ? current + value
                   : (std::numeric_limits<int>::max)();
    };

    TransferBatchProgress progress;
    bool currentFound = false;
    for (const TransferBatchProgressItem &item : items)
    {
        progress.transferredBytes = addBytes(progress.transferredBytes, item.transferredBytes);
        progress.totalBytes = addBytes(progress.totalBytes, item.totalBytes);
        progress.transferredFiles = addFiles(progress.transferredFiles, item.transferredFiles);

        const int displayFiles = qMax(1, item.totalFiles);
        progress.totalFiles = addFiles(progress.totalFiles, displayFiles);
        if (currentFound)
            continue;

        if (item.completed)
        {
            progress.currentFile = addFiles(progress.currentFile, displayFiles);
            continue;
        }

        const int transferredFiles = qMax(0, item.transferredFiles);
        const int nextFile = transferredFiles < (std::numeric_limits<int>::max)()
                                 ? transferredFiles + 1
                                 : transferredFiles;
        const int currentWithinItem = item.totalFiles > 0
                                          ? qMin(displayFiles, nextFile)
                                          : 1;
        progress.currentFile = addFiles(progress.currentFile, currentWithinItem);
        currentFound = true;
    }
    return progress;
}

#endif /* TRANSFER_BATCH_PROGRESS_H */
