#include "terminal_log_writer.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutexLocker>
#include <QTimer>

namespace
{
constexpr int kFlushIntervalMs = 250;
constexpr int kFlushThresholdBytes = 64 * 1024;
constexpr int kMaxLogicalLineBytes = 1024 * 1024;
constexpr int kMaxPendingInputBytes = 4 * 1024 * 1024;
}

TerminalLogWriter::TerminalLogWriter(QObject *parent)
    : QObject(parent)
{
}

qint64 TerminalLogWriter::enqueue(const QByteArray &data)
{
    if (data.isEmpty())
        return 0;

    bool scheduleDrain = false;
    bool overflowed = false;
    qint64 pendingBytes = 0;
    {
        QMutexLocker locker(&m_inputMutex);
        if (!m_accepting)
            return 0;
        if (data.size() > kMaxPendingInputBytes - m_pendingInput.size())
        {
            m_accepting = false;
            overflowed = true;
            pendingBytes = m_pendingInput.size();
        }
        else
        {
            m_pendingInput.append(data);
            pendingBytes = m_pendingInput.size();
            if (!m_drainScheduled)
            {
                m_drainScheduled = true;
                scheduleDrain = true;
            }
        }
    }
    if (overflowed)
    {
        emit errorOccurred(QStringLiteral("Terminal log buffer is full"));
        emit pendingBytesChanged(pendingBytes);
        return pendingBytes;
    }
    if (scheduleDrain)
        QMetaObject::invokeMethod(this, "drainInput", Qt::QueuedConnection);
    return pendingBytes;
}

bool TerminalLogWriter::openLog(const QString &path, const QString &remoteId)
{
    closeLog();

    const QFileInfo fileInfo(path);
    QDir dir(fileInfo.absolutePath());
    if ((!dir.exists() && !dir.mkpath(QStringLiteral("."))))
        return false;

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append))
        return false;

    resetParser();
    m_writeBuffer.append("# Airan Desk Terminal Log\n");
    m_writeBuffer.append("# Remote ID: ").append(remoteId.toUtf8()).append('\n');
    m_writeBuffer.append("# Started At: ").append(QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8()).append('\n');
    m_writeBuffer.append("# Encoding: UTF-8\n");
    m_writeBuffer.append("# Format: [local-time] output-line\n\n");

    if (!m_flushTimer)
    {
        m_flushTimer = new QTimer(this);
        m_flushTimer->setInterval(kFlushIntervalMs);
        connect(m_flushTimer, &QTimer::timeout, this, &TerminalLogWriter::flushBufferedOutput);
    }
    m_flushTimer->start();
    {
        QMutexLocker locker(&m_inputMutex);
        m_accepting = true;
    }
    return flushOutput();
}

void TerminalLogWriter::openLogAsync(const QString &path, const QString &remoteId)
{
    const bool ok = openLog(path, remoteId);
    emit openFinished(ok, ok ? QString() : m_file.errorString());
}

void TerminalLogWriter::appendMarker(const QString &marker)
{
    if (!m_file.isOpen())
        return;
    if (!m_pendingLine.isEmpty())
        finishLine();
    m_pendingLine = QByteArrayLiteral("[") + marker.toUtf8() + QByteArrayLiteral("]");
    finishLine();
}

void TerminalLogWriter::closeLog()
{
    QByteArray pending;
    {
        QMutexLocker locker(&m_inputMutex);
        m_accepting = false;
        m_drainScheduled = false;
        pending.swap(m_pendingInput);
    }
    if (!pending.isEmpty())
        processInput(pending);
    if (!m_pendingLine.isEmpty())
        finishLine();

    if (m_flushTimer)
        m_flushTimer->stop();
    if (m_file.isOpen())
    {
        m_writeBuffer.append("\n# Closed At: ")
            .append(QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8())
            .append('\n');
        flushOutput();
        m_file.close();
    }
    resetParser();
}

void TerminalLogWriter::closeLogAndFinish()
{
    closeLog();
    emit closeFinished();
}

void TerminalLogWriter::drainInput()
{
    QByteArray data;
    {
        QMutexLocker locker(&m_inputMutex);
        data.swap(m_pendingInput);
        m_drainScheduled = false;
    }
    processInput(data);

    bool scheduleAgain = false;
    {
        QMutexLocker locker(&m_inputMutex);
        if (m_accepting && !m_pendingInput.isEmpty() && !m_drainScheduled)
        {
            m_drainScheduled = true;
            scheduleAgain = true;
        }
    }
    if (scheduleAgain)
        QMetaObject::invokeMethod(this, "drainInput", Qt::QueuedConnection);

    qint64 pendingBytes = 0;
    {
        QMutexLocker locker(&m_inputMutex);
        pendingBytes = m_pendingInput.size();
    }
    emit pendingBytesChanged(pendingBytes);
}

void TerminalLogWriter::flushBufferedOutput()
{
    flushOutput();
}

void TerminalLogWriter::processInput(const QByteArray &data)
{
    for (const char raw : data)
    {
        const unsigned char ch = static_cast<unsigned char>(raw);
        if (m_parserState == NormalState)
        {
            if (ch == 0x1b)
                m_parserState = EscapeState;
            else if (ch == '\b')
                removeLastUtf8CodePoint();
            else if (ch == '\r')
            {
                finishLine();
                m_lastWasCr = true;
            }
            else if (ch == '\n')
            {
                if (!m_lastWasCr)
                    finishLine();
                m_lastWasCr = false;
            }
            else
            {
                m_lastWasCr = false;
                if (ch == '\t' || ch >= 0x20)
                    m_pendingLine.append(raw);
            }
        }
        else if (m_parserState == EscapeState)
        {
            if (ch == '[')
                m_parserState = CsiState;
            else if (ch == ']')
                m_parserState = OscState;
            else
                m_parserState = NormalState;
        }
        else if (m_parserState == CsiState)
        {
            if (ch >= 0x40 && ch <= 0x7e)
                m_parserState = NormalState;
        }
        else if (m_parserState == OscState)
        {
            if (ch == 0x07)
                m_parserState = NormalState;
            else if (ch == 0x1b)
                m_parserState = OscEscapeState;
        }
        else if (m_parserState == OscEscapeState)
        {
            m_parserState = ch == '\\' ? NormalState : OscState;
        }

        if (m_pendingLine.size() >= kMaxLogicalLineBytes)
            finishLine();
    }
    if (m_writeBuffer.size() >= kFlushThresholdBytes)
        flushOutput();
}

void TerminalLogWriter::finishLine()
{
    if (!m_file.isOpen())
    {
        m_pendingLine.clear();
        return;
    }
    m_writeBuffer.append('[')
        .append(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")).toUtf8())
        .append("] ")
        .append(m_pendingLine)
        .append('\n');
    m_pendingLine.clear();
}

void TerminalLogWriter::removeLastUtf8CodePoint()
{
    if (m_pendingLine.isEmpty())
        return;
    int index = m_pendingLine.size() - 1;
    while (index > 0 && (static_cast<unsigned char>(m_pendingLine.at(index)) & 0xc0) == 0x80)
        --index;
    m_pendingLine.truncate(index);
}

bool TerminalLogWriter::flushOutput()
{
    if (!m_file.isOpen() || m_writeBuffer.isEmpty())
        return m_file.isOpen();
    const qint64 written = m_file.write(m_writeBuffer);
    if (written != m_writeBuffer.size() || !m_file.flush())
    {
        m_writeBuffer.clear();
        if (m_flushTimer)
            m_flushTimer->stop();
        {
            QMutexLocker locker(&m_inputMutex);
            m_accepting = false;
        }
        emit errorOccurred(m_file.errorString());
        m_file.close();
        return false;
    }
    m_writeBuffer.clear();
    return true;
}

void TerminalLogWriter::resetParser()
{
    QMutexLocker locker(&m_inputMutex);
    m_pendingInput.clear();
    m_pendingLine.clear();
    m_writeBuffer.clear();
    m_parserState = NormalState;
    m_drainScheduled = false;
    m_lastWasCr = false;
}
