#include "terminal/emulator/native_terminal_widget.h"

#include <QDir>
#include <QFontDatabase>
#include <QUrl>


QString NativeTerminalWidget::preferredTerminalFontFamily()
{
#if defined(Q_OS_WIN)
    const QFontDatabase database;
    const QStringList families = database.families();
    const QString candidates[] = {
        QStringLiteral("Microsoft YaHei Mono"),
        QStringLiteral("NSimSun"),
        QStringLiteral("SimSun"),
        QStringLiteral("Consolas")};
    for (const QString &candidate : candidates)
    {
        if (families.contains(candidate))
            return candidate;
    }
#endif
    return QString();
}


QString NativeTerminalWidget::pathFromOsc7Payload(const QByteArray &payload)
{
    const QString text = QString::fromUtf8(payload);
    if (!text.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive))
        return QString();

    const QString remainder = text.mid(7);
    const int slash = remainder.indexOf(QLatin1Char('/'));
    if (slash < 0)
        return QString();

    const QString host = remainder.left(slash);
    const QString rawPath = remainder.mid(slash);
    QString path = host.isEmpty()
                       ? rawPath
                       : QUrl::fromPercentEncoding(rawPath.toUtf8());
    if (!host.isEmpty() && host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) != 0)
        path = QStringLiteral("//") + host + path;
    const bool leadingSlashBeforeUnc = path.size() >= 3 && path.at(0) == QLatin1Char('/') &&
                                       ((path.at(1) == QLatin1Char('/') && path.at(2) == QLatin1Char('/')) ||
                                        (path.at(1) == QLatin1Char('\\') && path.at(2) == QLatin1Char('\\')));
    if (leadingSlashBeforeUnc ||
        (path.size() >= 3 && path.at(0) == QLatin1Char('/') && path.at(2) == QLatin1Char(':')))
        path.remove(0, 1);
    return QDir::fromNativeSeparators(path);
}


bool NativeTerminalWidget::isWideCharTrailingCell(const VTermScreenCell &cell)
{
    return cell.chars[0] == static_cast<uint32_t>(-1);
}


bool NativeTerminalWidget::isCellBefore(const QPoint &left, const QPoint &right)
{
    if (left.y() != right.y())
        return left.y() < right.y();
    return left.x() < right.x();
}


QString NativeTerminalWidget::trimTrailingSpaces(QString text)
{
    while (!text.isEmpty() && text.at(text.size() - 1).isSpace())
        text.chop(1);
    return text;
}
