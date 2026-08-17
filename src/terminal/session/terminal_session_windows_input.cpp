#include "terminal_session.h"

#include "common/logger_manager.h"

#include <QTimer>
#include <system_error>

#if defined(Q_OS_WIN)

namespace
{
constexpr int kMaxWindowsWriterPendingBytes = 1024 * 1024;
constexpr int kWindowsInputWriteChunkBytes = 4096;
}

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


bool TerminalSession::enqueueWindowsInput(const QByteArray &data)
{
    if (data.isEmpty())
        return true;

    std::lock_guard<std::mutex> lock(m_writerMutex);
    if (!m_writerRunning.load() || !m_inputWrite ||
        data.size() > kMaxWindowsWriterPendingBytes - m_writerPendingInput.size())
        return false;
    m_writerPendingInput.append(data);
    m_writerCondition.notify_one();
    return true;
}


void TerminalSession::startWindowsInputWriter(quint64 generation)
{
    {
        std::lock_guard<std::mutex> lock(m_writerMutex);
        m_writerPendingInput.clear();
        m_writerRunning.store(true);
    }
    try
    {
        m_writerThread = std::thread(&TerminalSession::writerLoop, this, generation);
    }
    catch (const std::system_error &error)
    {
        std::lock_guard<std::mutex> lock(m_writerMutex);
        m_writerPendingInput.clear();
        m_writerRunning.store(false);
        LOG_ERROR("Failed to create Windows terminal input writer: generation={}, error={}",
                  generation,
                  error.what());
        throw;
    }
}


void TerminalSession::writerLoop(quint64 generation)
{
    for (;;)
    {
        QByteArray chunk;
        {
            std::unique_lock<std::mutex> lock(m_writerMutex);
            m_writerCondition.wait(lock, [this]() {
                return !m_writerRunning.load() || !m_writerPendingInput.isEmpty();
            });
            if (!m_writerRunning.load())
                return;
            chunk = m_writerPendingInput.left(qMin(kWindowsInputWriteChunkBytes,
                                                   m_writerPendingInput.size()));
        }

        DWORD written = 0;
        const BOOL ok = WriteFile(m_inputWrite,
                                  chunk.constData(),
                                  static_cast<DWORD>(chunk.size()),
                                  &written,
                                  nullptr);
        if (!ok || written == 0)
        {
            const DWORD error = ok ? ERROR_WRITE_FAULT : GetLastError();
            {
                std::lock_guard<std::mutex> lock(m_writerMutex);
                m_writerPendingInput.clear();
                m_writerRunning.store(false);
            }
            if (!m_stopping.load())
            {
                LOG_WARN("Windows terminal input writer failed: generation={}, error={}",
                         generation,
                         static_cast<int>(error));
                emit errorOccurred(QStringLiteral("Failed to write terminal input"));
            }
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_writerMutex);
            m_writerPendingInput.remove(0, qMin(static_cast<int>(written),
                                                m_writerPendingInput.size()));
        }
    }
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
    if (!enqueueWindowsInput(input))
    {
        if (!m_stopping.load())
        {
            LOG_ERROR("Rejected ConPTY input because the writer queue is unavailable: size={}",
                      input.size());
            emit errorOccurred(QStringLiteral("Terminal input buffer is full"));
        }
    }
    else
    {
        LOG_TRACE("Terminal ConPTY input queued for writer: size={}", input.size());
    }
}
#endif
