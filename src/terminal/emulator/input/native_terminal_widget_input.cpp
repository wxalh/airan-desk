#include "terminal/emulator/native_terminal_widget.h"
#include "terminal/emulator/input/terminal_key_encoder.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScrollBar>

bool NativeTerminalWidget::event(QEvent *event)
{
    if (event && event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab)
        {
            keyPressEvent(keyEvent);
            return keyEvent->isAccepted();
        }
    }
    return QWidget::event(event);
}


void NativeTerminalWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_scrollBar)
        m_scrollBar->setGeometry(qMax(0, width() - m_scrollBar->width()), 0, m_scrollBar->width(), height());
    updateGridFromViewport();
}


void NativeTerminalWidget::keyPressEvent(QKeyEvent *event)
{
    if (!m_worker || m_workerClosing.load())
        return;

    if (event->matches(QKeySequence::Paste) ||
        (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_V))
    {
        sendClipboardPaste();
        event->accept();
        return;
    }

    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_L)
    {
        clearScreenAndScrollback(true);
        event->accept();
        return;
    }

    const QByteArray controlByte = TerminalKeyEncoder::controlByteForKey(
        event->key(), event->modifiers());
    if (!controlByte.isEmpty())
    {
        sendInputBytes(controlByte);
        event->accept();
        return;
    }

    const QByteArray escapeSequence = TerminalKeyEncoder::escapeSequenceForQtKey(
        event->key(), event->modifiers());
    if (!escapeSequence.isEmpty())
    {
        sendInputBytes(escapeSequence);
        event->accept();
        return;
    }

    const VTermModifier modifiers = modifiersFromQt(event->modifiers());
    const VTermKey key = TerminalKeyEncoder::vtermKeyForQtKey(event->key(), event->modifiers());

    if (key != VTERM_KEY_NONE)
    {
        sendKey(key, modifiers);
        event->accept();
        return;
    }

    if (!event->text().isEmpty())
    {
        sendText(event->text(), modifiers);
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}


void NativeTerminalWidget::inputMethodEvent(QInputMethodEvent *event)
{
    if (!m_worker || m_workerClosing.load())
    {
        QWidget::inputMethodEvent(event);
        return;
    }

    const QString commitText = event->commitString();
    if (!commitText.isEmpty())
    {
        clearSelection();
        sendText(commitText, VTERM_MOD_NONE);
        event->accept();
        return;
    }

    QWidget::inputMethodEvent(event);
}


QVariant NativeTerminalWidget::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImCursorRectangle)
        return QRect(m_cursorPos.col * m_cellWidth, m_cursorPos.row * m_cellHeight, m_cellWidth, m_cellHeight);
    if (query == Qt::ImFont)
        return m_font;
    return QWidget::inputMethodQuery(query);
}


void NativeTerminalWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    if (m_worker && !m_workerClosing.load() && m_focusReport)
        sendInputBytes(QByteArrayLiteral("\x1b[I"));
    m_cursorBlinkState = true;
    updateCursorBlink();
    update();
}


void NativeTerminalWidget::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    if (m_worker && !m_workerClosing.load() && m_focusReport)
        sendInputBytes(QByteArrayLiteral("\x1b[O"));
    updateCursorBlink();
    update();
}



VTermModifier NativeTerminalWidget::modifiersFromQt(Qt::KeyboardModifiers modifiers) const
{
    int result = VTERM_MOD_NONE;
    if (modifiers.testFlag(Qt::ShiftModifier))
        result |= VTERM_MOD_SHIFT;
    if (modifiers.testFlag(Qt::AltModifier))
        result |= VTERM_MOD_ALT;
    if (modifiers.testFlag(Qt::ControlModifier))
        result |= VTERM_MOD_CTRL;
    return static_cast<VTermModifier>(result);
}


VTermModifier NativeTerminalWidget::mouseModifiersFromQt(Qt::KeyboardModifiers modifiers) const
{
    return modifiersFromQt(modifiers);
}
