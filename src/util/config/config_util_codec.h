#pragma once

#include <QString>

#include <functional>

class QSettings;

namespace config_util_internal
{

struct OpenH264Config
{
    bool enabled{false};
    QString libraryPath;
};

using OpenH264PathValidator = std::function<QString(const QString &candidatePath)>;

OpenH264Config readOpenH264Config(QSettings &settings,
                                  const OpenH264PathValidator &validateManagedPath);
bool writeOpenH264Config(QSettings &settings,
                         const OpenH264Config &config,
                         const OpenH264PathValidator &validateManagedPath);

} // namespace config_util_internal
