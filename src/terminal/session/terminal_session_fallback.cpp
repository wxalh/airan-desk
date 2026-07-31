#include "terminal_session.h"

#include <QProcess>


bool TerminalSession::startFallbackProcess()
{
    m_usingFallbackProcess = true;
#if defined(Q_OS_WIN)
    return startWindowsFallbackProcess();
#else
    m_process = new QProcess(this);
    m_process->setProgram(defaultShell());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setInputChannelMode(QProcess::ManagedInputChannel);

    connect(m_process, &QProcess::readyRead, this, &TerminalSession::onFallbackReadyRead);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TerminalSession::onFallbackFinished);
    connect(m_process, QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this, &TerminalSession::onFallbackError);

    m_process->start();
    if (!m_process->waitForStarted(3000))
    {
        emit errorOccurred(m_process->errorString());
        return false;
    }
    return true;
#endif
}


void TerminalSession::onFallbackReadyRead()
{
    if (m_stopping.load() || m_outputPaused.load())
        return;
    if (m_process)
    {
#if defined(Q_OS_WIN)
        const QByteArray data = m_process->readAll();
        logWindowsTerminalBytes(data, QStringLiteral("pipe"));
        emit outputReady(decodeWindowsOutput(data.constData(), data.size()));
#else
        emit outputReady(m_process->readAll());
#endif
    }
}


void TerminalSession::onFallbackFinished(int exitCode, QProcess::ExitStatus)
{
    emitClosedOnce(exitCode);
}


void TerminalSession::onFallbackError(QProcess::ProcessError)
{
    emit errorOccurred(m_process ? m_process->errorString() : QStringLiteral("Terminal process error"));
}


