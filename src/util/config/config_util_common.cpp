#include "config_util.h"
#include "config_util_codec.h"
#include "config_util_paths.h"
#include "config_settings_writer.h"
#include "media/codec/openh264/openh264_binary_manager.h"
#include "util/text/i18n_util.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

namespace
{
QString validatedManagedOpenH264Path(const QString &candidatePath)
{
    return airan::media::openh264::isManagedLibraryPath(candidatePath)
               ? airan::media::openh264::managedLibraryPath()
               : QString();
}

QString launchExecutablePath()
{
    const QString exe = QCoreApplication::applicationFilePath();
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    const QFileInfo info(exe);
    if (info.fileName() == QStringLiteral("airan-desk.real"))
    {
        const QString wrapper = info.dir().filePath(QStringLiteral("airan-desk"));
        const QFileInfo wrapperInfo(wrapper);
        if (wrapperInfo.exists() && wrapperInfo.isExecutable())
            return wrapper;
    }
#endif
    return exe;
}

QString autoStartCommand()
{
    const QString exe = QDir::toNativeSeparators(launchExecutablePath());
    return QStringLiteral("\"%1\" --start-in-tray --delaystart 5").arg(exe);
}

QString autoStartDesktopEntry()
{
    const QString exe = launchExecutablePath();
    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Name=AiRan Desk\n"
                          "GenericName=Remote Desktop\n"
                          "Exec=\"%1\" --start-in-tray --delaystart 5\n"
                          "Icon=airan-desk\n"
                          "Terminal=false\n"
                          "Categories=Network;RemoteAccess;\n"
                          "StartupNotify=false\n"
                          "X-GNOME-Autostart-enabled=true\n")
        .arg(exe);
}

bool writeTextFile(const QString &path, const QString &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;

    file.write(content.toUtf8());
    file.close();
    return file.error() == QFile::NoError;
}
} /* namespace */

/*
 * Saves common settings, including controlled access, remote desktop, signaling, ICE, and
 * audio device configuration.
 */
void ConfigUtilData::saveCommonIni(QSettings &settings)
{
    settings.beginGroup(QStringLiteral("local"));
    settings.setValue(QStringLiteral("allowRemote"), allow_remote);
    settings.setValue(QStringLiteral("autoStart"), auto_start);
    settings.setValue(QStringLiteral("logLevel"), logLevelStr);
    language = I18nUtil::normalizeUiLanguage(language);
    settings.setValue(QStringLiteral("language"), language);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("notification"));
    settings.setValue(QStringLiteral("notify_script"), notify_script);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("remote"));
    settings.setValue(QStringLiteral("fps"), fps);
    settings.remove(QStringLiteral("fpsCpuCaptureCpuEncode"));
    settings.remove(QStringLiteral("fpsCpuCaptureOrCpuEncode"));
    settings.remove(QStringLiteral("fpsGpuCaptureHwEncode"));
    settings.setValue(QStringLiteral("networkPath"), remote_network_path);
    settings.setValue(QStringLiteral("mediaTopology"), remote_media_topology);
    settings.setValue(QStringLiteral("qualityProfile"), remote_quality_profile);
    settings.setValue(QStringLiteral("width"), remote_width);
    settings.setValue(QStringLiteral("height"), remote_height);
    settings.setValue(QStringLiteral("enableWgc"), enable_wgc_capture);
    settings.setValue(QStringLiteral("enableDxgi"), enable_dxgi_capture);
    settings.setValue(QStringLiteral("enableDxgiNativeGpu"), enable_dxgi_native_gpu_capture);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("signal_server"));
    settings.setValue(QStringLiteral("wsUrl"), wsUrl);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("ice_server"));
    settings.setValue(QStringLiteral("host"), ice_host);
    settings.setValue(QStringLiteral("port"), ice_port);
    settings.setValue(QStringLiteral("username"), ice_username);
    settings.setValue(QStringLiteral("password"), ice_password);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("audio"));
    settings.setValue(QStringLiteral("micDevice"), audio_mic_device);
    settings.setValue(QStringLiteral("loopbackDevice"), audio_loopback_device);
    settings.endGroup();
}

/* Public wrapper for saving common settings. */
bool ConfigUtilData::saveCommonConfig(QString *errorMessage)
{
    delete m_commonIni;
    m_commonIni = nullptr;
    const QSettings::Status status = config_util_internal::writeIniFile(
        commonFilePath,
        [this](QSettings &settings) { saveCommonIni(settings); });
    m_commonIni = new QSettings(commonFilePath, QSettings::IniFormat);
    if (status == QSettings::NoError)
        return true;
    if (errorMessage)
    {
        const QString reason = status == QSettings::AccessError
                                   ? tr("access denied")
                                   : tr("invalid INI format");
        *errorMessage = tr("%1 (%2)").arg(commonFilePath, reason);
    }
    return false;
}

bool ConfigUtilData::loadPendingOpenH264Config(bool *enabled, QString *libraryPath)
{
    if (!m_commonIni)
        return false;
    const config_util_internal::OpenH264Config config =
        config_util_internal::readOpenH264Config(
            *m_commonIni, validatedManagedOpenH264Path);
    if (enabled)
        *enabled = config.enabled;
    if (libraryPath)
        *libraryPath = config.libraryPath;
    return m_commonIni->status() == QSettings::NoError;
}

bool ConfigUtilData::savePendingOpenH264Config(bool enabled,
                                               const QString &libraryPath)
{
    if (!m_commonIni)
        return false;
    return config_util_internal::writeOpenH264Config(
        *m_commonIni,
        {enabled, libraryPath},
        validatedManagedOpenH264Path);
}

/*
 * Initializes common.ini, then reads and validates UI, remote desktop,
 * signaling, ICE, and audio settings.
 */
void ConfigUtilData::initCommonIni()
{
    commonFilePath = config_util_internal::writableConfFile(QStringLiteral("common.ini"), true);
    QFile fileCheck(commonFilePath);
    const bool fileExists = fileCheck.exists();

    m_commonIni = new QSettings(commonFilePath, QSettings::IniFormat);

    m_commonIni->beginGroup(QStringLiteral("local"));
    allow_remote = m_commonIni->value(QStringLiteral("allowRemote"), false).toBool();
    m_commonIni->remove(QStringLiteral("showUI"));
    auto_start = m_commonIni->value(QStringLiteral("autoStart"), false).toBool();
    logLevelStr = m_commonIni->value(QStringLiteral("logLevel"), "info").toString();
    language = m_commonIni->value(QStringLiteral("language"), "auto").toString().trimmed();
    m_commonIni->endGroup();

    m_commonIni->beginGroup(QStringLiteral("notification"));
    notify_script = m_commonIni->value(QStringLiteral("notify_script"), "").toString().trimmed();
    m_commonIni->endGroup();

    language = I18nUtil::normalizeUiLanguage(language);

    m_commonIni->beginGroup(QStringLiteral("remote"));
    const bool hasLegacyFpsCaps =
        m_commonIni->contains(QStringLiteral("fpsCpuCaptureCpuEncode")) ||
        m_commonIni->contains(QStringLiteral("fpsCpuCaptureOrCpuEncode")) ||
        m_commonIni->contains(QStringLiteral("fpsGpuCaptureHwEncode"));
    fps = m_commonIni->value(QStringLiteral("fps"), 120).toInt();
    if (hasLegacyFpsCaps && fps == 60)
        fps = 120;
    remote_network_path = m_commonIni->value(QStringLiteral("networkPath"), "auto").toString().trimmed().toLower();
    remote_media_topology = m_commonIni->value(QStringLiteral("mediaTopology"), "p2p").toString().trimmed().toLower();
    remote_quality_profile = m_commonIni->value(QStringLiteral("qualityProfile"), "auto").toString().trimmed().toLower();
    remote_width = m_commonIni->value(QStringLiteral("width"), 0).toInt();
    remote_height = m_commonIni->value(QStringLiteral("height"), 0).toInt();
    enable_wgc_capture = m_commonIni->value(QStringLiteral("enableWgc"), true).toBool();
    enable_dxgi_capture = m_commonIni->value(QStringLiteral("enableDxgi"), true).toBool();
    enable_dxgi_native_gpu_capture =
        m_commonIni->value(QStringLiteral("enableDxgiNativeGpu"), true).toBool();
    m_commonIni->endGroup();

    if (fps < 1 || fps > 120)
        fps = 120;
    m_commonIni->remove(QStringLiteral("fpsCpuCaptureCpuEncode"));
    m_commonIni->remove(QStringLiteral("fpsCpuCaptureOrCpuEncode"));
    m_commonIni->remove(QStringLiteral("fpsGpuCaptureHwEncode"));

    if (remote_network_path != QStringLiteral("auto") &&
        remote_network_path != QStringLiteral("direct") &&
        remote_network_path != QStringLiteral("turn_udp") &&
        remote_network_path != QStringLiteral("turn_tcp"))
    {
        remote_network_path = QStringLiteral("auto");
    }
    if (remote_media_topology != QStringLiteral("p2p") &&
        remote_media_topology != QStringLiteral("sfu"))
    {
        remote_media_topology = QStringLiteral("p2p");
    }
    if (remote_quality_profile == QStringLiteral("auto"))
    {
        remote_quality_profile = QStringLiteral("auto");
    }
    else if (remote_quality_profile == QStringLiteral("lan") ||
        remote_quality_profile == QStringLiteral("hd") ||
        remote_quality_profile == QStringLiteral("high") ||
        remote_quality_profile == QStringLiteral("lossless"))
    {
        remote_quality_profile = QStringLiteral("lan_hd");
    }
    else if (remote_quality_profile == QStringLiteral("weak") ||
             remote_quality_profile == QStringLiteral("weak_clear") ||
             remote_quality_profile == QStringLiteral("lowbandwidth") ||
             remote_quality_profile == QStringLiteral("clear"))
    {
        remote_quality_profile = QStringLiteral("weak_clear");
    }
    else if (remote_quality_profile != QStringLiteral("lan_hd") &&
             remote_quality_profile != QStringLiteral("weak_clear") &&
             remote_quality_profile != QStringLiteral("balanced") &&
             remote_quality_profile != QStringLiteral("auto"))
    {
        remote_quality_profile = QStringLiteral("auto");
    }
    if (remote_width < 0 || remote_height < 0)
    {
        remote_width = 0;
        remote_height = 0;
    }

    m_commonIni->beginGroup(QStringLiteral("signal_server"));
    wsUrl = m_commonIni->value(QStringLiteral("wsUrl"), "").toString().trimmed();
    m_commonIni->endGroup();

    m_commonIni->beginGroup(QStringLiteral("ice_server"));
    ice_host = m_commonIni->value(QStringLiteral("host"), "").toString().trimmed();
    ice_port = static_cast<uint16_t>(m_commonIni->value(QStringLiteral("port"), 0).toUInt());
    ice_username = m_commonIni->value(QStringLiteral("username"), "").toString();
    ice_password = m_commonIni->value(QStringLiteral("password"), "").toString();
    m_commonIni->endGroup();

    m_commonIni->beginGroup(QStringLiteral("audio"));
    audio_mic_device = m_commonIni->value(QStringLiteral("micDevice"), "").toString().trimmed();
    audio_loopback_device = m_commonIni->value(QStringLiteral("loopbackDevice"), "").toString().trimmed();
    m_commonIni->endGroup();

    const config_util_internal::OpenH264Config openh264Config =
        config_util_internal::readOpenH264Config(
            *m_commonIni, validatedManagedOpenH264Path);
    openh264_enabled = openh264Config.enabled;
    openh264_library_path = openh264Config.libraryPath;

    if (logLevelStr == QStringLiteral("trace"))
        logLevel = spdlog::level::trace;
    else if (logLevelStr == QStringLiteral("debug"))
        logLevel = spdlog::level::debug;
    else if (logLevelStr == QStringLiteral("info"))
        logLevel = spdlog::level::info;
    else if (logLevelStr == QStringLiteral("warn"))
        logLevel = spdlog::level::warn;
    else if (logLevelStr == QStringLiteral("error"))
        logLevel = spdlog::level::err;
    else if (logLevelStr == QStringLiteral("critical"))
        logLevel = spdlog::level::critical;
    else
        logLevel = spdlog::level::info;

    if (fileExists)
        m_commonIni->sync();
}

void ConfigUtilData::applyAutoStartSetting()
{
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    QSettings runKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                     QSettings::NativeFormat);
    constexpr const char *kRunValueName = "AiranDesk";
    const QStringList legacyRunValueNames{
        QStringLiteral("AiRanDesk"),
        QStringLiteral("airan-desk"),
        QStringLiteral("airan")
    };
    if (auto_start)
    {
        for (const QString &name : legacyRunValueNames)
            runKey.remove(name);
        runKey.setValue(QString::fromUtf8(kRunValueName), autoStartCommand());
    }
    else
    {
        runKey.remove(QString::fromUtf8(kRunValueName));
        for (const QString &name : legacyRunValueNames)
            runKey.remove(name);
    }
    runKey.sync();
#elif defined(Q_OS_MACOS)
    const QString launchAgentsDir = QDir::home().filePath(QStringLiteral("Library/LaunchAgents"));
    const QString plistPath = QDir(launchAgentsDir).filePath(QStringLiteral("com.wxalh.airan-desk.plist"));
    if (auto_start)
    {
        QDir().mkpath(launchAgentsDir);
        const QString exe = QCoreApplication::applicationFilePath();
        const QString plist = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                             "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                                             "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                                             "<plist version=\"1.0\">\n"
                                             "<dict>\n"
                                             "  <key>Label</key>\n"
                                             "  <string>com.wxalh.airan-desk</string>\n"
                                             "  <key>ProgramArguments</key>\n"
                                             "  <array>\n"
                                             "    <string>%1</string>\n"
                                             "    <string>--start-in-tray</string>\n"
                                             "    <string>--delaystart</string>\n"
                                             "    <string>5</string>\n"
                                             "  </array>\n"
                                              "  <key>LimitLoadToSessionType</key>\n"
                                              "  <string>Aqua</string>\n"
                                              "  <key>ProcessType</key>\n"
                                              "  <string>Interactive</string>\n"
                                              "  <key>RunAtLoad</key>\n"
                                             "  <true/>\n"
                                             "</dict>\n"
                                             "</plist>\n")
                                  .arg(exe.toHtmlEscaped());
        writeTextFile(plistPath, plist);
    }
    else
    {
        QFile::remove(plistPath);
    }
#else
    const QString autostartDir = QDir::home().filePath(QStringLiteral(".config/autostart"));
    const QString desktopPath = QDir(autostartDir).filePath(QStringLiteral("airan-desk.desktop"));
    if (auto_start)
    {
        QDir().mkpath(autostartDir);
        writeTextFile(desktopPath, autoStartDesktopEntry());
    }
    else
    {
        QFile::remove(desktopPath);
    }
#endif
}
