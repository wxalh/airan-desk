#ifndef TERMINAL_SESSION_H
#define TERMINAL_SESSION_H

#include <QObject>
#include <QByteArray>
#include <QProcess>
#include <QSocketNotifier>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class QTimer;

#if defined(Q_OS_WIN)
#include <windows.h>
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif
#endif

class TerminalSession : public QObject
{
    Q_OBJECT
public:
    explicit TerminalSession(QObject *parent = nullptr);
    ~TerminalSession();

public slots:
    void start(int cols = 80, int rows = 24, const QString &requestedMode = QString());
    void writeInput(const QByteArray &data);
    void resize(int cols, int rows);
    void setOutputPaused(bool paused);
    void stop();

signals:
    void outputReady(const QByteArray &data);
    void closed(int exitCode);
    void errorOccurred(const QString &message);
    void terminalInfoReady(const QString &osName, const QString &shellPath, const QString &mode, bool pathTracking);

private:
    bool startPty(int cols, int rows);
    bool startFallbackProcess();
    QString defaultShell() const;
    void emitClosedOnce(int exitCode);
    void reapChildProcessNonBlocking();
    void emitTerminalInfo(const QString &mode, bool pathTracking);
#if defined(Q_OS_WIN)
    QString windowsConPtyShellNativeArguments() const;
    QString windowsFallbackShellNativeArguments() const;
    bool startWindowsFallbackProcess();
    QByteArray normalizeWindowsInput(const QByteArray &data) const;
    QByteArray normalizeFallbackInput(const QByteArray &data) const;
    bool enqueueWindowsInput(const QByteArray &data);
    void startWindowsInputWriter(quint64 generation);
    void stopWindowsInputWriter();
    void cancelWriterIo();
    void writerLoop(quint64 generation);
    QByteArray decodeWindowsOutput(const char *data, int size);
    QByteArray decodeWindowsConsoleBytes(const QByteArray &data);
    QByteArray encodeWindowsConsoleBytes(const QString &text) const;
    bool hasInvalidUtf8(const QByteArray &data) const;
    int incompleteUtf8TailLength(const QByteArray &data) const;
    void logWindowsTerminalBytes(const QByteArray &data, const QString &source);
    bool windowsInputNeedsImmediateFlush(const QByteArray &data) const;
    void flushWindowsPendingInput();
#endif

private slots:
    void onFallbackReadyRead();
    void onFallbackFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onFallbackError(QProcess::ProcessError error);
#if defined(Q_OS_WIN)
    void finalizeWindowsProcessExit(quint64 generation, int exitCode);
#endif
#if !defined(Q_OS_WIN)
    void onPtyReadyRead();
    void flushPtyInput();
#endif

private:
    QProcess *m_process = nullptr;
    bool m_usingFallbackProcess = false;
    std::atomic_bool m_closedEmitted{false};
    std::atomic_bool m_stopping{false};
    std::atomic_bool m_outputPaused{false};
    QString m_activeShellPath;

#if defined(Q_OS_WIN)
    void closeConPty();
    void cancelReaderIo();
    void readerLoop(quint64 generation);

    HPCON m_hpc = nullptr;
    HANDLE m_inputWrite = nullptr;
    HANDLE m_outputRead = nullptr;
    HANDLE m_processHandle = nullptr;
    std::thread m_readerThread;
    std::atomic_bool m_readerRunning{false};
    std::thread m_writerThread;
    std::atomic_bool m_writerRunning{false};
    std::mutex m_writerMutex;
    std::condition_variable m_writerCondition;
    QByteArray m_writerPendingInput;
    quint64 m_windowsSessionGeneration = 0;
    QByteArray m_windowsUtf8DecodePending;
    QByteArray m_windowsConsoleDecodePending;
    QByteArray m_windowsPendingInput;
    QTimer *m_windowsInputFlushTimer = nullptr;
    int m_windowsOutputLogCount = 0;
#else
    int m_masterFd = -1;
    qint64 m_childPid = -1;
    QSocketNotifier *m_notifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QByteArray m_pendingInput;
#endif
};

#endif /* TERMINAL_SESSION_H */
