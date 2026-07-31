#include "json_util.h"

#include "common/logger_manager.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{
struct JsonErrorLogState
{
    std::chrono::steady_clock::time_point nextLog{};
    std::uint64_t suppressed = 0;
};

void logJsonParseError(const char *kind, const QString &detail)
{
    const auto now = std::chrono::steady_clock::now();
    std::uint64_t suppressed = 0;
    bool shouldLog = false;
    {
        static std::mutex mutex;
        static std::unordered_map<std::string, JsonErrorLogState> states;
        std::lock_guard<std::mutex> lock(mutex);
        auto &state = states[std::string(kind)];
        if (state.nextLog.time_since_epoch().count() == 0 || now >= state.nextLog)
        {
            shouldLog = true;
            suppressed = state.suppressed;
            state.suppressed = 0;
            state.nextLog = now + std::chrono::seconds(1);
        }
        else
        {
            ++state.suppressed;
        }
    }

    if (!shouldLog)
        return;
    if (suppressed > 0)
        LOG_ERROR("JsonUtil::safeParse{}: {} ({} repeated errors suppressed)", kind, detail, suppressed);
    else
        LOG_ERROR("JsonUtil::safeParse{}: {}", kind, detail);
}
} // namespace


QJsonObject JsonUtil::safeParseObject(const QByteArray &data)
{
    if (data.isEmpty())
        return QJsonObject();

    QJsonParseError error;
    error.error = QJsonParseError::NoError;
    error.offset = 0;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError)
    {
        logJsonParseError("Object", QStringLiteral("Parse error: ") + error.errorString());
        return QJsonObject();
    }

    if (!doc.isObject())
    {
        logJsonParseError("Object", QStringLiteral("Document is not an object"));
        return QJsonObject();
    }

    return doc.object();
}


QJsonObject JsonUtil::safeParseObject(const QString &str)
{
    return safeParseObject(str.toUtf8());
}


QJsonArray JsonUtil::safeParseArray(const QByteArray &data)
{
    if (data.isEmpty())
        return QJsonArray();

    QJsonParseError error;
    error.error = QJsonParseError::NoError;
    error.offset = 0;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError)
    {
        logJsonParseError("Array", QStringLiteral("Parse error: ") + error.errorString());
        return QJsonArray();
    }

    if (!doc.isArray())
    {
        logJsonParseError("Array", QStringLiteral("Document is not an array"));
        return QJsonArray();
    }

    return doc.array();
}
