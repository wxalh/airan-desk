#include "webrtc/cli/webrtc_cli.h"
#include "common/constant.h"
#include "util/input/input_util.h"
#include "util/json/json_util.h"

#include <QDateTime>

void WebRtcCli::parseInputMsg(const QJsonObject &object)
{
    QString msgType = JsonUtil::getString(object, Constant::KEY_MSGTYPE);
    if (msgType.isEmpty())
    {
        LOG_ERROR("parseInputMsg: Missing msgType");
        return;
    }
    QString senderId = JsonUtil::getString(object, Constant::KEY_SENDER);
    if (senderId.isEmpty() || senderId != m_remoteId)
    {
        LOG_WARNING("parseInputMsg: Ignoring message from unknown sender: {}", senderId);
        return;
    }
    QString remoteId = JsonUtil::getString(object, Constant::KEY_RECEIVER);
    QString remotePwd = JsonUtil::getString(object, Constant::KEY_RECEIVER_PWD);
    if (remoteId.isEmpty() || remoteId != ConfigUtil->local_id || remotePwd != ConfigUtil->local_pwd_md5)
    {
        LOG_WARNING("parseInputMsg: Ignoring message for receiver {}, expected {}, passwordValid={}",
                    remoteId, ConfigUtil->local_id, remotePwd == ConfigUtil->local_pwd_md5);
        return;
    }
    m_lastControlAliveMs = QDateTime::currentMSecsSinceEpoch();
    if (msgType == Constant::TYPE_MOUSE)
    {
        const QString flags = JsonUtil::getString(object, Constant::KEY_DWFLAGS);
        if (flags == Constant::KEY_MOVE)
        {
            const quint64 sequence = static_cast<quint64>(
                JsonUtil::getDouble(object, Constant::KEY_SEQUENCE, 0));
            if (sequence > 0)
            {
                if (sequence <= m_lastMouseMoveSequence)
                {
                    LOG_TRACE("Ignored stale mouse move: sequence={}, last={}",
                              sequence,
                              m_lastMouseMoveSequence);
                    return;
                }
                const bool boundary = JsonUtil::getBool(object, Constant::KEY_MOVE_BOUNDARY, false);
                if (!boundary && !m_mouseMoveBoundaryReady)
                {
                    LOG_TRACE("Buffered mouse move until first reliable boundary: sequence={}", sequence);
                    return;
                }
                if (boundary)
                    m_mouseMoveBoundaryReady = true;
                m_lastMouseMoveSequence = sequence;
            }
        }
        handleMouseEvent(object);
    }
    else if (msgType == Constant::TYPE_KEYBOARD)
    {
        
        handleKeyboardEvent(object);
    }
    else if (msgType == Constant::TYPE_STREAM_CONFIG)
    {
        handleStreamConfig(object);
    }
    else if (msgType == Constant::TYPE_AUDIO_CAPTURE)
    {
        handleAudioCaptureConfig(object);
    }
    else if (msgType == Constant::TYPE_SWITCH_SCREEN)
    {
        handleSwitchScreen(object);
    }
    else if (msgType == Constant::TYPE_CONTROL_HEARTBEAT)
    {
        LOG_TRACE("Control heartbeat received from {}", senderId);
    }
    else if (msgType == Constant::TYPE_REMOTE_OPERATION)
    {
        handleRemoteOperation(object);
    }
    else if (msgType == Constant::TYPE_ANDROID_NAVIGATION)
    {
        QString errorMessage;
        const QString action = JsonUtil::getString(object, Constant::KEY_ACTION);
        const bool ok = InputUtil::execAndroidNavigation(action, &errorMessage);
        if (!ok)
            LOG_WARN("Android navigation failed: action={}, error={}", action, errorMessage);
    }
    else
    {
        LOG_WARNING("parseInputMsg: Unknown input message type: {}", msgType);
    }
}
