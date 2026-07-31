#ifndef CONFIG_UTIL_H
#define CONFIG_UTIL_H

#include <QObject>
#include <QSettings>
#include <QUuid>
#include <cstdint>
#include <spdlog/spdlog.h>
#include "security/runtime_environment.h"

#define ConfigUtil ConfigUtilData::getInstance()

/*
 * Centralizes app, signaling, ICE, identity, and password configuration.
 */
class ConfigUtilData : public QObject
{
    Q_OBJECT
private:
    /* Reads or creates the installation ID. */
    QString getOrCreateInstallId();
    void applyIdentitySnapshot(const QString &localId, const QString &password);
    /* Saves common.ini settings. */
    void saveCommonIni(QSettings &settings);
    /* Initializes local identity settings. */
    void initIdIni();
    /* Initializes common settings. */
    void initCommonIni();

public:
    explicit ConfigUtilData(QObject *parent = nullptr);
    ~ConfigUtilData();
    static ConfigUtilData *getInstance();
    bool setLocalPwd(const QString &pwd);
    bool replaceLocalId(const QString &localId);
    QString getLocalPwd();
    bool saveCommonConfig(QString *errorMessage = nullptr);
    bool loadPendingOpenH264Config(bool *enabled, QString *libraryPath);
    bool savePendingOpenH264Config(bool enabled, const QString &libraryPath);
    void applyAutoStartSetting();

public:
    /* common.ini file path and handle. */
    QString commonFilePath;
    QSettings *m_commonIni;
    /* Canonical local identity file. */
    QString idFilePath;
    /* Maximum remote-control FPS. Dynamic network/CPU caps may lower it at runtime. */
    int fps;
    /* Whether this device accepts inbound controlled sessions. */
    bool allow_remote;
    /* Whether the normal-privilege main process starts at user logon. */
    bool auto_start;
    /* UI language: auto, zh_CN, en_US. */
    QString language;
    /* Local control ID, installation ID, and access password digest. */
    QString local_id;
    QString install_id;
    QString local_pwd_md5;
    bool identity_storage_ready{true};
    /* WebSocket signaling server URL. */
    QString wsUrl;
    /* ICE server settings. */
    QString ice_host;
    uint16_t ice_port;
    QString ice_username;
    QString ice_password;
    /* Remote desktop request settings. */
    QString remote_network_path;
    QString remote_media_topology;
    QString remote_quality_profile;
    int remote_width;
    int remote_height;
    bool enable_wgc_capture;
    bool enable_dxgi_capture;
    bool enable_dxgi_native_gpu_capture;
    /* Audio input and loopback device settings. */
    QString audio_mic_device;
    QString audio_loopback_device;
    /* User-controlled optional Cisco OpenH264 software codec state. */
    bool openh264_enabled{false};
    QString openh264_library_path;
    /* Log level settings. */
    spdlog::level::level_enum logLevel;
    QString logLevelStr;
    /* Optional headless notification script. */
    QString notify_script;

private:
    /* Plain local password, used only for local UI display and config saves. */
    QString local_pwd;
signals:
};

#endif /* CONFIG_UTIL_H */
