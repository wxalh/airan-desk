#include "config_settings_writer.h"

namespace config_util_internal
{
QSettings::Status writeIniFile(const QString &path, const IniWriter &writer)
{
    QSettings settings(path, QSettings::IniFormat);
    writer(settings);
    settings.sync();
    return settings.status();
}
}
