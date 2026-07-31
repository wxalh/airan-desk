#include "terminal_session.h"

#include "common/logger_manager.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringDecoder>
#else
#include <QTextCodec>
#endif

#if defined(Q_OS_WIN)

void TerminalSession::logWindowsTerminalBytes(const QByteArray &data, const QString &source)
{
    if (data.isEmpty())
    {
        return;
    }

    bool hasHighBitByte = false;
    for (char ch : data)
    {
        if (static_cast<unsigned char>(ch) >= 0x80)
        {
            hasHighBitByte = true;
            break;
        }
    }

    if (m_windowsOutputLogCount >= 20 && !hasHighBitByte)
    {
        return;
    }
    ++m_windowsOutputLogCount;

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QTextCodec::ConverterState utf8State;
    QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
    if (utf8)
        utf8->toUnicode(data.constData(), data.size(), &utf8State);
    const int invalidChars = utf8State.invalidChars;
#else
    QStringDecoder decoder(QStringDecoder::Utf8);
    decoder(data);
    const int invalidChars = decoder.hasError() ? 1 : 0;
#endif

    LOG_TRACE("Terminal Windows output sample source={}, size={}, utf8Invalid={}, selected={}",
              source,
              data.size(),
              invalidChars,
              invalidChars > 0 ? QStringLiteral("local") : QStringLiteral("utf8"));
}
#endif
