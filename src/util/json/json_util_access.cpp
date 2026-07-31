#include "json_util.h"

#include "common/logger_manager.h"

#include <cmath>
#include <limits>


namespace
{
bool isIntegral(double value)
{
    return std::isfinite(value) && std::trunc(value) == value;
}
}


QString JsonUtil::getString(const QJsonObject &object, const QString &key, const QString &defaultValue)
{
    if (!object.contains(key))
        return defaultValue;

    QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : defaultValue;
}


int JsonUtil::getInt(const QJsonObject &object, const QString &key, int defaultValue)
{
    const QJsonValue value = object.value(key);
    if (!value.isDouble())
        return defaultValue;

    const double number = value.toDouble();
    if (!isIntegral(number) ||
        number < static_cast<double>((std::numeric_limits<int>::min)()) ||
        number > static_cast<double>((std::numeric_limits<int>::max)()))
    {
        return defaultValue;
    }

    return static_cast<int>(number);
}


qint64 JsonUtil::getInt64(const QJsonObject &object, const QString &key, qint64 defaultValue)
{
    const QJsonValue value = object.value(key);
    if (!value.isDouble())
        return defaultValue;

    const double number = value.toDouble();
    // qint64's positive limit rounds to 2^63 as a double, so use an
    // exclusive upper bound to keep the conversion defined on every platform.
    constexpr double kQint64UpperBound = 9223372036854775808.0;
    if (!isIntegral(number) || number < -kQint64UpperBound || number >= kQint64UpperBound)
        return defaultValue;

    return static_cast<qint64>(number);
}


bool JsonUtil::getBool(const QJsonObject &object, const QString &key, bool defaultValue)
{
    if (!object.contains(key))
        return defaultValue;

    QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : defaultValue;
}


double JsonUtil::getDouble(const QJsonObject &object, const QString &key, double defaultValue)
{
    const QJsonValue value = object.value(key);
    if (!value.isDouble())
        return defaultValue;

    const double number = value.toDouble();
    return std::isfinite(number) ? number : defaultValue;
}


QJsonObject JsonUtil::getObject(const QJsonObject &object, const QString &key)
{
    if (!object.contains(key))
        return QJsonObject();

    QJsonValue value = object.value(key);
    return value.isObject() ? value.toObject() : QJsonObject();
}


QJsonArray JsonUtil::getArray(const QJsonObject &object, const QString &key)
{
    if (!object.contains(key))
        return QJsonArray();

    QJsonValue value = object.value(key);
    return value.isArray() ? value.toArray() : QJsonArray();
}


bool JsonUtil::hasRequiredKeys(const QJsonObject &object, const QStringList &requiredKeys)
{
    for (const QString &key : requiredKeys)
    {
        if (!object.contains(key) || object.value(key).isNull())
        {
            LOG_ERROR("JsonUtil::hasRequiredKeys: Missing required key: {}", key);
            return false;
        }
    }
    return true;
}


bool JsonUtil::isValidObject(const QJsonObject &object)
{
    return !object.isEmpty();
}


QMap<QString, QVariant> JsonUtil::json2Map(const QJsonObject &json_obj)
{
    QMap<QString, QVariant> maps;
    if (!json_obj.isEmpty())
    {
        for (auto it = json_obj.begin(); it != json_obj.end(); ++it)
        {
            maps.insert(it.key(), it.value().toVariant());
        }
    }
    return maps;
}


QMap<QString, QVariant> JsonUtil::json2Map(const QString &str)
{
    QJsonObject json_obj = safeParseObject(str);
    return json2Map(json_obj);
}
