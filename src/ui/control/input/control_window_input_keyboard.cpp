#include "ui/control/control_window.h"

#include "common/logger_manager.h"
#include "util/config/config_util.h"
#include "util/clipboard/clipboard_util.h"
#include "util/json/json_util.h"
#include "util/input/key_util.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QTimer>

namespace
{
bool isClipboardModifier(Qt::KeyboardModifiers modifiers)
{
#if defined(Q_OS_MACOS)
    return modifiers.testFlag(Qt::MetaModifier);
#else
    return modifiers.testFlag(Qt::ControlModifier);
#endif
}

bool isClipboardCopyShortcut(QKeyEvent *event)
{
    return event && !event->isAutoRepeat() &&
           event->key() == Qt::Key_C &&
           isClipboardModifier(event->modifiers());
}

bool isClipboardPasteShortcut(QKeyEvent *event)
{
    return event && !event->isAutoRepeat() &&
           event->key() == Qt::Key_V &&
           isClipboardModifier(event->modifiers());
}

bool payloadHasTransferableContent(const QJsonObject &payload)
{
    return !JsonUtil::getString(payload, Constant::KEY_TEXT).isEmpty() ||
           ClipboardUtil::payloadHasFiles(payload) ||
           ClipboardUtil::payloadHasImage(payload);
}

QByteArray clipboardPayloadSignature(const QJsonObject &payload)
{
    return payloadHasTransferableContent(payload) ? JsonUtil::toCompactBytes(payload) : QByteArray();
}
} // namespace


bool ControlWindow::shouldCaptureRemoteKeyboard() const
{
    if (isClosing())
        return false;
    return isReceivedImg &&
           QApplication::activeWindow() == this &&
           QApplication::activeModalWidget() == nullptr &&
           QApplication::activePopupWidget() == nullptr;
}

void ControlWindow::connectLocalClipboardMonitor()
{
    if (m_clipboardMonitorConnected)
        return;

    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard)
        return;

    connect(clipboard, &QClipboard::dataChanged, this, &ControlWindow::noteLocalClipboardChanged);
    m_clipboardMonitorConnected = true;

    const QJsonObject payload = ClipboardUtil::readClipboardPayload();
    m_lastObservedClipboardSignature = clipboardPayloadSignature(payload);
    m_localClipboardDirty = !m_lastObservedClipboardSignature.isEmpty();
}


void ControlWindow::noteLocalClipboardChanged()
{
    if (isClosing())
        return;
    const QJsonObject payload = ClipboardUtil::readClipboardPayload();
    const QByteArray signature = clipboardPayloadSignature(payload);
    if (signature == m_lastObservedClipboardSignature)
        return;

    m_lastObservedClipboardSignature = signature;
    if (signature.isEmpty())
    {
        m_localClipboardDirty = false;
        m_lastSyncedClipboardSignature.clear();
        m_pendingRemoteClipboardApplySignature.clear();
        return;
    }

    if (signature == m_pendingRemoteClipboardApplySignature)
    {
        markLocalClipboardSynced(signature);
        m_pendingRemoteClipboardApplySignature.clear();
        return;
    }

    if (signature == m_lastSyncedClipboardSignature)
    {
        m_localClipboardDirty = false;
        return;
    }

    m_localClipboardDirty = true;
}


void ControlWindow::syncLocalClipboardToRemoteIfNeeded()
{
    if (isClosing())
        return;
    if (!m_localClipboardDirty)
        return;

    const QJsonObject payload = ClipboardUtil::readClipboardPayload();
    const QByteArray signature = clipboardPayloadSignature(payload);
    m_lastObservedClipboardSignature = signature;
    if (signature.isEmpty())
    {
        m_localClipboardDirty = false;
        return;
    }
    if (signature == m_lastSyncedClipboardSignature)
    {
        m_localClipboardDirty = false;
        return;
    }

    emit syncClipboardPayloadToRemote(payload);
    markLocalClipboardSynced(signature);
}


void ControlWindow::markLocalClipboardSynced(const QByteArray &signature)
{
    if (signature.isEmpty())
        return;

    m_lastSyncedClipboardSignature = signature;
    m_lastObservedClipboardSignature = signature;
    m_localClipboardDirty = false;
}


bool ControlWindow::handleRemoteKeyboardEvent(QKeyEvent *event, bool pressed)
{
    if (!event || !shouldCaptureRemoteKeyboard())
        return false;

    if (pressed && isClipboardPasteShortcut(event))
    {
        m_suppressClipboardPasteRelease = true;
        const QJsonObject payload = ClipboardUtil::readClipboardPayload();
        const QByteArray signature = clipboardPayloadSignature(payload);
        emit pasteClipboardPayloadToRemote(payload);
        markLocalClipboardSynced(signature);
        event->accept();
        return true;
    }

    const bool clipboardCopyShortcut = pressed && isClipboardCopyShortcut(event);
    if (pressed && !clipboardCopyShortcut)
        syncLocalClipboardToRemoteIfNeeded();

    if (!pressed && event->key() == Qt::Key_V && m_suppressClipboardPasteRelease)
    {
        m_suppressClipboardPasteRelease = false;
        event->accept();
        return true;
    }

    if (!pressed && event->key() == Qt::Key_C && m_suppressClipboardCopyRelease)
    {
        m_suppressClipboardCopyRelease = false;
        QTimer::singleShot(200, this, [this]() {
            emit requestRemoteClipboardSnapshot();
        });
    }

    const int winKey = KeyUtil::qtKeyToWinKey(event->key(), event->nativeVirtualKey());
    if (winKey <= 0)
    {
        LOG_DEBUG("Ignoring unsupported local key for remote keyboard forwarding: qtKey={}", event->key());
        event->accept();
        return true;
    }
    if (pressed)
    {
        if (!event->isAutoRepeat() && !m_remotePressedKeys.contains(winKey))
            m_remotePressedKeys.append(winKey);
    }
    else
    {
        m_remotePressedKeys.removeAll(winKey);
    }

    sendRemoteKeyboardEvent(winKey, pressed);

    if (clipboardCopyShortcut)
    {
        m_suppressClipboardCopyRelease = true;
    }

    event->accept();
    return true;
}


void ControlWindow::sendRemoteKeyboardEvent(int winKey, bool pressed)
{
    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_KEYBOARD)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, remote_id)
                          .add(Constant::KEY_RECEIVER_PWD, remote_pwd_md5)
                          .add(Constant::KEY_KEY, winKey)
                          .add(Constant::KEY_DWFLAGS, pressed ? Constant::KEY_DOWN : Constant::KEY_UP)
                          .build();

    QByteArray msg = JsonUtil::toCompactBytes(obj);
    rtc::message_variant msgStr(msg.toStdString());
    emit sendMsg2InputChannel(msgStr);
}


void ControlWindow::releaseRemotePressedKeys()
{
    for (int i = m_remotePressedKeys.size() - 1; i >= 0; --i)
        sendRemoteKeyboardEvent(m_remotePressedKeys.at(i), false);

    m_remotePressedKeys.clear();
    m_suppressClipboardCopyRelease = false;
    m_suppressClipboardPasteRelease = false;
}


void ControlWindow::keyPressEvent(QKeyEvent *event)
{
    if (handleRemoteKeyboardEvent(event, true))
        return;

    QMainWindow::keyPressEvent(event);
}


void ControlWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (handleRemoteKeyboardEvent(event, false))
        return;

    QMainWindow::keyReleaseEvent(event);
}


void ControlWindow::applyLocalClipboardPayload(const QJsonObject &payload)
{
    if (isClosing())
        return;
    QString errorMessage;
    const QByteArray signature = clipboardPayloadSignature(payload);
    m_pendingRemoteClipboardApplySignature = signature;
    if (!ClipboardUtil::writeClipboardPayload(payload, &errorMessage))
    {
        m_pendingRemoteClipboardApplySignature.clear();
        LOG_WARN("Failed to apply local clipboard payload: {}", errorMessage);
        return;
    }
    markLocalClipboardSynced(signature);
    m_pendingRemoteClipboardApplySignature.clear();
}
