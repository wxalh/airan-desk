#include "config_util.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>

ConfigUtilData::ConfigUtilData(QObject *parent)
    : QObject{parent}
{
    initCommonIni();
    initIdIni();
    SPDLOG_INFO("local control code: {} pwd: {}", local_id.toStdString(), local_pwd.toStdString());
}

ConfigUtilData::~ConfigUtilData()
{
    // Ensure QSettings are cleaned up
    if (m_idIni) {
        try { m_idIni->sync(); } catch(...) {}
        delete m_idIni;
        m_idIni = nullptr;
    }
    if (m_commonIni) {
        try { m_commonIni->sync(); } catch(...) {}
        delete m_commonIni;
        m_commonIni = nullptr;
    }
}

ConfigUtilData *ConfigUtilData::getInstance()
{
    static ConfigUtilData configUtil;
    return &configUtil;
}

QString ConfigUtilData::getOrCreateUuid()
{
    // 设置组织名和应用名（确定存储路径）
    QCoreApplication::setOrganizationName("wxalh.com");
    QCoreApplication::setApplicationName("airan");
    QSettings settings; // 自动选择系统默认位置

    // 尝试读取存储的UUID
    QString uuidKey = "Global/Uuid";
    QString storedUuid = settings.value(uuidKey).toString().toUpper();

    // 检查UUID是否有效（非空且符合格式）
    QUuid uuid(storedUuid);
    if (!storedUuid.isEmpty() && !uuid.isNull())
    {
        return storedUuid;
    }

    // 生成新的UUID并存储
    QUuid newUuid = QUuid::createUuid();
    QString newUuidStr = newUuid.toString().remove("{").remove("}").toUpper(); // 移除花括号
    settings.setValue(uuidKey, newUuidStr);
    settings.sync(); // 强制写入磁盘
    return newUuidStr;
}

void ConfigUtilData::saveIdIni()
{
    m_idIni->beginGroup("local");
    m_idIni->setValue("local_id", local_id);
    m_idIni->setValue("local_pwd", local_pwd);
    m_idIni->endGroup();
    m_idIni->sync();
}

void ConfigUtilData::saveCommonIni()
{
    m_commonIni->beginGroup("local");
    m_commonIni->setValue("showUI", showUI);
    m_commonIni->setValue("logLevel", logLevelStr);
    m_commonIni->endGroup();

    m_commonIni->beginGroup("remote");
    m_commonIni->setValue("fps", fps);
    m_commonIni->endGroup();

    m_commonIni->beginGroup("signal_server");
    m_commonIni->setValue("wsUrl", wsUrl);
    m_commonIni->endGroup();

    m_commonIni->beginGroup("ice_server");
    m_commonIni->setValue("host", ice_host);
    m_commonIni->setValue("port", ice_port);
    m_commonIni->setValue("username", ice_username);
    m_commonIni->setValue("password", ice_password);
    m_commonIni->endGroup();

    m_commonIni->sync();
}

void ConfigUtilData::initIdIni()
{
    idFilePath = QCoreApplication::applicationDirPath() + "/conf/id.ini";
    m_idIni = new QSettings(idFilePath, QSettings::IniFormat);
    m_idIni->setIniCodec("UTF-8");

    m_idIni->beginGroup("local");
    local_id = getOrCreateUuid();
    local_pwd = m_idIni->value("local_pwd", "").toString();
    m_idIni->endGroup();

    if (local_pwd.isEmpty() || QUuid(local_pwd).isNull())
    {
        local_pwd = QUuid::createUuid().toString().remove("{").remove("}").toUpper();
    }

    setLocalPwd(local_pwd);
}

void ConfigUtilData::initCommonIni()
{
    commonFilePath = QCoreApplication::applicationDirPath() + "/conf/common.ini";
    QFile fileCheck(commonFilePath);
    bool fileExists = fileCheck.exists();

    m_commonIni = new QSettings(commonFilePath, QSettings::IniFormat);
    m_commonIni->setIniCodec("UTF-8");

    m_commonIni->beginGroup("local");
    showUI = m_commonIni->value("showUI", true).toBool();
    logLevelStr = m_commonIni->value("logLevel", "info").toString();
    m_commonIni->endGroup();

    m_commonIni->beginGroup("remote");
    fps = m_commonIni->value("fps", 25).toInt(); // 默认帧率25
    m_commonIni->endGroup();

    if (fps < 1 || fps > 60)
    {
        fps = 25;
    }
    m_commonIni->beginGroup("signal_server");
    wsUrl = m_commonIni->value("wsUrl", "").toString();
    m_commonIni->endGroup();

    m_commonIni->beginGroup("ice_server");
    ice_host = m_commonIni->value("host", "stun.l.google.com").toString();
    ice_port = (uint16_t)(m_commonIni->value("port", 19302).toUInt());
    ice_username = m_commonIni->value("username", "").toString();
    ice_password = m_commonIni->value("password", "").toString();
    m_commonIni->endGroup();

    if (logLevelStr == "trace")
    {
        logLevel = spdlog::level::trace;
    }
    else if (logLevelStr == "debug")
    {
        logLevel = spdlog::level::debug;
    }
    else if (logLevelStr == "info")
    {
        logLevel = spdlog::level::info;
    }
    else if (logLevelStr == "warn")
    {
        logLevel = spdlog::level::warn;
    }
    else if (logLevelStr == "error")
    {
        logLevel = spdlog::level::err;
    }
    else if (logLevelStr == "critical")
    {
        logLevel = spdlog::level::critical;
    }
    else
    {
        logLevel = spdlog::level::info; // 默认级别
    }
    
    if (fileExists)
    {
        m_commonIni->sync();
    }
}

void ConfigUtilData::setLocalPwd(const QString &pwd)
{
    this->local_pwd = pwd;
    QByteArray hashResult = QCryptographicHash::hash(local_pwd.toUtf8(), QCryptographicHash::Md5);
    this->local_pwd_md5 = hashResult.toHex().toUpper();
    saveIdIni();
}

QString ConfigUtilData::getLocalPwd()
{
    return local_pwd;
}
