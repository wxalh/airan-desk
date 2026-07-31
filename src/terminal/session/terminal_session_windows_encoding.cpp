#include "terminal_session.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringDecoder>
#else
#include <QTextCodec>
#endif

#if defined(Q_OS_WIN)

QByteArray TerminalSession::decodeWindowsOutput(const char *data, int size)
{
    QByteArray bytes = m_windowsUtf8DecodePending + QByteArray(data, size);
    m_windowsUtf8DecodePending.clear();

    const int pendingUtf8Bytes = incompleteUtf8TailLength(bytes);
    const QByteArray utf8Candidate = pendingUtf8Bytes > 0
                                         ? bytes.left(bytes.size() - pendingUtf8Bytes)
                                         : bytes;

    if (!hasInvalidUtf8(utf8Candidate))
    {
        if (pendingUtf8Bytes > 0)
            m_windowsUtf8DecodePending = bytes.right(pendingUtf8Bytes);
        return utf8Candidate;
    }

    m_windowsUtf8DecodePending.clear();
    return decodeWindowsConsoleBytes(bytes);
}


QByteArray TerminalSession::decodeWindowsConsoleBytes(const QByteArray &data)
{
    QByteArray bytes = m_windowsConsoleDecodePending + data;
    m_windowsConsoleDecodePending.clear();

    const UINT codePage = GetOEMCP();
    if (!bytes.isEmpty() && IsDBCSLeadByteEx(codePage, static_cast<BYTE>(bytes.at(bytes.size() - 1))))
    {
        m_windowsConsoleDecodePending.append(bytes.at(bytes.size() - 1));
        bytes.chop(1);
    }

    if (bytes.isEmpty())
    {
        return QByteArray();
    }

    const int wideSize = MultiByteToWideChar(codePage, 0, bytes.constData(), bytes.size(), nullptr, 0);
    if (wideSize <= 0)
    {
        return QString::fromLocal8Bit(bytes).toUtf8();
    }

    std::wstring wide(static_cast<size_t>(wideSize), L'\0');
    MultiByteToWideChar(codePage, 0, bytes.constData(), bytes.size(), &wide[0], wideSize);
    return QString::fromWCharArray(wide.data(), wideSize).toUtf8();
}


QByteArray TerminalSession::encodeWindowsConsoleBytes(const QString &text) const
{
    if (text.isEmpty())
    {
        return QByteArray();
    }

    const UINT codePage = GetOEMCP();
    const std::wstring wide = text.toStdWString();
    const int byteSize = WideCharToMultiByte(codePage, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (byteSize <= 0)
    {
        return text.toLocal8Bit();
    }

    QByteArray bytes(byteSize, Qt::Uninitialized);
    WideCharToMultiByte(codePage, 0, wide.data(), static_cast<int>(wide.size()), bytes.data(), byteSize, nullptr, nullptr);
    return bytes;
}


bool TerminalSession::hasInvalidUtf8(const QByteArray &data) const
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QTextCodec::ConverterState state;
    QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
    if (!utf8)
    {
        return false;
    }
    utf8->toUnicode(data.constData(), data.size(), &state);
    if (state.invalidChars > 0)
    {
        return true;
    }
#else
    QStringDecoder decoder(QStringDecoder::Utf8);
    decoder(data);
    if (decoder.hasError())
    {
        return true;
    }
#endif
    if (!data.isEmpty())
    {
        const unsigned char last = static_cast<unsigned char>(data.at(data.size() - 1));
        return last >= 0xC2 && last <= 0xF4;
    }
    return false;
}


int TerminalSession::incompleteUtf8TailLength(const QByteArray &data) const
{
    if (data.isEmpty())
        return 0;

    int leadIndex = data.size() - 1;
    while (leadIndex >= 0)
    {
        const unsigned char byte = static_cast<unsigned char>(data.at(leadIndex));
        if (byte < 0x80 || byte > 0xBF)
            break;
        --leadIndex;
    }

    if (leadIndex < 0)
        return 0;

    const unsigned char lead = static_cast<unsigned char>(data.at(leadIndex));
    int expectedBytes = 0;
    if (lead >= 0xC2 && lead <= 0xDF)
        expectedBytes = 2;
    else if (lead >= 0xE0 && lead <= 0xEF)
        expectedBytes = 3;
    else if (lead >= 0xF0 && lead <= 0xF4)
        expectedBytes = 4;
    else
        return 0;

    const int availableBytes = data.size() - leadIndex;
    if (availableBytes < expectedBytes)
        return availableBytes;
    return 0;
}
#endif
