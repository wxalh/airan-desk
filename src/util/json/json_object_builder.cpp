#include "json_util.h"


JsonObjectBuilder &JsonObjectBuilder::add(const QString &key, const char *value)
{
    m_object.insert(key, QString::fromUtf8(value));
    return *this;
}


JsonObjectBuilder &JsonObjectBuilder::add(const QString &key, const QString &value)
{
    m_object.insert(key, value);
    return *this;
}


JsonObjectBuilder &JsonObjectBuilder::add(const QString &key, const std::string &value)
{
    m_object.insert(key, QString::fromStdString(value));
    return *this;
}


JsonObjectBuilder &JsonObjectBuilder::add(const std::string &key, const std::string &value)
{
    m_object.insert(QString::fromStdString(key), QString::fromStdString(value));
    return *this;
}


JsonObjectBuilder &JsonObjectBuilder::add(const QString &key, int value)
{
    m_object.insert(key, value);
    return *this;
}


JsonObjectBuilder &JsonObjectBuilder::add(const QString &key, qint64 value)
{
    m_object.insert(key, static_cast<double>(value));
    return *this;
}


JsonObjectBuilder &JsonObjectBuilder::add(const QString &key, bool value)
{
    m_object.insert(key, value);
    return *this;
}


JsonObjectBuilder &JsonObjectBuilder::add(const QString &key, double value)
{
    m_object.insert(key, value);
    return *this;
}


JsonObjectBuilder &JsonObjectBuilder::add(const QString &key, const QJsonObject &value)
{
    m_object.insert(key, value);
    return *this;
}


JsonObjectBuilder &JsonObjectBuilder::add(const QString &key, const QJsonArray &value)
{
    m_object.insert(key, value);
    return *this;
}


QByteArray JsonObjectBuilder::toBytes() const
{
    return JsonUtil::toCompactBytes(m_object);
}


QString JsonObjectBuilder::toString() const
{
    return JsonUtil::toCompactString(m_object);
}
