#ifndef CLIPBOARD_FILE_PROMISE_H
#define CLIPBOARD_FILE_PROMISE_H

#include <functional>

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>

class QObject;
class QWidget;

struct ClipboardFilePromiseItem
{
    QString sourcePath;
    QString displayName;
    qint64 size = 0;
    bool isDirectory = false;
};

class ClipboardFilePromise
{
public:
    using ReadFileChunk = std::function<QByteArray(const QString &sourcePath,
                                                   qint64 offset,
                                                   qint64 maxBytes,
                                                   bool *ok,
                                                   QString *errorMessage)>;
    using InstallCompletion = std::function<void(bool ok, const QString &errorMessage)>;

    static bool isSupported();
    static void cancelCacheRoot(const QString &cacheRoot);
    static bool install(const QList<ClipboardFilePromiseItem> &items,
                        const QString &cacheRoot,
                        const ReadFileChunk &readFileChunk,
                        QString *errorMessage = nullptr);
    static void installAsync(QObject *context,
                             const QList<ClipboardFilePromiseItem> &items,
                             const QString &cacheRoot,
                             const ReadFileChunk &readFileChunk,
                             const InstallCompletion &completion);
    static bool startDrag(QWidget *dragSource,
                          const QList<ClipboardFilePromiseItem> &items,
                          const QString &cacheRoot,
                          const ReadFileChunk &readFileChunk,
                          QString *errorMessage = nullptr);
};

Q_DECLARE_METATYPE(ClipboardFilePromiseItem)
Q_DECLARE_METATYPE(QList<ClipboardFilePromiseItem>)
Q_DECLARE_METATYPE(ClipboardFilePromise::ReadFileChunk)

#endif /* CLIPBOARD_FILE_PROMISE_H */
