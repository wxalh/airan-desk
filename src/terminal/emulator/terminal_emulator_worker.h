#ifndef TERMINAL_EMULATOR_WORKER_H
#define TERMINAL_EMULATOR_WORKER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QQueue>
#include <QSize>
#include <QString>
#include <QVector>

#include <deque>

class QTimer;

extern "C"
{
#ifdef small
#undef small
#endif
#include <vterm.h>
}

struct TerminalEmulatorSnapshot
{
    QSize gridSize{80, 24};
    QVector<VTermScreenCell> cells;
    int scrollbackLines = 0;
    quint64 scrollbackSequence = 0;
    QPoint cursorPosition;
    bool cursorVisible = true;
    bool cursorBlink = false;
    int cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
    int mouseMode = VTERM_PROP_MOUSE_NONE;
    bool alternateScreen = false;
    bool focusReport = false;
};

class TerminalEmulatorWorker : public QObject
{
    Q_OBJECT
public:
    explicit TerminalEmulatorWorker(QObject *parent = nullptr);
    ~TerminalEmulatorWorker() override;

public slots:
    void initialize(int cols, int rows);
    void enqueueOutput(const QByteArray &data);
    void enqueueLocalOutput(const QByteArray &data);
    void setGridSize(int cols, int rows);
    void setLocalEchoEnabled(bool enabled);
    void setPipeMode(bool enabled);
    void setPromptEchoFiltering(bool enabled);
    void sendKey(int key, int modifiers);
    void sendText(const QString &text, int modifiers);
    void sendInputBytes(const QByteArray &data);
    void sendClipboardPaste(const QString &text);
    void sendMouseMove(int row, int col, int modifiers);
    void sendMouseButton(int button, bool pressed, int modifiers);
    void clearScreenAndScrollback(bool notifyRemote);
    void shutdown();

signals:
    void snapshotReady(const TerminalEmulatorSnapshot &snapshot);
    void outputConsumed(qint64 bytes);
    void processedOutput(const QByteArray &data);
    void inputGenerated(const QByteArray &data);
    void terminalTitleChanged(const QString &title);
    void currentDirectoryChanged(const QString &path);
    void screenCleared();
    void bell();

private slots:
    void drainOutput();
    void publishSnapshot();

private:
    void initializeTerminal(int cols, int rows);
    struct PendingOutput
    {
        QByteArray data;
        bool remoteOutput = false;
    };

    void processOutput(const QByteArray &data, bool remoteOutput);
    void scheduleOutputDrain();
    void scheduleSnapshot();
    TerminalEmulatorSnapshot buildSnapshot() const;
    VTermScreenCell resolvedCell(VTermScreenCell cell) const;
    VTermScreenCell blankCell() const;
    void handleOutputBytes(const char *data, size_t len);
    QByteArray terminalInputBytesFromText(const QString &text) const;
    QByteArray localEchoBytes(const char *data, size_t len) const;
    QByteArray filterTerminalOutput(const QByteArray &data);
    QByteArray normalizePipeTerminalOutput(const QByteArray &data);
    void parseOsc7Directory(const QByteArray &data);
    void trimScrollback();
    void clearScrollback();
    int maxScrollbackLines() const;
    void flushDamage();

    static void outputCallback(const char *data, size_t len, void *user);
    static int damageCallback(VTermRect rect, void *user);
    static int cursorCallback(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int termPropCallback(VTermProp prop, VTermValue *value, void *user);
    static int bellCallback(void *user);
    static int resizeCallback(int rows, int cols, void *user);
    static int scrollbackPushCallback(int cols, const VTermScreenCell *cells, void *user);
    static int scrollbackPopCallback(int cols, VTermScreenCell *cells, void *user);
    static int scrollbackClearCallback(void *user);

    VTerm *m_vterm = nullptr;
    VTermScreen *m_screen = nullptr;
    QSize m_gridSize{80, 24};
    VTermPos m_cursorPos{0, 0};
    bool m_cursorVisible = true;
    bool m_cursorBlink = false;
    int m_cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
    int m_mouseMode = VTERM_PROP_MOUSE_NONE;
    bool m_alternateScreen = false;
    bool m_focusReport = false;
    bool m_localEchoEnabled = false;
    bool m_pipeMode = false;
    bool m_lastPipeOutputWasCr = false;
    bool m_filterPromptEcho = false;
    QByteArray m_terminalFilterPending;
    QElapsedTimer m_promptEchoFilterTimer;
    QByteArray m_osc7Pending;
    std::deque<QVector<VTermScreenCell>> m_scrollback;
    quint64 m_scrollbackSequence = 0;
    QQueue<PendingOutput> m_pendingOutput;
    bool m_outputDrainScheduled = false;
    bool m_snapshotDirty = false;
    bool m_snapshotBurstActive = false;
    bool m_shuttingDown = false;
    QTimer *m_snapshotTimer = nullptr;
    QElapsedTimer m_snapshotBurstTimer;
};

#endif /* TERMINAL_EMULATOR_WORKER_H */
