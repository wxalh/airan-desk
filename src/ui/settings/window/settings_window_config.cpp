#include "settings_window.h"

#include "media/codec/openh264/openh264_binary_manager.h"
#include "security/audit_logger.h"
#include "security/controlled_access_gate.h"
#include "ui/widgets/password/password_line_edit_util.h"
#include "ui_settings_window.h"
#include "util/config/config_util.h"
#include "util/text/i18n_util.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>

namespace
{

void setComboByData(QComboBox *combo, const QString &data)
{
    const int index = combo ? combo->findData(data) : -1;
    if (index >= 0)
        combo->setCurrentIndex(index);
}
}


void SettingsWindow::populateGeneralSettings()
{
    m_allowRemoteCheck->setChecked(ConfigUtil->allow_remote);
    m_notificationScriptEdit->setText(ConfigUtil->notify_script);
    if (m_autoStartCheck)
        m_autoStartCheck->setChecked(ConfigUtil->auto_start);

    for (const QString &level : {QStringLiteral("trace"), QStringLiteral("debug"), QStringLiteral("info"),
                                 QStringLiteral("warn"), QStringLiteral("error"), QStringLiteral("critical")})
        m_logLevelCombo->addItem(level, level);
    setComboByData(m_logLevelCombo, ConfigUtil->logLevelStr);

    m_languageCombo->addItem(tr("Auto"), I18nUtil::autoLanguageKey());
    m_languageCombo->addItem(tr("Simplified Chinese"), I18nUtil::zhCnLanguageKey());
    m_languageCombo->addItem(tr("English"), I18nUtil::enUsLanguageKey());
    setComboByData(m_languageCombo, I18nUtil::normalizeUiLanguage(ConfigUtil->language));
}


void SettingsWindow::populateConnectionSettings()
{
    m_wsUrlEdit->setText(ConfigUtil->wsUrl);
    m_iceHostEdit->setText(ConfigUtil->ice_host);
    m_icePortSpin->setRange(1, 65535);
    m_icePortSpin->setValue(ConfigUtil->ice_port);
    m_iceUserEdit->setText(ConfigUtil->ice_username);
    m_icePasswordEdit->setText(ConfigUtil->ice_password);
    installPasswordRevealButton(m_icePasswordEdit);
}


void SettingsWindow::populateAudioSettings()
{
    refreshAudioDevices();
    setComboByData(m_audioMicDeviceCombo, ConfigUtil->audio_mic_device);
    setComboByData(m_audioLoopbackDeviceCombo, ConfigUtil->audio_loopback_device);
    m_micLevelBar->setValue(0);
}


void SettingsWindow::populateRemoteSettings()
{
    m_fpsSpin->setRange(1, 120);
    m_fpsSpin->setValue(ConfigUtil->fps);

    m_networkCombo->addItem(tr("Auto"), QStringLiteral("auto"));
    m_networkCombo->addItem(tr("Direct"), QStringLiteral("direct"));
    m_networkCombo->addItem(tr("UDP relay"), QStringLiteral("turn_udp"));
    m_networkCombo->addItem(tr("TCP relay"), QStringLiteral("turn_tcp"));
    setComboByData(m_networkCombo, ConfigUtil->remote_network_path);

    if (m_enableWgcCheck)
    {
        m_enableWgcCheck->setChecked(ConfigUtil->enable_wgc_capture);
        if (m_enableDxgiCheck)
            m_enableDxgiCheck->setChecked(ConfigUtil->enable_dxgi_capture);
        if (m_enableDxgiNativeGpuCheck)
            m_enableDxgiNativeGpuCheck->setChecked(ConfigUtil->enable_dxgi_native_gpu_capture);
    }
}


void SettingsWindow::saveSettings()
{
    const bool requestedOpenH264Enabled = m_openh264EnableCheck->isChecked();
    QString requestedOpenH264Path = m_pendingOpenH264Path.trimmed();
    const airan::media::openh264::ValidationResult openh264Validation =
        airan::media::openh264::validateManagedBinary(requestedOpenH264Path);
    if (requestedOpenH264Enabled &&
        openh264Validation.availability != airan::media::openh264::Availability::Ready)
    {
        QMessageBox::warning(
            this,
            tr("OpenH264"),
            tr("Import a verified Cisco OpenH264 binary before enabling this codec."));
        return;
    }
    if (openh264Validation.availability == airan::media::openh264::Availability::Ready)
        requestedOpenH264Path = openh264Validation.absolutePath;
    else if (!requestedOpenH264Enabled)
        requestedOpenH264Path.clear();

    const bool previousAllowRemote = ConfigUtil->allow_remote;
    const QString previousWsUrl = ConfigUtil->wsUrl;
    ConfigUtil->allow_remote = m_allowRemoteCheck->isChecked();
    ConfigUtil->notify_script = m_notificationScriptEdit->text().trimmed();
    if (m_autoStartCheck)
        ConfigUtil->auto_start = m_autoStartCheck->isChecked();
    ConfigUtil->logLevelStr = m_logLevelCombo->currentData().toString();
    ConfigUtil->language = I18nUtil::normalizeUiLanguage(m_languageCombo->currentData().toString());
    ConfigUtil->wsUrl = m_wsUrlEdit->text().trimmed();
    ConfigUtil->ice_host = m_iceHostEdit->text().trimmed();
    ConfigUtil->ice_port = static_cast<uint16_t>(m_icePortSpin->value());
    ConfigUtil->ice_username = m_iceUserEdit->text().trimmed();
    ConfigUtil->ice_password = m_icePasswordEdit->text();
    ConfigUtil->audio_mic_device = selectedAudioDeviceValue(m_audioMicDeviceCombo).trimmed();
    ConfigUtil->audio_loopback_device = selectedAudioDeviceValue(m_audioLoopbackDeviceCombo).trimmed();

    ConfigUtil->fps = m_fpsSpin->value();
    ConfigUtil->remote_network_path = m_networkCombo->currentData().toString();
    ConfigUtil->remote_width = 0;
    ConfigUtil->remote_height = 0;
    if (m_enableWgcCheck)
        ConfigUtil->enable_wgc_capture = m_enableWgcCheck->isChecked();
    if (m_enableDxgiCheck)
        ConfigUtil->enable_dxgi_capture = m_enableDxgiCheck->isChecked();
    if (m_enableDxgiNativeGpuCheck)
        ConfigUtil->enable_dxgi_native_gpu_capture = m_enableDxgiNativeGpuCheck->isChecked();
    QString saveError;
    if (!ConfigUtil->saveCommonConfig(&saveError))
    {
        ConfigUtil->allow_remote = previousAllowRemote;
        QMessageBox::critical(this, tr("Settings"),
                              tr("Failed to save settings: %1").arg(saveError));
        return;
    }
    ConfigUtil->applyAutoStartSetting();
    if (ConfigUtil->allow_remote && !AuditLogger::instance().isReady())
    {
        QString auditError;
        if (!AuditLogger::instance().initialize(&auditError))
        {
            ControlledAccessGate::setRuntimePrerequisiteReady(false);
            emit controlledAccessChanged(true);
            QMessageBox::critical(this, tr("Controlled access"),
                                  tr("Controlled access remains unavailable because the audit log could not be opened: %1")
                                      .arg(auditError));
            return;
        }
    }
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    if (previousAllowRemote != ConfigUtil->allow_remote)
    {
        ControlledAccessGate::setRuntimePrerequisiteReady(!ConfigUtil->allow_remote);
        startServiceOperation();
    }
#else
    ControlledAccessGate::setRuntimePrerequisiteReady(
        !ConfigUtil->allow_remote || AuditLogger::instance().isReady());
#endif
    if (previousAllowRemote != ConfigUtil->allow_remote)
        emit controlledAccessChanged(ConfigUtil->allow_remote);
    if (!ConfigUtil->savePendingOpenH264Config(
            requestedOpenH264Enabled, requestedOpenH264Path))
    {
        QMessageBox::critical(this, tr("Settings"), tr("Failed to save OpenH264 settings."));
        return;
    }
    m_pendingOpenH264Enabled = requestedOpenH264Enabled;
    m_pendingOpenH264Path = requestedOpenH264Path;
    if (previousWsUrl != ConfigUtil->wsUrl)
        emit signalingServerChanged();
    const bool openH264RestartRequired =
        m_pendingOpenH264Enabled != ConfigUtil->openh264_enabled ||
        m_pendingOpenH264Path != ConfigUtil->openh264_library_path;
    QMessageBox::information(
        this,
        tr("Settings"),
        openH264RestartRequired
            ? tr("Saved. Restart Airan Desk to apply OpenH264 changes.")
            : tr("Saved."));
}
