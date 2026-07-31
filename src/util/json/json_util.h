#ifndef JSONUTIL_H
#define JSONUTIL_H

#include <QObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QVariant>
#include <QMap>



class JsonObjectBuilder
{
public:
    JsonObjectBuilder() = default;

    JsonObjectBuilder& add(const QString &key, const char* value);
    JsonObjectBuilder& add(const QString &key, const QString &value);
    JsonObjectBuilder& add(const QString &key, const std::string &value);
    JsonObjectBuilder& add(const std::string &key, const std::string &value);
    JsonObjectBuilder& add(const QString &key, int value);
    JsonObjectBuilder& add(const QString &key, qint64 value);
    JsonObjectBuilder& add(const QString &key, bool value);
    JsonObjectBuilder& add(const QString &key, double value);
    JsonObjectBuilder& add(const QString &key, const QJsonObject &value);
    JsonObjectBuilder& add(const QString &key, const QJsonArray &value);

    QJsonObject build() const { return m_object; }
    QByteArray toBytes() const;
    QString toString() const;

private:
    QJsonObject m_object;
};

class JsonUtil
{
public:
    

    
    static QJsonObject safeParseObject(const QByteArray &data);

    
    static QJsonObject safeParseObject(const QString &str);

    
    static QJsonArray safeParseArray(const QByteArray &data);

    

    
    static QString getString(const QJsonObject &object, const QString &key, const QString &defaultValue = QString());

    
    static int getInt(const QJsonObject &object, const QString &key, int defaultValue = 0);

    
    static qint64 getInt64(const QJsonObject &object, const QString &key, qint64 defaultValue = 0);

    
    static bool getBool(const QJsonObject &object, const QString &key, bool defaultValue = false);

    
    static double getDouble(const QJsonObject &object, const QString &key, double defaultValue = 0.0);

    
    static QJsonObject getObject(const QJsonObject &object, const QString &key);

    
    static QJsonArray getArray(const QJsonObject &object, const QString &key);

    

    
    static bool hasRequiredKeys(const QJsonObject &object, const QStringList &requiredKeys);

    
    static bool isValidObject(const QJsonObject &object);

    

    
    static JsonObjectBuilder createObject();

    
    static QByteArray toCompactBytes(const QJsonObject &object);

    
    static QString toCompactString(const QJsonObject &object);

    

    
    static QJsonObject str2Json(const QString &str) { return safeParseObject(str); }

    
    static QMap<QString, QVariant> json2Map(const QJsonObject &json_obj);

    
    static QMap<QString, QVariant> json2Map(const QString &str);
};


#endif /* JSONUTIL_H */
