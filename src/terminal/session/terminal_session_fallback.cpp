#include "terminal_session.h"

#include <QProcess>


bool TerminalSession::startFallbackProcess()
{
    m_usingFallbackProcess = true;
#if defined(Q_OS_WIN)
    const bool started = startWindowsFallbackProcess();
    if (!started)
        m_usingFallbackProcess = false;
    return started;
#else
    m_process = new QProcess(this);
    m_process->setProgram(defaultShell());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setInputChannelMode(QProcess::ManagedInputChannel);

    connect(m_process, &QProcess::readyRead, this, &TerminalSession::onFallbackReadyRead);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TerminalSession::onFallbackFinished);

    m_process->start();
    if (!m_process->waitForStarted(3000))
    {
        const QString error = m_process->errorString();
        m_process->terminate();
        if (!m_process->waitForFinished(1000))
        {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        m_process->deleteLater();
        m_process = nullptr;
        m_usingFallbackProcess = false;
        emit errorOccurred(error);
        return false;
    }
    connect(m_process, QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this, &TerminalSession::onFallbackError);
    return true;
#endif
}


void TerminalSession::onFallbackReadyRead()
{
    if (sender() && sender() != m_process)
        return;
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
    auto *process = qobject_cast<QProcess *>(sender());
    if (!process || process != m_process)
        return;

    if (!m_stopping.load() && !m_outputPaused.load())
    {
        const QByteArray data = process->readAll();
        if (!data.isEmpty())
            emit outputReady(data);
    }
    m_process = nullptr;
    m_usingFallbackProcess = false;
    process->deleteLater();
    emitClosedOnce(exitCode);
}


void TerminalSession::onFallbackError(QProcess::ProcessError)
{
    if (sender() != m_process)
        return;
    emit errorOccurred(m_process ? m_process->errorString() : QStringLiteral("Terminal process error"));
}


