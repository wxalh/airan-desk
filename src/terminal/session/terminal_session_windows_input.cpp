#include "terminal_session.h"

#include "common/logger_manager.h"

#include <QTimer>

#if defined(Q_OS_WIN)

QByteArray TerminalSession::normalizeFallbackInput(const QByteArray &data) const
{
    QByteArray normalized;
    normalized.reserve(data.size() + 8);
    for (int i = 0; i < data.size(); ++i)
    {
        const char ch = data.at(i);
        if (ch == '\r')
        {
            if (i + 1 < data.size() && data.at(i + 1) == '\n')
                ++i;
            normalized.append("\r\n");
        }
        else if (ch == '\n')
        {
            normalized.append("\r\n");
        }
        else if (static_cast<unsigned char>(ch) == 0x7f)
        {
            normalized.append('\b');
        }
        else
        {
            normalized.append(ch);
        }
    }
    return encodeWindowsConsoleBytes(QString::fromUtf8(normalized));
}


QByteArray TerminalSession::normalizeWindowsInput(const QByteArray &data) const
{
    QByteArray normalized;
    normalized.reserve(data.size() + 8);
    for (int i = 0; i < data.size(); ++i)
    {
        const char ch = data.at(i);
        if (ch == '\r')
        {
            if (i + 1 < data.size() && data.at(i + 1) == '\n')
                ++i;
            normalized.append('\r');
        }
        else if (ch == '\n')
        {
            normalized.append('\r');
        }
        else if (static_cast<unsigned char>(ch) == 0x7f)
        {
            normalized.append('\b');
        }
        else
        {
            normalized.append(ch);
        }
    }
    return normalized;
}


DWORD TerminalSession::writeWindowsConPtyInput(const QByteArray &data)
{
    if (!m_inputWrite || data.isEmpty())
        return 0;

    DWORD written = 0;
    if (!WriteFile(m_inputWrite, data.constData(), static_cast<DWORD>(data.size()), &written, nullptr))
    {
        LOG_WARN("Failed to write terminal input to ConPTY: error={}", static_cast<int>(GetLastError()));
        return 0;
    }
    return written;
}


bool TerminalSession::windowsInputNeedsImmediateFlush(const QByteArray &data) const
{
    for (char ch : data)
    {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 0x20 || byte == 0x7f)
            return true;
    }
    return false;
}


void TerminalSession::flushWindowsPendingInput()
{
    if (m_windowsInputFlushTimer)
        m_windowsInputFlushTimer->stop();
    if (m_windowsPendingInput.isEmpty() || !m_inputWrite)
        return;

    const QByteArray input = m_windowsPendingInput;
    m_windowsPendingInput.clear();
    const DWORD written = writeWindowsConPtyInput(input);
    if (written > 0 && written < static_cast<DWORD>(input.size()))
    {
        m_windowsPendingInput.prepend(input.mid(static_cast<int>(written)));
        if (m_windowsInputFlushTimer)
            m_windowsInputFlushTimer->start(10);
    }
    LOG_TRACE("Terminal ConPTY input written: size={}, written={}", input.size(), written);
}
#endif
