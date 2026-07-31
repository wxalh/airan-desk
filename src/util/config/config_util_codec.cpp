#include "util/config/config_util_codec.h"

#include <QSettings>

namespace config_util_internal
{
namespace
{

QString managedPath(const QString &candidate,
                    const OpenH264PathValidator &validateManagedPath)
{
    const QString trimmed = candidate.trimmed();
    if (trimmed.isEmpty() || !validateManagedPath)
        return QString();
    return validateManagedPath(trimmed).trimmed();
}
} // namespace

OpenH264Config readOpenH264Config(QSettings &settings,
                                  const OpenH264PathValidator &validateManagedPath)
{
    settings.beginGroup(QStringLiteral("codec"));
    OpenH264Config config;
    const bool requestedEnabled =
        settings.value(QStringLiteral("openh264Enabled"), false).toBool();
    const QString requestedPath =
        settings.value(QStringLiteral("openh264Library"), QString()).toString().trimmed();
    config.libraryPath = managedPath(requestedPath, validateManagedPath);
    config.enabled = requestedEnabled && !config.libraryPath.isEmpty();
    if (config.enabled != requestedEnabled || config.libraryPath != requestedPath)
    {
        settings.setValue(QStringLiteral("openh264Enabled"), config.enabled);
        settings.setValue(QStringLiteral("openh264Library"), config.libraryPath);
    }
    settings.endGroup();
    settings.sync();
    return config;
}

bool writeOpenH264Config(QSettings &settings,
                         const OpenH264Config &config,
                         const OpenH264PathValidator &validateManagedPath)
{
    const QString libraryPath = managedPath(config.libraryPath, validateManagedPath);
    settings.beginGroup(QStringLiteral("codec"));
    settings.setValue(QStringLiteral("openh264Enabled"),
                      config.enabled && !libraryPath.isEmpty());
    settings.setValue(QStringLiteral("openh264Library"), libraryPath);
    settings.endGroup();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

} // namespace config_util_internal
