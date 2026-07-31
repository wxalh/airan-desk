#include "config_util.h"
#include "config_util_paths.h"
#include "identity_storage.h"

#include <QCryptographicHash>


QString ConfigUtilData::getOrCreateInstallId()
{
    QSettings settings(config_util_internal::writableConfFile(QStringLiteral("airan.ini"), false),
                       QSettings::IniFormat);
    const QString installIdKey = QStringLiteral("Global/InstallId");
    const QString storedInstallId = settings.value(installIdKey).toString().toUpper();
    if (!storedInstallId.isEmpty() && !QUuid(storedInstallId).isNull())
        return QUuid(storedInstallId).toString().remove(QLatin1Char('{')).remove(QLatin1Char('}')).toUpper();

    const QString newInstallId = QUuid::createUuid().toString()
                                     .remove(QLatin1Char('{'))
                                     .remove(QLatin1Char('}'))
                                     .toUpper();
    settings.setValue(installIdKey, newInstallId);
    settings.sync();
    return newInstallId;
}


void ConfigUtilData::applyIdentitySnapshot(const QString &localId, const QString &password)
{
    local_id = localId;
    local_pwd = password;
    local_pwd_md5 = QCryptographicHash::hash(local_pwd.toUtf8(), QCryptographicHash::Md5)
                        .toHex()
                        .toUpper();
}


bool ConfigUtilData::replaceLocalId(const QString &localId)
{
    IdentityStorage::Snapshot stored;
    QString error;
    if (!IdentityStorage::replaceLocalId(idFilePath, localId, &stored, &error))
    {
        identity_storage_ready = false;
        SPDLOG_ERROR("Failed to persist replacement local ID: {}", error.toStdString());
        return false;
    }

    applyIdentitySnapshot(stored.localId, stored.password);
    identity_storage_ready = true;
    return true;
}


void ConfigUtilData::initIdIni()
{
    idFilePath = config_util_internal::writableConfFile(QStringLiteral("id.ini"), false);
    install_id = getOrCreateInstallId().trimmed();

    QSettings legacySettings(
        config_util_internal::writableConfFile(QStringLiteral("airan.ini"), false),
        QSettings::IniFormat);
    const QString legacyKey = QStringLiteral("Global/Uuid");
    const QString legacyId = legacySettings.value(legacyKey).toString();

    IdentityStorage::Snapshot stored;
    QString error;
    if (!IdentityStorage::loadOrCreate(idFilePath, legacyId, &stored, &error))
    {
        identity_storage_ready = false;
        local_id.clear();
        local_pwd.clear();
        local_pwd_md5.clear();
        SPDLOG_ERROR("Identity storage initialization failed: {}", error.toStdString());
        return;
    }

    applyIdentitySnapshot(stored.localId, stored.password);
    identity_storage_ready = true;

    if (legacySettings.contains(legacyKey))
    {
        legacySettings.remove(legacyKey);
        legacySettings.sync();
        if (legacySettings.status() != QSettings::NoError)
            SPDLOG_WARN("Legacy local ID could not be removed from airan.ini; id.ini remains authoritative");
    }
}


bool ConfigUtilData::setLocalPwd(const QString &pwd)
{
    IdentityStorage::Snapshot stored;
    QString error;
    if (!IdentityStorage::replacePassword(idFilePath, pwd, &stored, &error))
    {
        identity_storage_ready = false;
        SPDLOG_ERROR("Failed to persist replacement local password: {}", error.toStdString());
        return false;
    }

    applyIdentitySnapshot(stored.localId, stored.password);
    identity_storage_ready = true;
    return true;
}


QString ConfigUtilData::getLocalPwd()
{
    return local_pwd;
}
