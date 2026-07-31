#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "util/input/input_util.h"
#include "util/json/json_util.h"

#if defined(WEBRTC_MAC) && !defined(WEBRTC_IOS)
#include "desktop_capture/mac/desktop_configuration.h"
#endif


void WebRtcCli::handleMouseEvent(const QJsonObject &object)
{
    noteClipboardInputActivity();

    int button = JsonUtil::getInt(object, Constant::KEY_BUTTON, -1);
    qreal x = JsonUtil::getDouble(object, Constant::KEY_X, -1);
    qreal y = JsonUtil::getDouble(object, Constant::KEY_Y, -1);
    int mouseData = JsonUtil::getInt(object, Constant::KEY_MOUSEDATA, -1);
    QString flags = JsonUtil::getString(object, Constant::KEY_DWFLAGS, "");
    const bool reliableMoveBoundary = JsonUtil::getBool(object, Constant::KEY_MOVE_BOUNDARY, false);
    if (x < 0 || y < 0)
    {
        LOG_ERROR("handleMouseEvent: Invalid mouse event data");
        return;
    }

    const QRect desktopSourceRect = m_currentDesktopSourceRect.isValid()
                                        ? m_currentDesktopSourceRect
                                        : desktopSourceRectForScreenIndex(m_screenIndex);
#if defined(WEBRTC_MAC) && !defined(WEBRTC_IOS)
    const airan::desktop_capture::MacDesktopConfiguration macConfig =
        airan::desktop_capture::MacDesktopConfiguration::GetCurrent(
            airan::desktop_capture::MacDesktopConfiguration::TopLeftOrigin);
    if (m_currentDesktopSourceIndex >= 0 &&
        m_currentDesktopSourceIndex < static_cast<int>(macConfig.displays.size()))
    {
        const auto &bounds = macConfig.displays[static_cast<size_t>(m_currentDesktopSourceIndex)].bounds;
        InputUtil::execMouseEventInRect(button,
                                        x,
                                        y,
                                        mouseData,
                                        flags,
                                        QRect(bounds.left(), bounds.top(), bounds.width(), bounds.height()),
                                        reliableMoveBoundary);
    }
    else
    {
        InputUtil::execMouseEventOnScreen(button, x, y, mouseData, flags, m_screenIndex, reliableMoveBoundary);
    }
#else
    InputUtil::execMouseEventInRect(button, x, y, mouseData, flags, desktopSourceRect, reliableMoveBoundary);
#endif
    LOG_TRACE("Handled mouse event: {} at ({}, {}) on screen {}, desktopSource {}, rect={}x{}+{}+{}",
              flags,
              x,
              y,
              m_screenIndex,
              m_currentDesktopSourceIndex,
              desktopSourceRect.width(),
              desktopSourceRect.height(),
              desktopSourceRect.x(),
              desktopSourceRect.y());
}


void WebRtcCli::handleKeyboardEvent(const QJsonObject &object)
{
    noteClipboardInputActivity();

    int key = JsonUtil::getInt(object, Constant::KEY_KEY, -1);
    QString flags = JsonUtil::getString(object, Constant::KEY_DWFLAGS, "");
    QString text = JsonUtil::getString(object, Constant::KEY_TEXT, "");

    if (flags == QStringLiteral("text"))
    {
        if (text.isEmpty())
        {
            LOG_WARN("handleKeyboardEvent: Empty keyboard text event");
            return;
        }
        InputUtil::execKeyboardText(text);
        LOG_TRACE("Handled keyboard text event: chars={}", text.size());
        return;
    }

    if (key == -1 || flags.isEmpty())
    {
        LOG_ERROR("handleKeyboardEvent: Invalid keyboard event data");
        return;
    }

    InputUtil::execKeyboardEvent(key, flags);
    LOG_TRACE("Handled keyboard event: {} {}", flags, key);
}


void WebRtcCli::handleRemoteOperation(const QJsonObject &object)
{
    const QString action = JsonUtil::getString(object, Constant::KEY_ACTION);
    QString errorMessage;
    const bool ok = InputUtil::execRemoteOperation(action, &errorMessage);
    if (ok)
        LOG_INFO("Executed remote operation: {}", action);
    else
        LOG_WARN("Failed to execute remote operation {}: {}", action, errorMessage);
}


void WebRtcCli::handleRunFile(const QJsonObject &object)
{
    const QString filePath = JsonUtil::getString(object, Constant::KEY_PATH_CLI,
                                                JsonUtil::getString(object, Constant::KEY_PATH));
    if (filePath.isEmpty())
    {
        LOG_WARN("handleRunFile ignored empty path");
        return;
    }

    QString errorMessage;
    const bool ok = InputUtil::runProgram(filePath, &errorMessage);
    QJsonObject response = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_RUN_FILE)
                               .add(Constant::KEY_PATH_CLI, filePath)
                               .add(Constant::KEY_STATUS, ok)
                               .add(Constant::KEY_ERROR, errorMessage)
                               .build();
    sendFileTextChannelMessage(response);
}
