#ifndef CONFIG_UTIL_PATHS_H
#define CONFIG_UTIL_PATHS_H

#include <QString>

namespace config_util_internal
{


void configureDefaultSettingsPath();

QString selectWritableConfFile(const QString &runtimeDir,
                               const QString &userDir,
                               const QString &fileName);

QString selectWritableDirectory(const QString &runtimeDir, const QString &userDir);

QString writableConfFile(const QString &fileName, bool copyDefault);


QString writableLogDir();

} /* namespace config_util_internal */

#endif /* CONFIG_UTIL_PATHS_H */
