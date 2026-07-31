#ifndef TERMINAL_LOG_WRITER_H
#define TERMINAL_LOG_WRITER_H

#include <QByteArray>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>

class QTimer;

class TerminalLogWriter : public QObject
{
    Q_OBJECT
public:
    explicit TerminalLogWriter(QObject *parent = nullptr);

    qint64 enqueue(const QByteArray &data);

public slots:
    bool openLog(const QString &path, const QString &remoteId);
    void openLogAsync(const QString &path, const QString &remoteId);
    void appendMarker(const QString &marker);
    void closeLog();
    void closeLogAndFinish();

signals:
    void openFinished(bool ok, const QString &errorMessage);
    void closeFinished();
    void errorOccurred(const QString &message);
    void pendingBytesChanged(qint64 bytes);

private slots:
    void drainInput();
    void flushBufferedOutput();

private:
    enum ParserState
    {
        NormalState,
        EscapeState,
        CsiState,
        OscState,
        OscEscapeState
    };

    void processInput(const QByteArray &data);
    void finishLine();
    void removeLastUtf8CodePoint();
    bool flushOutput();
    void resetParser();

    QFile m_file;
    QTimer *m_flushTimer = nullptr;
    QMutex m_inputMutex;
    QByteArray m_pendingInput;
    QByteArray m_pendingLine;
    QByteArray m_writeBuffer;
    ParserState m_parserState = NormalState;
    bool m_accepting = false;
    bool m_drainScheduled = false;
    bool m_lastWasCr = false;
};

#endif /* TERMINAL_LOG_WRITER_H */
