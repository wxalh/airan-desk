#ifndef CLIPBOARD_UTIL_H
#define CLIPBOARD_UTIL_H

#include <functional>

#include <QJsonObject>
#include <QString>
#include <QStringList>

class QObject;

class ClipboardUtil
{
public:
    using ReadCompletion = std::function<void(const QJsonObject &payload)>;
    using WriteCompletion = std::function<void(bool ok, const QString &errorMessage)>;
    static QJsonObject readClipboardPayload();
    static void readClipboardPayloadAsync(QObject *context, const ReadCompletion &completion);
    static bool writeClipboardPayload(const QJsonObject &payload, QString *errorMessage = nullptr);
    static void writeClipboardPayloadAsync(QObject *context, const QJsonObject &payload, const WriteCompletion &completion);
    static bool writeFilePaths(const QStringList &paths, QString *errorMessage = nullptr);
    static void writeFilePathsAsync(QObject *context, const QStringList &paths, const WriteCompletion &completion);
    static bool payloadHasFiles(const QJsonObject &payload);
    static bool payloadHasImage(const QJsonObject &payload);
    static QStringList payloadFilePaths(const QJsonObject &payload);
    static QJsonObject payloadWithFilePaths(const QStringList &paths);
    static QString cacheRoot(const QString &sessionId = QString());
    static bool cleanupCacheRoot(const QString &path);
    static void cleanupCacheRootAsync(const QString &path);
};

#endif /* CLIPBOARD_UTIL_H */
