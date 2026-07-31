#include "config_util.h"
#include "config_util_paths.h"


ConfigUtilData::ConfigUtilData(QObject *parent)
    : QObject{parent}
{
    config_util_internal::configureDefaultSettingsPath();
    initCommonIni();
    initIdIni();
    SPDLOG_INFO("local control code initialized: {}", local_id.toStdString());
}


ConfigUtilData::~ConfigUtilData()
{
    if (m_commonIni)
    {
        try { m_commonIni->sync(); } catch (...) {}
        delete m_commonIni;
        m_commonIni = nullptr;
    }
}


ConfigUtilData *ConfigUtilData::getInstance()
{
    static ConfigUtilData configUtil;
    return &configUtil;
}
