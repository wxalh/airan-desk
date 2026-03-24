#include "input_util.h"
#include <QRect>
#include <QScreen>
#include <QGuiApplication>
#include <cmath>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#elif defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>
#endif

#if defined(Q_OS_LINUX)
namespace {

static int  s_uiFd    = -1; // uinput 虚拟设备 fd，进程生命周期内保持打开
static int  s_screenW = 1920;
static int  s_screenH = 1080;

// Windows VK 码 → Linux input keycode
static int vkToLinux(int vk)
{
    switch (vk) {
    case 0x08: return KEY_BACKSPACE;
    case 0x09: return KEY_TAB;
    case 0x0D: return KEY_ENTER;
    case 0x10: return KEY_LEFTSHIFT;
    case 0x11: return KEY_LEFTCTRL;
    case 0x12: return KEY_LEFTALT;
    case 0x13: return KEY_PAUSE;
    case 0x14: return KEY_CAPSLOCK;
    case 0x1B: return KEY_ESC;
    case 0x20: return KEY_SPACE;
    case 0x21: return KEY_PAGEUP;
    case 0x22: return KEY_PAGEDOWN;
    case 0x23: return KEY_END;
    case 0x24: return KEY_HOME;
    case 0x25: return KEY_LEFT;
    case 0x26: return KEY_UP;
    case 0x27: return KEY_RIGHT;
    case 0x28: return KEY_DOWN;
    case 0x2A: return KEY_SYSRQ;  // VK_PRINT
    case 0x2C: return KEY_SYSRQ;  // VK_SNAPSHOT
    case 0x2D: return KEY_INSERT;
    case 0x2E: return KEY_DELETE;
    case 0x30: return KEY_0;
    case 0x31: return KEY_1;
    case 0x32: return KEY_2;
    case 0x33: return KEY_3;
    case 0x34: return KEY_4;
    case 0x35: return KEY_5;
    case 0x36: return KEY_6;
    case 0x37: return KEY_7;
    case 0x38: return KEY_8;
    case 0x39: return KEY_9;
    case 0x41: return KEY_A;
    case 0x42: return KEY_B;
    case 0x43: return KEY_C;
    case 0x44: return KEY_D;
    case 0x45: return KEY_E;
    case 0x46: return KEY_F;
    case 0x47: return KEY_G;
    case 0x48: return KEY_H;
    case 0x49: return KEY_I;
    case 0x4A: return KEY_J;
    case 0x4B: return KEY_K;
    case 0x4C: return KEY_L;
    case 0x4D: return KEY_M;
    case 0x4E: return KEY_N;
    case 0x4F: return KEY_O;
    case 0x50: return KEY_P;
    case 0x51: return KEY_Q;
    case 0x52: return KEY_R;
    case 0x53: return KEY_S;
    case 0x54: return KEY_T;
    case 0x55: return KEY_U;
    case 0x56: return KEY_V;
    case 0x57: return KEY_W;
    case 0x58: return KEY_X;
    case 0x59: return KEY_Y;
    case 0x5A: return KEY_Z;
    case 0x5D: return KEY_COMPOSE;    // VK_APPS / Menu
    case 0x6A: return KEY_KPASTERISK; // VK_MULTIPLY
    case 0x70: return KEY_F1;
    case 0x71: return KEY_F2;
    case 0x72: return KEY_F3;
    case 0x73: return KEY_F4;
    case 0x74: return KEY_F5;
    case 0x75: return KEY_F6;
    case 0x76: return KEY_F7;
    case 0x77: return KEY_F8;
    case 0x78: return KEY_F9;
    case 0x79: return KEY_F10;
    case 0x7A: return KEY_F11;
    case 0x7B: return KEY_F12;
    case 0x7C: return KEY_F13;
    case 0x7D: return KEY_F14;
    case 0x7E: return KEY_F15;
    case 0x7F: return KEY_F16;
    case 0x80: return KEY_F17;
    case 0x81: return KEY_F18;
    case 0x82: return KEY_F19;
    case 0x83: return KEY_F20;
    case 0x84: return KEY_F21;
    case 0x85: return KEY_F22;
    case 0x86: return KEY_F23;
    case 0x87: return KEY_F24;
    case 0x90: return KEY_NUMLOCK;
    case 0x91: return KEY_SCROLLLOCK;
    case 0xAD: return KEY_MUTE;
    case 0xAE: return KEY_VOLUMEDOWN;
    case 0xAF: return KEY_VOLUMEUP;
    case 0xB2: return KEY_STOPCD;
    case 0xB3: return KEY_PLAYPAUSE;
    case 0xBA: return KEY_SEMICOLON;
    case 0xBB: return KEY_EQUAL;
    case 0xBC: return KEY_COMMA;
    case 0xBD: return KEY_MINUS;
    case 0xBE: return KEY_DOT;
    case 0xBF: return KEY_SLASH;
    case 0xC0: return KEY_GRAVE;
    case 0xDB: return KEY_LEFTBRACE;
    case 0xDC: return KEY_BACKSLASH;
    case 0xDD: return KEY_RIGHTBRACE;
    case 0xDE: return KEY_APOSTROPHE;
    default:   return -1;
    }
}

// 写入一条 input_event
static void uiWrite(int fd, uint16_t type, uint16_t code, int32_t val)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type  = type;
    ev.code  = code;
    ev.value = val;
    ::write(fd, &ev, sizeof(ev));
}

// 获取（按需创建）uinput 虚拟设备 fd
static int getUiFd()
{
    if (s_uiFd >= 0) return s_uiFd;

    // 尝试两个常见路径
    s_uiFd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (s_uiFd < 0)
        s_uiFd = ::open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
    if (s_uiFd < 0) return -1;

    // 读取屏幕物理分辨率
    QScreen *sc = QGuiApplication::primaryScreen();
    if (sc) {
        qreal dpr = sc->devicePixelRatio();
        s_screenW = static_cast<int>(sc->geometry().width()  * dpr);
        s_screenH = static_cast<int>(sc->geometry().height() * dpr);
    }

    // 声明事件类型
    ioctl(s_uiFd, UI_SET_EVBIT, EV_KEY);
    ioctl(s_uiFd, UI_SET_EVBIT, EV_ABS);
    ioctl(s_uiFd, UI_SET_EVBIT, EV_REL);
    ioctl(s_uiFd, UI_SET_EVBIT, EV_SYN);

    // 开放所有键盘按键位
    for (int i = 0; i < KEY_MAX; i++)
        ioctl(s_uiFd, UI_SET_KEYBIT, i);

    // 鼠标按键
    ioctl(s_uiFd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(s_uiFd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(s_uiFd, UI_SET_KEYBIT, BTN_MIDDLE);

    // 绝对坐标轴（鼠标定位）
    ioctl(s_uiFd, UI_SET_ABSBIT, ABS_X);
    ioctl(s_uiFd, UI_SET_ABSBIT, ABS_Y);

    // 相对轴（滚轮）
    ioctl(s_uiFd, UI_SET_RELBIT, REL_WHEEL);

    // 使用 uinput_user_dev（兼容内核 3.x+，无需 UI_DEV_SETUP）
    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    strncpy(uidev.name, "airan-desk virtual input", UINPUT_MAX_NAME_SIZE);
    uidev.id.bustype = BUS_VIRTUAL;
    uidev.id.vendor  = 0x1d6b; // Linux Foundation
    uidev.id.product = 0x0101;
    uidev.id.version = 1;
    uidev.absmin[ABS_X] = 0;          uidev.absmax[ABS_X] = s_screenW - 1;
    uidev.absmin[ABS_Y] = 0;          uidev.absmax[ABS_Y] = s_screenH - 1;
    ::write(s_uiFd, &uidev, sizeof(uidev));

    if (ioctl(s_uiFd, UI_DEV_CREATE) < 0) {
        ::close(s_uiFd);
        s_uiFd = -1;
        return -1;
    }
    return s_uiFd;
}

} // namespace
#endif // Q_OS_LINUX

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
    // Linux 实现 (uinput 内核虚拟设备，X11/Wayland 均可用)
    int fd = getUiFd();
    if (fd < 0) return;
    int linuxKey = vkToLinux(keyCode);
    if (linuxKey < 0) return;
    uiWrite(fd, EV_KEY, static_cast<uint16_t>(linuxKey), dwFlags == "down" ? 1 : 0);
    uiWrite(fd, EV_SYN, SYN_REPORT, 0);

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
    // Linux 实现 (uinput 内核虚拟设备，X11/Wayland 均可用)
    int fd = getUiFd();
    if (fd < 0) return;

    // 坐标已由调用方基于 scaleFactor 折算为物理像素，直接映射到绝对坐标
    uiWrite(fd, EV_ABS, ABS_X, x);
    uiWrite(fd, EV_ABS, ABS_Y, y);

    if (dwFlags == "move") {
        uiWrite(fd, EV_SYN, SYN_REPORT, 0);
        return;
    }

    if (dwFlags == "wheel") {
        uiWrite(fd, EV_REL, REL_WHEEL, mouseData > 0 ? 1 : -1);
        uiWrite(fd, EV_SYN, SYN_REPORT, 0);
        return;
    }

    int btn;
    switch (button) {
    case Qt::LeftButton:   btn = BTN_LEFT;   break;
    case Qt::RightButton:  btn = BTN_RIGHT;  break;
    case Qt::MiddleButton: btn = BTN_MIDDLE; break;
    default:
        uiWrite(fd, EV_SYN, SYN_REPORT, 0);
        return;
    }

    if (dwFlags == "doubleClick") {
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 1);
        uiWrite(fd, EV_SYN, SYN_REPORT, 0);
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 0);
        uiWrite(fd, EV_SYN, SYN_REPORT, 0);
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 1);
        uiWrite(fd, EV_SYN, SYN_REPORT, 0);
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), 0);
    } else {
        uiWrite(fd, EV_KEY, static_cast<uint16_t>(btn), dwFlags == "down" ? 1 : 0);
    }
    uiWrite(fd, EV_SYN, SYN_REPORT, 0);

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
