#ifndef NATIVE_TERMINAL_WIDGET_H
#define NATIVE_TERMINAL_WIDGET_H

#include "terminal/emulator/terminal_emulator_worker.h"

#include <QBasicTimer>
#include <QFont>
#include <QMutex>
#include <QPoint>
#include <QThread>
#include <QVariant>
#include <QWidget>

#include <atomic>
#include <memory>

class QScrollBar;
class QTimer;

class NativeTerminalWidget : public QWidget
{
    Q_OBJECT
public:
    explicit NativeTerminalWidget(QWidget *parent = nullptr);
    ~NativeTerminalWidget();

    QSize gridSize() const;
    void setLocalEchoEnabled(bool enabled);
    void setPipeMode(bool enabled);
    void setPromptEchoFiltering(bool enabled);

public slots:
    void writePtyOutput(const QByteArray &data);
    void showStatusLine(const QString &message);

signals:
    void inputGenerated(const QByteArray &data);
    void gridSizeChanged(const QSize &size);
    void terminalTitleChanged(const QString &title);
    void currentDirectoryChanged(const QString &path);
    void outputConsumed(qint64 bytes);
    void processedOutput(const QByteArray &data);
    void screenCleared();
    void bell();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void timerEvent(QTimerEvent *event) override;

private Q_SLOTS:
    void applyPendingSnapshot();

private:
    static QString preferredTerminalFontFamily();
    static QString pathFromOsc7Payload(const QByteArray &payload);
    static bool isWideCharTrailingCell(const VTermScreenCell &cell);
    static bool isCellBefore(const QPoint &left, const QPoint &right);
    static QString trimTrailingSpaces(QString text);

    void startWorker();
    void shutdownWorker();
    void enqueueSnapshot(const TerminalEmulatorSnapshot &snapshot);
    void updateGridFromViewport();
    void sendKey(VTermKey key, VTermModifier modifiers);
    void sendText(const QString &text, VTermModifier modifiers);
    void sendInputBytes(const QByteArray &data);
    void sendClipboardPaste();
    bool clipboardHasText() const;
    void showContextMenu(const QPoint &globalPos);
    void sendMouseButton(QMouseEvent *event, bool pressed);
    void sendMouseMove(QMouseEvent *event);
    int wheelSteps(QWheelEvent *event);
    void sendWheelCursorKeys(int steps);
    QPoint cellFromPosition(const QPoint &pos) const;
    bool visibleCell(int row, int col, VTermScreenCell *cell) const;
    bool globalCell(int globalLine, int col, VTermScreenCell *cell) const;
    int visibleGlobalLine(int row) const;
    QPoint globalCellFromVisibleCell(const QPoint &cell) const;
    void scrollHistory(int lines);
    void setScrollbackOffset(int offset);
    void updateScrollBar();
    void markScrollBarInteraction();
    bool shouldStartLocalSelection(QMouseEvent *event) const;
    void beginSelection(const QPoint &cell);
    void updateSelection(const QPoint &cell);
    void finishSelection();
    void clearSelection();
    bool hasSelection() const;
    bool isCellSelected(int row, int col) const;
    bool isCellRangeSelected(int row, int col, int width) const;
    QString selectedText() const;
    void copySelectionToClipboard();
    void clearScreenAndScrollback(bool notifyRemote);
    VTermModifier modifiersFromQt(Qt::KeyboardModifiers modifiers) const;
    VTermModifier mouseModifiersFromQt(Qt::KeyboardModifiers modifiers) const;
    QColor colorFromVTerm(VTermColor color, bool foreground) const;
    QString cellText(const VTermScreenCell &cell) const;
    void updateCursorBlink();

    TerminalEmulatorWorker *m_worker = nullptr;
    QThread *m_workerThread = nullptr;
    std::atomic_bool m_workerClosing{false};
    QMutex m_workerMutex;
    QMutex m_snapshotMutex;
    std::shared_ptr<const TerminalEmulatorSnapshot> m_snapshot;
    std::shared_ptr<const TerminalEmulatorSnapshot> m_pendingSnapshot;
    bool m_snapshotApplyScheduled = false;
    quint64 m_lastScrollbackSequence = 0;
    QSize m_gridSize = QSize(80, 24);
    QFont m_font;
    int m_cellWidth = 8;
    int m_cellHeight = 16;
    int m_ascent = 12;
    VTermPos m_cursorPos{0, 0};
    bool m_cursorVisible = true;
    bool m_cursorBlink = false;
    bool m_cursorBlinkState = true;
    int m_cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
    int m_mouseMode = VTERM_PROP_MOUSE_NONE;
    bool m_alternateScreen = false;
    bool m_focusReport = false;
    int m_wheelPixelRemainder = 0;
    bool m_selecting = false;
    bool m_hasSelection = false;
    QPoint m_selectionAnchor;
    QPoint m_selectionCursor;
    int m_selectionAutoScrollDirection = 0;
    int m_scrollbackOffset = 0;
    QScrollBar *m_scrollBar = nullptr;
    QTimer *m_scrollBarInteractionTimer = nullptr;
    QBasicTimer m_blinkTimer;
    QBasicTimer m_selectionScrollTimer;
};

#endif /* NATIVE_TERMINAL_WIDGET_H */
