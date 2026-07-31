#include "terminal_session.h"

#include "common/logger_manager.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

#if !defined(Q_OS_WIN)
#include <signal.h>
#include <unistd.h>
#endif


TerminalSession::TerminalSession(QObject *parent)
    : QObject(parent)
{
#if defined(Q_OS_WIN)
    m_windowsInputFlushTimer = new QTimer(this);
    m_windowsInputFlushTimer->setSingleShot(true);
    connect(m_windowsInputFlushTimer, &QTimer::timeout, this, &TerminalSession::flushWindowsPendingInput);
#endif
}


TerminalSession::~TerminalSession()
{
    stop();
}


QString TerminalSession::defaultShell() const
{
#if defined(Q_OS_WIN)
    const QString comspec = QString::fromLocal8Bit(qgetenv("ComSpec"));
    return comspec.isEmpty() ? QStringLiteral("C:/Windows/System32/cmd.exe") : QDir::fromNativeSeparators(comspec);
#else
    QString shell = QString::fromLocal8Bit(qgetenv("SHELL")).trimmed();
    if (!shell.isEmpty() && !QFileInfo(shell).isAbsolute())
        shell = QStandardPaths::findExecutable(shell);
    if (!shell.isEmpty() && QFileInfo(shell).isExecutable())
        return shell;
#if defined(Q_OS_MACOS)
    const QStringList candidates{QStringLiteral("/bin/zsh"), QStringLiteral("/bin/sh")};
#else
    const QStringList candidates{QStringLiteral("/bin/bash"), QStringLiteral("/bin/sh")};
#endif
    for (const QString &candidate : candidates)
    {
        if (QFileInfo(candidate).isExecutable())
            return candidate;
    }
    return QStringLiteral("/bin/sh");
#endif
}


void TerminalSession::start(int cols, int rows)
{
    stop();
#if !defined(Q_OS_WIN)
    m_activeShellPath.clear();
#endif
    m_closedEmitted.store(false);
    m_stopping.store(false);
    m_outputPaused.store(false);
#if defined(Q_OS_WIN)
    m_windowsConsoleDecodePending.clear();
    m_windowsUtf8DecodePending.clear();
    m_windowsOutputLogCount = 0;
#endif
    const int safeCols = qMax(20, cols);
    const int safeRows = qMax(5, rows);
#if defined(Q_OS_WIN)
    if (startPty(safeCols, safeRows))
    {
        LOG_INFO("Terminal started with Windows ConPTY mode");
        emitTerminalInfo(QStringLiteral("pty"), true);
        return;
    }

    LOG_WARN("Windows ConPTY is not available; starting plain shell fallback");
    if (startFallbackProcess())
    {
        LOG_INFO("Terminal started with Windows pipe fallback mode");
        emitTerminalInfo(QStringLiteral("pipe"), true);
        emit outputReady(QStringLiteral("\r\n[Windows 7 compatibility terminal]\r\n").toUtf8());
    }
#else
    m_pendingInput.clear();
    if (startPty(safeCols, safeRows))
    {
        emitTerminalInfo(QStringLiteral("pty"), true);
    }
    else
    {
        emit errorOccurred(QStringLiteral("Pseudo terminal is not available on this system"));
    }
#endif
}


void TerminalSession::setOutputPaused(bool paused)
{
    if (m_outputPaused.exchange(paused) == paused)
        return;
#if !defined(Q_OS_WIN)
    if (m_notifier)
        m_notifier->setEnabled(!paused);
    if (!paused && m_notifier)
        QTimer::singleShot(0, this, &TerminalSession::onPtyReadyRead);
#endif
    if (!paused && m_process)
        QTimer::singleShot(0, this, &TerminalSession::onFallbackReadyRead);
}


void TerminalSession::emitTerminalInfo(const QString &mode, bool pathTracking)
{
    QString osName;
#if defined(Q_OS_WIN)
    osName = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    osName = QStringLiteral("macos");
#else
    osName = QStringLiteral("linux");
#endif
    const QString shellPath = m_activeShellPath.isEmpty() ? defaultShell() : m_activeShellPath;
#if !defined(Q_OS_WIN)
    const QString shellName = QFileInfo(shellPath).fileName().toLower();
    pathTracking = pathTracking && shellName == QStringLiteral("bash");
#endif
    emit terminalInfoReady(osName, shellPath, mode, pathTracking);
}


void TerminalSession::emitClosedOnce(int exitCode)
{
    if (m_closedEmitted.exchange(true))
        return;
    emit closed(exitCode);
}


void TerminalSession::stop()
{
    m_stopping.store(true);
    m_closedEmitted.store(true);
    m_outputPaused.store(false);
    if (m_process)
    {
        m_process->terminate();
        if (!m_process->waitForFinished(1000))
            m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_usingFallbackProcess = false;

#if defined(Q_OS_WIN)
    if (m_windowsInputFlushTimer)
        m_windowsInputFlushTimer->stop();
    m_windowsPendingInput.clear();
    closeConPty();
#else
    if (m_notifier)
    {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_writeNotifier)
    {
        m_writeNotifier->setEnabled(false);
        m_writeNotifier->deleteLater();
        m_writeNotifier = nullptr;
    }
    m_pendingInput.clear();
    if (m_masterFd >= 0)
    {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
    if (m_childPid > 0)
    {
        ::kill(static_cast<pid_t>(m_childPid), SIGHUP);
        reapChildProcessNonBlocking();
    }
#endif
}


