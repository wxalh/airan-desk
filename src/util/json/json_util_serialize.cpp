#include "json_util.h"


JsonObjectBuilder JsonUtil::createObject()
{
    return JsonObjectBuilder();
}


QByteArray JsonUtil::toCompactBytes(const QJsonObject &object)
{
    QJsonDocument doc(object);
    return doc.toJson(QJsonDocument::Compact);
}


QString JsonUtil::toCompactString(const QJsonObject &object)
{
    return QString::fromUtf8(toCompactBytes(object));
}
