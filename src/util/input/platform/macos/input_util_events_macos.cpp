#include "util/input/input_util.h"

#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QtGlobal>

#include "common/logger_manager.h"

#if defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>
#include <atomic>
#include <chrono>
#include <thread>

namespace
{

    static CGKeyCode winVkToMacKeyCode(int vk)
    {
        switch (vk)
        {
        case 0x41:
            return kVK_ANSI_A;
        case 0x42:
            return kVK_ANSI_B;
        case 0x43:
            return kVK_ANSI_C;
        case 0x44:
            return kVK_ANSI_D;
        case 0x45:
            return kVK_ANSI_E;
        case 0x46:
            return kVK_ANSI_F;
        case 0x47:
            return kVK_ANSI_G;
        case 0x48:
            return kVK_ANSI_H;
        case 0x49:
            return kVK_ANSI_I;
        case 0x4A:
            return kVK_ANSI_J;
        case 0x4B:
            return kVK_ANSI_K;
        case 0x4C:
            return kVK_ANSI_L;
        case 0x4D:
            return kVK_ANSI_M;
        case 0x4E:
            return kVK_ANSI_N;
        case 0x4F:
            return kVK_ANSI_O;
        case 0x50:
            return kVK_ANSI_P;
        case 0x51:
            return kVK_ANSI_Q;
        case 0x52:
            return kVK_ANSI_R;
        case 0x53:
            return kVK_ANSI_S;
        case 0x54:
            return kVK_ANSI_T;
        case 0x55:
            return kVK_ANSI_U;
        case 0x56:
            return kVK_ANSI_V;
        case 0x57:
            return kVK_ANSI_W;
        case 0x58:
            return kVK_ANSI_X;
        case 0x59:
            return kVK_ANSI_Y;
        case 0x5A:
            return kVK_ANSI_Z;
        case 0x30:
            return kVK_ANSI_0;
        case 0x31:
            return kVK_ANSI_1;
        case 0x32:
            return kVK_ANSI_2;
        case 0x33:
            return kVK_ANSI_3;
        case 0x34:
            return kVK_ANSI_4;
        case 0x35:
            return kVK_ANSI_5;
        case 0x36:
            return kVK_ANSI_6;
        case 0x37:
            return kVK_ANSI_7;
        case 0x38:
            return kVK_ANSI_8;
        case 0x39:
            return kVK_ANSI_9;
        case 0x08:
            return kVK_Delete;
        case 0x09:
            return kVK_Tab;
        case 0x0C:
            return kVK_ANSI_KeypadClear;
        case 0x0D:
            return kVK_Return;
        case 0x10:
            return kVK_Shift;
        case 0x11:
            return kVK_Control;
        case 0x12:
            return kVK_Option;
        case 0x5B:
        case 0x5C:
            return kVK_Command;
        case 0x60:
            return kVK_ANSI_Keypad0;
        case 0x61:
            return kVK_ANSI_Keypad1;
        case 0x62:
            return kVK_ANSI_Keypad2;
        case 0x63:
            return kVK_ANSI_Keypad3;
        case 0x64:
            return kVK_ANSI_Keypad4;
        case 0x65:
            return kVK_ANSI_Keypad5;
        case 0x66:
            return kVK_ANSI_Keypad6;
        case 0x67:
            return kVK_ANSI_Keypad7;
        case 0x68:
            return kVK_ANSI_Keypad8;
        case 0x69:
            return kVK_ANSI_Keypad9;
        case 0x6A:
            return kVK_ANSI_KeypadMultiply;
        case 0x6B:
            return kVK_ANSI_KeypadPlus;
        case 0x6D:
            return kVK_ANSI_KeypadMinus;
        case 0x6E:
            return kVK_ANSI_KeypadDecimal;
        case 0x6F:
            return kVK_ANSI_KeypadDivide;
        case 0x90:
            return kVK_ANSI_KeypadClear;
        case 0xA0:
        case 0xA1:
            return kVK_Shift;
        case 0xA2:
        case 0xA3:
            return kVK_Control;
        case 0xA4:
        case 0xA5:
            return kVK_Option;
        case 0x14:
            return kVK_CapsLock;
        case 0x13:
            return kVK_F15;
        case 0x1B:
            return kVK_Escape;
        case 0x20:
            return kVK_Space;
        case 0x21:
            return kVK_PageUp;
        case 0x22:
            return kVK_PageDown;
        case 0x23:
            return kVK_End;
        case 0x24:
            return kVK_Home;
        case 0x25:
            return kVK_LeftArrow;
        case 0x26:
            return kVK_UpArrow;
        case 0x27:
            return kVK_RightArrow;
        case 0x28:
            return kVK_DownArrow;
        case 0x2A:
        case 0x2C:
            return kVK_F13;
        case 0x2D:
        case 0x2F:
            return kVK_Help;
        case 0x2E:
            return kVK_ForwardDelete;
        case 0x70:
            return kVK_F1;
        case 0x71:
            return kVK_F2;
        case 0x72:
            return kVK_F3;
        case 0x73:
            return kVK_F4;
        case 0x74:
            return kVK_F5;
        case 0x75:
            return kVK_F6;
        case 0x76:
            return kVK_F7;
        case 0x77:
            return kVK_F8;
        case 0x78:
            return kVK_F9;
        case 0x79:
            return kVK_F10;
        case 0x7A:
            return kVK_F11;
        case 0x7B:
            return kVK_F12;
        case 0x7C:
            return kVK_F13;
        case 0x7D:
            return kVK_F14;
        case 0x7E:
            return kVK_F15;
        case 0x7F:
            return kVK_F16;
        case 0x80:
            return kVK_F17;
        case 0x81:
            return kVK_F18;
        case 0x82:
            return kVK_F19;
        case 0x83:
            return kVK_F20;
        case 0x91:
            return kVK_F14;
        case 0xAD:
            return kVK_Mute;
        case 0xAE:
            return kVK_VolumeDown;
        case 0xAF:
            return kVK_VolumeUp;
        case 0xBA:
            return kVK_ANSI_Semicolon;
        case 0xBB:
            return kVK_ANSI_Equal;
        case 0xBC:
            return kVK_ANSI_Comma;
        case 0xBD:
            return kVK_ANSI_Minus;
        case 0xBE:
            return kVK_ANSI_Period;
        case 0xBF:
            return kVK_ANSI_Slash;
        case 0xC0:
            return kVK_ANSI_Grave;
        case 0xDB:
            return kVK_ANSI_LeftBracket;
        case 0xDC:
            return kVK_ANSI_Backslash;
        case 0xDD:
            return kVK_ANSI_RightBracket;
        case 0xDE:
            return kVK_ANSI_Quote;
        default:
            return static_cast<CGKeyCode>(UINT16_MAX);
        }
    }

    static bool ensureMacAccessibilityReady(const char *action)
    {
        bool trusted = AXIsProcessTrusted();
        static std::atomic_bool promptRequested{false};
        if (!trusted && !promptRequested.exchange(true))
        {
            const void *keys[] = {kAXTrustedCheckOptionPrompt};
            const void *values[] = {kCFBooleanTrue};
            CFDictionaryRef options = CFDictionaryCreate(kCFAllocatorDefault,
                                                         keys,
                                                         values,
                                                         1,
                                                         &kCFCopyStringDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
            trusted = AXIsProcessTrustedWithOptions(options);
            if (options)
                CFRelease(options);
        }
        static std::atomic_bool permissionWarningLogged{false};
        if (!trusted && !permissionWarningLogged.exchange(true))
        {
            LOG_WARN("macOS accessibility permission is not granted, {} may not work", action ? action : "operation");
        }
        return trusted;
    }

    static void postMacMouseMove(CGPoint pos)
    {
        CGEventRef event = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved, pos, kCGMouseButtonLeft);
        if (!event)
            return;
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }

    static void postAndReleaseMacEvent(CGEventRef event)
    {
        if (!event)
            return;
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }

    static void postMacMouseMoveAndWait(CGPoint pos)
    {
        postMacMouseMove(pos);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

} /* namespace */


void InputUtil::execKeyboardEvent(int keyCode, const QString &dwFlags)
{
    
    if (!ensureMacAccessibilityReady("keyboard injection"))
        return;
    CGEventRef event;
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    CGEventType type = (dwFlags == "down") ? kCGEventKeyDown : kCGEventKeyUp;

    if (!source)
    {
        LOG_ERROR("Failed to create macOS keyboard event source");
        return;
    }

    CGKeyCode macKeyCode = winVkToMacKeyCode(keyCode);
    if (macKeyCode == static_cast<CGKeyCode>(UINT16_MAX))
    {
        LOG_WARN("Unsupported macOS key mapping for incoming keyCode={}", keyCode);
        CFRelease(source);
        return;
    }

    event = CGEventCreateKeyboardEvent(source, macKeyCode, (type == kCGEventKeyDown));
    if (!event)
    {
        LOG_ERROR("Failed to create macOS keyboard event for keyCode={}", keyCode);
        CFRelease(source);
        return;
    }
    CGEventPost(kCGHIDEventTap, event);

    CFRelease(event);
    CFRelease(source);
}

void InputUtil::execKeyboardText(const QString &text)
{
    if (text.isEmpty())
        return;
    if (!ensureMacAccessibilityReady("text injection"))
        return;
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    if (!source)
    {
        LOG_ERROR("Failed to create macOS text event source");
        return;
    }
    const UniChar *characters = reinterpret_cast<const UniChar *>(text.utf16());
    int offset = 0;
    while (offset < text.size())
    {
        int length = qMin(20, text.size() - offset);
        if (offset + length < text.size() &&
            QChar::isHighSurrogate(characters[offset + length - 1]))
        {
            --length;
        }
        if (length <= 0)
            length = qMin(2, text.size() - offset);

        CGEventRef down = CGEventCreateKeyboardEvent(source, 0, true);
        CGEventRef up = CGEventCreateKeyboardEvent(source, 0, false);
        if (down && up)
        {
            CGEventKeyboardSetUnicodeString(down, length, characters + offset);
            CGEventKeyboardSetUnicodeString(up, length, characters + offset);
            CGEventPost(kCGHIDEventTap, down);
            CGEventPost(kCGHIDEventTap, up);
        }
        if (down)
            CFRelease(down);
        if (up)
            CFRelease(up);
        offset += length;
    }
    CFRelease(source);
}

void InputUtil::execMouseEvent(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags,
                               bool reliableMoveBoundary)
{
    Q_UNUSED(reliableMoveBoundary)
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QRect screenRect = screen->geometry();
    const int x = screenRect.left() + static_cast<int>(qBound(0.0, x_n, 1.0) * (screenRect.width() - 1));
    const int y = screenRect.top() + static_cast<int>(qBound(0.0, y_n, 1.0) * (screenRect.height() - 1));

    
    if (!ensureMacAccessibilityReady("mouse injection"))
        return;
    CGPoint pos = CGPointMake(x, y);
    CGEventType downType = kCGEventLeftMouseDown;
    CGEventType upType = kCGEventLeftMouseUp;
    CGMouseButton btn = kCGMouseButtonLeft;

    
    switch (button)
    {
    case Qt::LeftButton:
        btn = kCGMouseButtonLeft;
        downType = kCGEventLeftMouseDown;
        upType = kCGEventLeftMouseUp;
        break;
    case Qt::RightButton:
        btn = kCGMouseButtonRight;
        downType = kCGEventRightMouseDown;
        upType = kCGEventRightMouseUp;
        break;
    case Qt::MiddleButton:
        btn = kCGMouseButtonCenter;
        downType = kCGEventOtherMouseDown;
        upType = kCGEventOtherMouseUp;
        break;
    }

    
    if (dwFlags == "move")
    {
        postMacMouseMove(pos);
    }
    else if (dwFlags == "doubleClick")
    {
        postMacMouseMoveAndWait(pos);
        CGEventRef down1 = CGEventCreateMouseEvent(nullptr, downType, pos, btn);
        CGEventRef up1 = CGEventCreateMouseEvent(nullptr, upType, pos, btn);
        CGEventRef down2 = CGEventCreateMouseEvent(nullptr, downType, pos, btn);
        CGEventRef up2 = CGEventCreateMouseEvent(nullptr, upType, pos, btn);
        if (down1 && up1 && down2 && up2)
        {
            CGEventSetIntegerValueField(down1, kCGMouseEventClickState, 1);
            CGEventSetIntegerValueField(up1, kCGMouseEventClickState, 1);
            CGEventSetIntegerValueField(down2, kCGMouseEventClickState, 2);
            CGEventSetIntegerValueField(up2, kCGMouseEventClickState, 2);
            CGEventPost(kCGHIDEventTap, down1);
            CGEventPost(kCGHIDEventTap, up1);
            CGEventPost(kCGHIDEventTap, down2);
            CGEventPost(kCGHIDEventTap, up2);
        }
        if (down1)
            CFRelease(down1);
        if (up1)
            CFRelease(up1);
        if (down2)
            CFRelease(down2);
        if (up2)
            CFRelease(up2);
    }
    else if (dwFlags == "wheel")
    {
        postMacMouseMoveAndWait(pos);
        CGEventRef event = CGEventCreateScrollWheelEvent(
            nullptr, kCGScrollEventUnitLine, 1, mouseData / 120);
        postAndReleaseMacEvent(event);
    }
    else
    {
        postMacMouseMoveAndWait(pos);
        CGEventType type = (dwFlags == "down") ? downType : upType;

        CGEventRef event = CGEventCreateMouseEvent(nullptr, type, pos, btn);
        postAndReleaseMacEvent(event);
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
    const int x = screenRect.left() + static_cast<int>(qBound(0.0, x_n, 1.0) * (screenRect.width() - 1));
    const int y = screenRect.top() + static_cast<int>(qBound(0.0, y_n, 1.0) * (screenRect.height() - 1));

    if (!ensureMacAccessibilityReady("mouse injection"))
        return;
    CGPoint pos = CGPointMake(x, y);
    CGEventType downType = kCGEventLeftMouseDown;
    CGEventType upType = kCGEventLeftMouseUp;
    CGMouseButton btn = kCGMouseButtonLeft;

    switch (button)
    {
    case Qt::LeftButton:
        btn = kCGMouseButtonLeft;
        downType = kCGEventLeftMouseDown;
        upType = kCGEventLeftMouseUp;
        break;
    case Qt::RightButton:
        btn = kCGMouseButtonRight;
        downType = kCGEventRightMouseDown;
        upType = kCGEventRightMouseUp;
        break;
    case Qt::MiddleButton:
        btn = kCGMouseButtonCenter;
        downType = kCGEventOtherMouseDown;
        upType = kCGEventOtherMouseUp;
        break;
    }

    if (dwFlags == "move")
    {
        postMacMouseMove(pos);
    }
    else if (dwFlags == "doubleClick")
    {
        postMacMouseMoveAndWait(pos);
        CGEventRef down1 = CGEventCreateMouseEvent(nullptr, downType, pos, btn);
        CGEventRef up1 = CGEventCreateMouseEvent(nullptr, upType, pos, btn);
        CGEventRef down2 = CGEventCreateMouseEvent(nullptr, downType, pos, btn);
        CGEventRef up2 = CGEventCreateMouseEvent(nullptr, upType, pos, btn);
        if (down1 && up1 && down2 && up2)
        {
            CGEventSetIntegerValueField(down1, kCGMouseEventClickState, 1);
            CGEventSetIntegerValueField(up1, kCGMouseEventClickState, 1);
            CGEventSetIntegerValueField(down2, kCGMouseEventClickState, 2);
            CGEventSetIntegerValueField(up2, kCGMouseEventClickState, 2);
            CGEventPost(kCGHIDEventTap, down1);
            CGEventPost(kCGHIDEventTap, up1);
            CGEventPost(kCGHIDEventTap, down2);
            CGEventPost(kCGHIDEventTap, up2);
        }
        if (down1)
            CFRelease(down1);
        if (up1)
            CFRelease(up1);
        if (down2)
            CFRelease(down2);
        if (up2)
            CFRelease(up2);
    }
    else if (dwFlags == "wheel")
    {
        postMacMouseMoveAndWait(pos);
        CGEventRef event = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, mouseData / 120);
        postAndReleaseMacEvent(event);
    }
    else
    {
        postMacMouseMoveAndWait(pos);
        CGEventType type = (dwFlags == "down") ? downType : upType;
        CGEventRef event = CGEventCreateMouseEvent(nullptr, type, pos, btn);
        postAndReleaseMacEvent(event);
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

    const int x = rect.left() + static_cast<int>(qBound(0.0, x_n, 1.0) * (rect.width() - 1));
    const int y = rect.top() + static_cast<int>(qBound(0.0, y_n, 1.0) * (rect.height() - 1));

    if (!ensureMacAccessibilityReady("mouse injection"))
        return;
    CGPoint pos = CGPointMake(x, y);
    CGEventType downType = kCGEventLeftMouseDown;
    CGEventType upType = kCGEventLeftMouseUp;
    CGMouseButton btn = kCGMouseButtonLeft;

    switch (button)
    {
    case Qt::LeftButton:
        btn = kCGMouseButtonLeft;
        downType = kCGEventLeftMouseDown;
        upType = kCGEventLeftMouseUp;
        break;
    case Qt::RightButton:
        btn = kCGMouseButtonRight;
        downType = kCGEventRightMouseDown;
        upType = kCGEventRightMouseUp;
        break;
    case Qt::MiddleButton:
        btn = kCGMouseButtonCenter;
        downType = kCGEventOtherMouseDown;
        upType = kCGEventOtherMouseUp;
        break;
    default:
        return;
    }

    if (dwFlags == "move")
    {
        postMacMouseMove(pos);
    }
    else if (dwFlags == "doubleClick")
    {
        postMacMouseMoveAndWait(pos);
        CGEventRef down1 = CGEventCreateMouseEvent(nullptr, downType, pos, btn);
        CGEventRef up1 = CGEventCreateMouseEvent(nullptr, upType, pos, btn);
        CGEventRef down2 = CGEventCreateMouseEvent(nullptr, downType, pos, btn);
        CGEventRef up2 = CGEventCreateMouseEvent(nullptr, upType, pos, btn);
        if (down1 && up1 && down2 && up2)
        {
            CGEventSetIntegerValueField(down1, kCGMouseEventClickState, 1);
            CGEventSetIntegerValueField(up1, kCGMouseEventClickState, 1);
            CGEventSetIntegerValueField(down2, kCGMouseEventClickState, 2);
            CGEventSetIntegerValueField(up2, kCGMouseEventClickState, 2);
            CGEventPost(kCGHIDEventTap, down1);
            CGEventPost(kCGHIDEventTap, up1);
            CGEventPost(kCGHIDEventTap, down2);
            CGEventPost(kCGHIDEventTap, up2);
        }
        if (down1)
            CFRelease(down1);
        if (up1)
            CFRelease(up1);
        if (down2)
            CFRelease(down2);
        if (up2)
            CFRelease(up2);
    }
    else if (dwFlags == "wheel")
    {
        postMacMouseMoveAndWait(pos);
        CGEventRef event = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, mouseData / 120);
        postAndReleaseMacEvent(event);
    }
    else
    {
        postMacMouseMoveAndWait(pos);
        CGEventType type = (dwFlags == "down") ? downType : upType;
        CGEventRef event = CGEventCreateMouseEvent(nullptr, type, pos, btn);
        postAndReleaseMacEvent(event);
    }
}
#endif
