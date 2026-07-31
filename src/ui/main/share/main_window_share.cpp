#include "ui/main/main_window.h"

#include <QLineEdit>
#include <QRegularExpression>
#include <QSignalBlocker>

namespace
{

QString firstCapture(const QString &text, const QStringList &patterns)
{
    for (const QString &pattern : patterns)
    {
        QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = regex.match(text);
        if (match.hasMatch())
            return match.captured(1).trimmed();
    }
    return QString();
}


bool looksLikeLocalIni(const QString &text)
{
    return text.contains(QStringLiteral("[local]"), Qt::CaseInsensitive) ||
           text.contains(QStringLiteral("local_id"), Qt::CaseInsensitive) ||
           text.contains(QStringLiteral("local_pwd"), Qt::CaseInsensitive);
}
} // namespace


bool MainWindow::tryFillRemoteFieldsFromShareText(const QString &text)
{
    if (m_remoteShareParsing)
        return false;

    const QString normalized = text.trimmed();
    if (normalized.size() < 8)
        return false;

    QString remoteId;
    QString remotePwd;
    if (looksLikeLocalIni(normalized))
    {
        remoteId = firstCapture(normalized, {
            QStringLiteral("(?:^|[\\r\\n])\\s*local_id\\s*=\\s*([A-Za-z0-9][A-Za-z0-9_\\-{}]{5,})"),
        });
        remotePwd = firstCapture(normalized, {
            QStringLiteral("(?:^|[\\r\\n])\\s*local_pwd\\s*=\\s*([A-Za-z0-9][A-Za-z0-9_\\-{}]{3,})"),
        });
    }

    if (remoteId.isEmpty())
        remoteId = firstCapture(normalized, {
        QStringLiteral("(?:\\x{8BC6}\\x{522B}\\x{7801}|\\x{8FDC}\\x{7AEF}\\x{8BC6}\\x{522B}\\x{7801}|\\x{8BBE}\\x{5907}\\x{7801}|Remote ID|ID|id|sessionId|remoteId|deviceId)\\s*[:\\x{FF1A}=]?\\s*([A-Za-z0-9][A-Za-z0-9_\\-{}]{5,})"),
        QStringLiteral("(?:sessionId|remoteId|deviceId)\\s*=\\s*([A-Za-z0-9][A-Za-z0-9_\\-{}]{5,})"),
        });
    if (remotePwd.isEmpty())
        remotePwd = firstCapture(normalized, {
        QStringLiteral("(?:\\x{9A8C}\\x{8BC1}\\x{7801}|\\x{8FDC}\\x{7AEF}\\x{9A8C}\\x{8BC1}\\x{7801}|\\x{5BC6}\\x{7801}|Remote verification code|Password|password|pwd|code)\\s*[:\\x{FF1A}=]?\\s*([A-Za-z0-9][A-Za-z0-9_\\-{}]{3,})"),
        QStringLiteral("(?:remotePwd|pwd|password|code)\\s*=\\s*([A-Za-z0-9][A-Za-z0-9_\\-{}]{3,})"),
        });

    if (remoteId.isEmpty() || remotePwd.isEmpty())
        return false;

    m_remoteShareParsing = true;
    const QSignalBlocker idBlocker(m_remoteIdEdit);
    const QSignalBlocker pwdBlocker(m_remotePwdEdit);
    m_remoteIdEdit->setText(remoteId.remove(QChar('{')).remove(QChar('}')).trimmed());
    m_remotePwdEdit->setText(remotePwd.remove(QChar('{')).remove(QChar('}')).trimmed());
    m_remotePwdEdit->setFocus();
    m_remoteShareParsing = false;
    LOG_INFO("Parsed remote share text into remote id and password");
    return true;
}
