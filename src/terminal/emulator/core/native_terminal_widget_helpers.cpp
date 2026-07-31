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
    const QUrl url(text);
    if (url.scheme() == QStringLiteral("file"))
    {
        QString path;
        const QString host = url.host().toLower();
        if (host.isEmpty() || host == QStringLiteral("localhost"))
            path = QUrl::fromPercentEncoding(url.path().toUtf8());
        else
            path = url.toLocalFile();

        if (!path.isEmpty())
        {
            if (path.size() >= 3 && path.at(0) == QLatin1Char('/') && path.at(2) == QLatin1Char(':'))
                path.remove(0, 1);
            return QDir::fromNativeSeparators(path);
        }
    }

    const QString prefix = QStringLiteral("file:///");
    if (text.startsWith(prefix, Qt::CaseInsensitive))
    {
        QString path = QUrl::fromPercentEncoding(text.mid(prefix.size()).toUtf8());
        if (path.size() >= 2 && path.at(1) == QLatin1Char(':'))
            return QDir::fromNativeSeparators(path);
        return QDir::fromNativeSeparators(QStringLiteral("/") + path);
    }
    return QString();
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
