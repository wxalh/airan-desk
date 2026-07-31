#ifndef CONFIG_SETTINGS_WRITER_H
#define CONFIG_SETTINGS_WRITER_H

#include <QSettings>
#include <QString>

#include <functional>

namespace config_util_internal
{
using IniWriter = std::function<void(QSettings &)>;

QSettings::Status writeIniFile(const QString &path, const IniWriter &writer);
}

#endif /* CONFIG_SETTINGS_WRITER_H */
