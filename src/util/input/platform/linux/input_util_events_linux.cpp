#include "util/input/input_util.h"

#include "util/input/platform/linux/input_util_events_linux_internal.h"

#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QtGlobal>
#include <algorithm>
#include <chrono>
#include <thread>

#include "common/logger_manager.h"

#if defined(Q_OS_LINUX)
#include <linux/uinput.h>

using namespace input_linux_internal;

void InputUtil::execKeyboardEvent(int keyCode, const QString &dwFlags)
{
    
    int fd = getKeyboardFd();
    if (fd < 0)
        return;

    int linuxKey = vkToLinux(keyCode);
    if (linuxKey < 0)
        return;

    LOG_DEBUG("Inject Linux keyboard event: keyCode={}, linuxKey={}, flags={}",
              keyCode, linuxKey, dwFlags.toStdString());
    uiWrite(fd, EV_KEY, static_cast<uint16_t>(linuxKey), dwFlags == "down" ? 1 : 0);
    uiWrite(fd, EV_SYN, SYN_REPORT, 0);
}

void InputUtil::execKeyboardText(const QString &text)
{
    if (text.isEmpty())
        return;

    int fd = getKeyboardFd();
    if (fd < 0)
        return;

    const QByteArray bytes = text.toUtf8();
    auto clipboardBackendName = [](const ClipboardTool &tool) {
        return tool.program.isEmpty() ? QStringLiteral("qt") : tool.program;
    };

    ClipboardTool tool = findClipboardTool();
    QByteArray previousClipboard;
    bool hasPreviousClipboard = readClipboardText(tool, &previousClipboard);
    if (!setClipboardText(tool, bytes))
    {
        LOG_WARN("Failed to set clipboard for Linux text injection: chars={}, backend={}",
                 text.size(),
                 clipboardBackendName(tool));
        if (!tool.program.isEmpty())
        {
            tool = ClipboardTool();
            previousClipboard.clear();
            hasPreviousClipboard = readClipboardText(tool, &previousClipboard);
            if (!setClipboardText(tool, bytes))
            {
                LOG_WARN("Failed to set clipboard for Linux text injection through Qt fallback: chars={}", text.size());
                return;
            }
        }
        else
        {
            return;
        }
    }

    pasteShortcut(fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));

    if (hasPreviousClipboard)
        setClipboardText(tool, previousClipboard);

    LOG_DEBUG("Injected Linux keyboard text through clipboard paste: chars={}, clipboardBackend={}",
              text.size(),
              clipboardBackendName(tool));
}

void InputUtil::execMouseEvent(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags,
                               bool reliableMoveBoundary)
{
    Q_UNUSED(reliableMoveBoundary)
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenRect = screen->geometry();
    qreal scaleFactor = screen->devicePixelRatio();
    int x = static_cast<int>((screenRect.x() + x_n * screenRect.width()) * scaleFactor);
    int y = static_cast<int>((screenRect.y() + y_n * screenRect.height()) * scaleFactor);

    
    int fd = getMouseFd();
    if (fd < 0)
        return;

    updateScreenSize();
    x -= s_screenX;
    y -= s_screenY;
    x = std::clamp(x, 0, std::max(0, s_screenW - 1));
    y = std::clamp(y, 0, std::max(0, s_screenH - 1));

    LOG_DEBUG("Inject Linux mouse event: button={}, x={}, y={}, wheel={}, flags={}",
              button, x, y, mouseData, dwFlags.toStdString());

    
    if (dwFlags == "move")
    {
        uiMoveAbs(fd, x, y);
        return;
    }

    uiMoveAbsAndWait(fd, x, y);

    if (dwFlags == "wheel")
    {
        uiWrite(fd, EV_REL, REL_WHEEL, mouseData > 0 ? 1 : -1);
        uiSync(fd);
        return;
    }

    int btn;
    switch (button)
    {
    case Qt::LeftButton:
        btn = BTN_LEFT;
        break;
    case Qt::RightButton:
        btn = BTN_RIGHT;
        break;
    case Qt::MiddleButton:
        btn = BTN_MIDDLE;
        break;
    default:
        return;
    }

    if (dwFlags == "doubleClick")
    {
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 1);
        uiSync(fd);
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 0);
        uiSync(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        uiMoveAbsAndWait(fd, x, y);
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 1);
        uiSync(fd);
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 0);
        uiSync(fd);
    }
    else
    {
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), dwFlags == "down" ? 1 : 0);
        uiSync(fd);
    }
}

void InputUtil::execMouseEventOnScreen(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags,
                                       int screenIndex, bool reliableMoveBoundary)
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    QScreen *screen = screens.value(screenIndex, QGuiApplication::primaryScreen());
    if (!screen)
    {
        execMouseEvent(button, x_n, y_n, mouseData, dwFlags, reliableMoveBoundary);
        return;
    }

    const QRect screenRect = screen->geometry();
    const qreal scaleFactor = screen->devicePixelRatio();
    int x = static_cast<int>((screenRect.x() + x_n * screenRect.width()) * scaleFactor);
    int y = static_cast<int>((screenRect.y() + y_n * screenRect.height()) * scaleFactor);

    int fd = getMouseFd();
    if (fd < 0)
        return;

    updateScreenSize();
    x -= s_screenX;
    y -= s_screenY;
    x = std::clamp(x, 0, std::max(0, s_screenW - 1));
    y = std::clamp(y, 0, std::max(0, s_screenH - 1));

    if (dwFlags == "move")
    {
        uiMoveAbs(fd, x, y);
        return;
    }

    uiMoveAbsAndWait(fd, x, y);

    if (dwFlags == "wheel")
    {
        uiWrite(fd, EV_REL, REL_WHEEL, mouseData > 0 ? 1 : -1);
        uiSync(fd);
        return;
    }

    int btn;
    switch (button)
    {
    case Qt::LeftButton:
        btn = BTN_LEFT;
        break;
    case Qt::RightButton:
        btn = BTN_RIGHT;
        break;
    case Qt::MiddleButton:
        btn = BTN_MIDDLE;
        break;
    default:
        return;
    }

    if (dwFlags == "doubleClick")
    {
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 1);
        uiSync(fd);
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 0);
        uiSync(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        uiMoveAbsAndWait(fd, x, y);
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 1);
        uiSync(fd);
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 0);
        uiSync(fd);
    }
    else
    {
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), dwFlags == "down" ? 1 : 0);
        uiSync(fd);
    }
}

void InputUtil::execMouseEventOnDesktopSource(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags,
                                              int desktopSourceIndex, bool reliableMoveBoundary)
{
    execMouseEventOnScreen(button, x_n, y_n, mouseData, dwFlags, desktopSourceIndex, reliableMoveBoundary);
}

void InputUtil::execMouseEventInRect(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags,
                                     const QRect &rect, bool reliableMoveBoundary)
{
    if (!rect.isValid())
    {
        execMouseEvent(button, x_n, y_n, mouseData, dwFlags, reliableMoveBoundary);
        return;
    }

    int fd = getMouseFd();
    if (fd < 0)
        return;

    updateScreenSize();
    int x = rect.left() + static_cast<int>(qBound(0.0, x_n, 1.0) * (rect.width() - 1));
    int y = rect.top() + static_cast<int>(qBound(0.0, y_n, 1.0) * (rect.height() - 1));
    x -= s_screenX;
    y -= s_screenY;
    x = std::clamp(x, 0, std::max(0, s_screenW - 1));
    y = std::clamp(y, 0, std::max(0, s_screenH - 1));

    if (dwFlags == "move")
    {
        uiMoveAbs(fd, x, y);
        return;
    }

    uiMoveAbsAndWait(fd, x, y);

    if (dwFlags == "wheel")
    {
        uiWrite(fd, EV_REL, REL_WHEEL, mouseData > 0 ? 1 : -1);
        uiSync(fd);
        return;
    }

    int btn;
    switch (button)
    {
    case Qt::LeftButton:
        btn = BTN_LEFT;
        break;
    case Qt::RightButton:
        btn = BTN_RIGHT;
        break;
    case Qt::MiddleButton:
        btn = BTN_MIDDLE;
        break;
    default:
        return;
    }

    if (dwFlags == "doubleClick")
    {
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 1);
        uiSync(fd);
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 0);
        uiSync(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        uiMoveAbsAndWait(fd, x, y);
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 1);
        uiSync(fd);
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 0);
        uiSync(fd);
    }
    else
    {
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), dwFlags == "down" ? 1 : 0);
        uiSync(fd);
    }
}
#endif /* Q_OS_LINUX */
