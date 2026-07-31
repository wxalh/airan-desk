#include "util/clipboard/native/clipboard_file_promise.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QTimer>

bool ClipboardFilePromise::isSupported()
{
    return false;
}

void ClipboardFilePromise::cancelCacheRoot(const QString &cacheRoot)
{
    Q_UNUSED(cacheRoot)
}

bool ClipboardFilePromise::install(const QList<ClipboardFilePromiseItem> &items,
                                   const QString &cacheRoot,
                                   const ReadFileChunk &readFileChunk,
                                   QString *errorMessage)
{
    Q_UNUSED(items)
    Q_UNUSED(cacheRoot)
    Q_UNUSED(readFileChunk)
    if (errorMessage)
        *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "Native file promise clipboard is not supported on this platform.");
    return false;
}

void ClipboardFilePromise::installAsync(QObject *context,
                                            const QList<ClipboardFilePromiseItem> &items,
                                            const QString &cacheRoot,
                                            const ReadFileChunk &readFileChunk,
                                            const InstallCompletion &completion)
{
    Q_UNUSED(items)
    Q_UNUSED(cacheRoot)
    Q_UNUSED(readFileChunk)
    const QString error = QCoreApplication::translate(
        "ClipboardFilePromise", "Native clipboard file promise is not supported on this platform.");
    if (!completion)
        return;
    if (context)
        QTimer::singleShot(0, context, [completion, error]() { completion(false, error); });
    else
        completion(false, error);
}

bool ClipboardFilePromise::startDrag(QWidget *dragSource,
                                     const QList<ClipboardFilePromiseItem> &items,
                                     const QString &cacheRoot,
                                     const ReadFileChunk &readFileChunk,
                                     QString *errorMessage)
{
    Q_UNUSED(dragSource)
    Q_UNUSED(items)
    Q_UNUSED(cacheRoot)
    Q_UNUSED(readFileChunk)
    if (errorMessage)
        *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "Native file promise drag is not supported on this platform.");
    return false;
}
