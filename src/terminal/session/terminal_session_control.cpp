#include "terminal_session.h"
#include "common/logger_manager.h"

#include <QTimer>

#if !defined(Q_OS_WIN)
#include <cerrno>
#include <cstring>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace
{
constexpr int kMaxWindowsPendingInputBytes = 1024 * 1024;
}

void TerminalSession::writeInput(const QByteArray &data)
{
    if (data.isEmpty())
        return;

    LOG_TRACE("Terminal write input requested: size={}", data.size());

#if defined(Q_OS_WIN)
    if (m_usingFallbackProcess && m_inputWrite)
    {
        const QByteArray normalized = normalizeFallbackInput(data);
        if (!enqueueWindowsInput(normalized) && !m_stopping.load())
        {
            LOG_ERROR("Rejected Windows fallback input because the writer queue is unavailable: size={}",
                      normalized.size());
            emit errorOccurred(QStringLiteral("Terminal input buffer is full"));
        }
        LOG_TRACE("Terminal Windows fallback input queued: size={}", normalized.size());
        return;
    }
#else
    if (m_usingFallbackProcess && m_process)
    {
        m_process->write(data);
        return;
    }
#endif

#if defined(Q_OS_WIN)
    if (m_inputWrite)
    {
        const QByteArray localInput = normalizeWindowsInput(data);
        if (localInput.isEmpty())
            return;

        if (localInput.size() > kMaxWindowsPendingInputBytes - m_windowsPendingInput.size())
        {
            LOG_ERROR("Rejected terminal input because the ConPTY pending buffer is full: pendingSize={}, inputSize={}",
                      m_windowsPendingInput.size(), localInput.size());
            emit errorOccurred(QStringLiteral("Terminal input buffer is full"));
            return;
        }
        m_windowsPendingInput.append(localInput);
        if (windowsInputNeedsImmediateFlush(localInput))
        {
            flushWindowsPendingInput();
        }
        else
        {
            if (m_windowsInputFlushTimer)
                m_windowsInputFlushTimer->start(120);
            LOG_TRACE("Terminal ConPTY input queued: pendingSize={}", m_windowsPendingInput.size());
        }
    }
#else
    if (m_masterFd >= 0)
    {
        m_pendingInput.append(data);
        flushPtyInput();
    }
#endif
}


void TerminalSession::resize(int cols, int rows)
{
    cols = qBound(20, cols, 500);
    rows = qBound(5, rows, 300);

    if (m_usingFallbackProcess)
        return;

#if defined(Q_OS_WIN)
    typedef HRESULT(WINAPI * ResizePseudoConsoleFn)(HPCON, COORD);
    static ResizePseudoConsoleFn resizePseudoConsole =
        reinterpret_cast<ResizePseudoConsoleFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ResizePseudoConsole"));
    if (resizePseudoConsole && m_hpc)
    {
        COORD size{static_cast<SHORT>(cols), static_cast<SHORT>(rows)};
        resizePseudoConsole(m_hpc, size);
    }
#else
    if (m_masterFd >= 0)
    {
        struct winsize ws{};
        ws.ws_col = static_cast<unsigned short>(cols);
        ws.ws_row = static_cast<unsigned short>(rows);
        ioctl(m_masterFd, TIOCSWINSZ, &ws);
    }
#endif
}
