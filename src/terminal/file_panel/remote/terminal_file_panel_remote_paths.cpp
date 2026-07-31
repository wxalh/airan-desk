#include "terminal/file_panel/terminal_file_panel.h"

#include <QDir>


QString TerminalFilePanel::joinRemotePath(const QString &basePath, const QString &name) const
{
    if (basePath.endsWith('/') || basePath.endsWith('\\'))
        return basePath + name;

    const QChar separator = basePath.contains('\\') && !basePath.contains('/') ? QLatin1Char('\\') : QLatin1Char('/');
    return basePath + separator + name;
}


QString TerminalFilePanel::parentRemotePath(const QString &path) const
{
    const QString normalized = QDir::cleanPath(path);
    const bool windowsDriveRoot = normalized.size() == 3 &&
                                  normalized.at(1) == QLatin1Char(':') &&
                                  (normalized.at(2) == QLatin1Char('/') || normalized.at(2) == QLatin1Char('\\'));
    if (windowsDriveRoot)
        return normalized;

    const bool windowsDrivePath = normalized.size() >= 3 &&
                                  normalized.at(1) == QLatin1Char(':') &&
                                  (normalized.at(2) == QLatin1Char('/') || normalized.at(2) == QLatin1Char('\\'));
    const int slash = normalized.lastIndexOf('/');
    const int backslash = normalized.lastIndexOf('\\');
    const int index = qMax(slash, backslash);
    if (windowsDrivePath && index <= 2)
        return normalized.left(3);
    if (index <= 0)
        return slash == 0 ? QStringLiteral("/") : normalized;
    return normalized.left(index);
}
