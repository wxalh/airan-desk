#include "input_util.h"
#include <QRect>
#include <QScreen>
#include <QGuiApplication>
#include <cmath>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#undef KeyPress // 避免与Qt宏冲突
#undef KeyRelease
#undef None
#elif defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>
#endif

InputUtil::InputUtil(QObject *parent)
    : QObject{parent}
{
}

void InputUtil::execKeyboardEvent(int keyCode, const QString &dwFlags)
{

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(keyCode);
    input.ki.dwFlags = dwFlags == "down" ? 0 : KEYEVENTF_KEYUP;

    SendInput(1, &input, sizeof(INPUT));

#elif defined(Q_OS_LINUX)
    // Linux 实现 (X11)
    Display *display = XOpenDisplay(nullptr);
    if (display)
    {
        KeyCode code = XKeysymToKeycode(display, static_cast<KeySym>(keyCode));
        Bool isPress = (dwFlags == "down") ? True : False;
        XTestFakeKeyEvent(display, code, isPress, CurrentTime);
        XFlush(display);
        XCloseDisplay(display);
    }

#elif defined(Q_OS_MACOS)
    // macOS 实现 (CoreGraphics)
    CGEventRef event;
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    CGEventType type = (dwFlags == "down") ? kCGEventKeyDown : kCGEventKeyUp;

    event = CGEventCreateKeyboardEvent(source, static_cast<CGKeyCode>(keyCode), (type == kCGEventKeyDown));
    CGEventPost(kCGHIDEventTap, event);

    CFRelease(event);
    CFRelease(source);
#endif
}

void InputUtil::execMouseEvent(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenRect = screen->geometry();
    // 统一坐标转换（考虑 macOS Retina 缩放）
    qreal scaleFactor = screen->devicePixelRatio();
    int x = static_cast<int>(x_n * screenRect.width() * scaleFactor);
    int y = static_cast<int>(y_n * screenRect.height() * scaleFactor);

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    // Windows 实现
    // 使用绝对坐标并修正整数除法问题，确保 SendInput 在各种场景（包括 Task Manager）下更可靠
    int screenX = GetSystemMetrics(SM_CXSCREEN);
    int screenY = GetSystemMetrics(SM_CYSCREEN);

    auto normalize = [](int coord, int max) -> LONG {
        if (max <= 1) return 0;
        double v = (coord * 65535.0) / (static_cast<double>(max) - 1.0);
        if (v < 0.0) v = 0.0;
        if (v > 65535.0) v = 65535.0;
        return static_cast<LONG>(std::round(v));
    };

    LONG absX = normalize(x, screenX);
    LONG absY = normalize(y, screenY);

    // 先移动鼠标到目标物理位置（兼容部分只响应 SetCursorPos 的情况）
    SetCursorPos(x, y);

    // 仅移动，不发送点击
    if (dwFlags == "move")
    {
        INPUT input = {0};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        input.mi.dx = absX;
        input.mi.dy = absY;
        SendInput(1, &input, sizeof(INPUT));
        return;
    }

    // 滚轮事件：只发送 WHEEL 类型并设置 mouseData
    if (dwFlags == "wheel")
    {
        INPUT input = {0};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = mouseData;
        SendInput(1, &input, sizeof(INPUT));
        return;
    }

    // 双击：发送两次 按下/抬起，带绝对坐标
    if (dwFlags == "doubleClick")
    {
        INPUT inputs[4] = {0};
        DWORD downFlag = (button == Qt::LeftButton) ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_RIGHTDOWN;
        DWORD upFlag = (button == Qt::LeftButton) ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_RIGHTUP;

        // 第一次按下
        inputs[0].type = INPUT_MOUSE;
        inputs[0].mi.dwFlags = downFlag | MOUSEEVENTF_ABSOLUTE;
        inputs[0].mi.dx = absX;
        inputs[0].mi.dy = absY;
        // 第一次抬起
        inputs[1] = inputs[0];
        inputs[1].mi.dwFlags = upFlag | MOUSEEVENTF_ABSOLUTE;
        // 第二次按下
        inputs[2] = inputs[0];
        inputs[2].mi.dwFlags = downFlag | MOUSEEVENTF_ABSOLUTE;
        // 第二次抬起
        inputs[3] = inputs[1];

        SendInput(4, inputs, sizeof(INPUT));
        return;
    }

    // 普通按下/抬起，带绝对坐标
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    DWORD flag = 0;
    switch (button)
    {
    case Qt::LeftButton:
        flag = (dwFlags == "down") ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        break;
    case Qt::RightButton:
        flag = (dwFlags == "down") ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        break;
    case Qt::MiddleButton:
        flag = (dwFlags == "down") ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        break;
    default:
        return;
    }

    input.mi.dwFlags = flag | MOUSEEVENTF_ABSOLUTE;
    input.mi.dx = absX;
    input.mi.dy = absY;
    SendInput(1, &input, sizeof(INPUT));

#elif defined(Q_OS_LINUX)
    // Linux 实现 (XTest)
    Display *display = XOpenDisplay(nullptr);
    if (display)
    {
        // 移动光标
        XTestFakeMotionEvent(display, -1, x, y, CurrentTime);

        if (dwFlags == "move")
        {
            return;
        }
        // 处理点击/滚轮
        if (dwFlags == "doubleClick")
        {
            int btn = (button == Qt::LeftButton) ? Button1 : Button3;
            XTestFakeButtonEvent(display, btn, True, CurrentTime);
            XTestFakeButtonEvent(display, btn, False, CurrentTime);
            XTestFakeButtonEvent(display, btn, True, CurrentTime);
            XTestFakeButtonEvent(display, btn, False, CurrentTime);
        }
        else if (dwFlags == "wheel")
        {
            int direction = (mouseData > 0) ? Button4 : Button5;
            XTestFakeButtonEvent(display, direction, True, CurrentTime);
            XTestFakeButtonEvent(display, direction, False, CurrentTime);
        }
        else
        {
            int btn;
            switch (button)
            {
            case Qt::LeftButton:
                btn = Button1;
                break;
            case Qt::RightButton:
                btn = Button3;
                break;
            case Qt::MiddleButton:
                btn = Button2;
                break;
            default:
                return;
            }
            XTestFakeButtonEvent(display, btn, (dwFlags == "down"), CurrentTime);
        }
        XFlush(display);
        XCloseDisplay(display);
    }

#elif defined(Q_OS_MACOS)
    // macOS 实现 (CoreGraphics)
    CGPoint pos = CGPointMake(x, y);
    CGEventType type;
    CGMouseButton btn = kCGMouseButtonLeft;

    // 确定按钮类型
    switch (button)
    {
    case Qt::LeftButton:
        btn = kCGMouseButtonLeft;
        break;
    case Qt::RightButton:
        btn = kCGMouseButtonRight;
        break;
    case Qt::MiddleButton:
        btn = kCGMouseButtonCenter;
        break;
    }

    // 处理事件类型
    if (dwFlags == "move")
    {
        CGEventRef event = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, pos, 0);
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
    else if (dwFlags == "doubleClick")
    {
        CGEventRef event = CGEventCreateMouseEvent(
            nullptr, kCGEventLeftMouseDown, pos, kCGMouseButtonLeft);
        CGEventSetIntegerValueField(event, kCGMouseEventClickState, 2);
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
    else if (dwFlags == "wheel")
    {
        CGEventRef event = CGEventCreateScrollWheelEvent(
            nullptr, kCGScrollEventUnitLine, 1, mouseData / 120);
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
    else
    {
        type = (dwFlags == "down") ? ((btn == kCGMouseButtonLeft) ? kCGEventLeftMouseDown : kCGEventRightMouseDown) : ((btn == kCGMouseButtonLeft) ? kCGEventLeftMouseUp : kCGEventRightMouseUp);

        CGEventRef event = CGEventCreateMouseEvent(nullptr, type, pos, btn);
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
#endif
}
