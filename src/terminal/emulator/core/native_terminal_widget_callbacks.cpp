#include "terminal/emulator/native_terminal_widget.h"

#include <QMetaObject>
#include <QMutexLocker>

#include <climits>

void NativeTerminalWidget::enqueueSnapshot(const TerminalEmulatorSnapshot &snapshot)
{
    if (m_workerClosing.load())
        return;

    bool scheduleApply = false;
    {
        QMutexLocker locker(&m_snapshotMutex);
        m_pendingSnapshot = std::make_shared<TerminalEmulatorSnapshot>(snapshot);
        if (!m_snapshotApplyScheduled)
        {
            m_snapshotApplyScheduled = true;
            scheduleApply = true;
        }
    }
    if (!scheduleApply)
        return;

    QMetaObject::invokeMethod(this, "applyPendingSnapshot", Qt::QueuedConnection);
}

void NativeTerminalWidget::applyPendingSnapshot()
{
    std::shared_ptr<const TerminalEmulatorSnapshot> snapshot;
    {
        QMutexLocker locker(&m_snapshotMutex);
        snapshot = std::move(m_pendingSnapshot);
        m_snapshotApplyScheduled = false;
    }
    if (!snapshot || m_workerClosing.load())
        return;

    const int previousScrollbackLines = m_snapshot ? m_snapshot->scrollbackLines : 0;
    const quint64 previousSequence = m_lastScrollbackSequence;
    const bool gridChanged = snapshot->gridSize != m_gridSize;
    m_snapshot = std::move(snapshot);
    m_gridSize = m_snapshot->gridSize;
    m_cursorPos = VTermPos{m_snapshot->cursorPosition.y(), m_snapshot->cursorPosition.x()};
    m_cursorVisible = m_snapshot->cursorVisible;
    m_cursorBlink = m_snapshot->cursorBlink;
    m_cursorShape = m_snapshot->cursorShape;
    if (m_mouseMode != m_snapshot->mouseMode ||
        m_alternateScreen != m_snapshot->alternateScreen)
    {
        m_wheelPixelRemainder = 0;
    }
    m_mouseMode = m_snapshot->mouseMode;
    m_alternateScreen = m_snapshot->alternateScreen;
    m_focusReport = m_snapshot->focusReport;

    if (m_scrollbackOffset > 0 && m_snapshot->scrollbackSequence > previousSequence)
    {
        const quint64 pushed = m_snapshot->scrollbackSequence - previousSequence;
        m_scrollbackOffset = qMin(m_snapshot->scrollbackLines,
                                  m_scrollbackOffset + static_cast<int>(qMin<quint64>(pushed, static_cast<quint64>(INT_MAX))));
    }
    m_lastScrollbackSequence = m_snapshot->scrollbackSequence;
    m_scrollbackOffset = qBound(0, m_scrollbackOffset, m_snapshot->scrollbackLines);
    if (gridChanged || m_snapshot->scrollbackLines < previousScrollbackLines)
        clearSelection();
    updateScrollBar();
    updateCursorBlink();
    update();
}
