#include "util/input/platform/linux/input_util_events_linux_internal.h"

#if defined(Q_OS_LINUX)
#include "common/logger_manager.h"

#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QtGlobal>
#include <chrono>
#include <string>
#include <thread>

#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace input_linux_internal
{

namespace
{
int s_keyboardFd = -1;
int s_mouseFd = -1;
} // namespace

int s_screenW = 1920;
int s_screenH = 1080;
int s_screenX = 0;
int s_screenY = 0;


bool uiWrite(int fd, uint16_t type, uint16_t code, int32_t val)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = val;
     ssize_t n = ::write(fd, &ev, sizeof(ev));
    if (n != static_cast<ssize_t>(sizeof(ev)))
    {
        LOG_WARN("Failed to write uinput event: fd={}, type={}, code={}, value={}, written={}, errno={}",
                 fd, type, code, val, n, errno);
        return false;
    }
    return true;
}

void uiSync(int fd)
{
    uiWrite(fd, EV_SYN, SYN_REPORT, 0);
}

void tapKey(int fd, int key)
{
    uiWrite(fd, EV_KEY, static_cast<uint16_t>(key), 1);
    uiSync(fd);
    uiWrite(fd, EV_KEY, static_cast<uint16_t>(key), 0);
    uiSync(fd);
}

void pasteShortcut(int fd)
{
    uiWrite(fd, EV_KEY, KEY_LEFTCTRL, 1);
    uiSync(fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    tapKey(fd, KEY_V);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    uiWrite(fd, EV_KEY, KEY_LEFTCTRL, 0);
    uiSync(fd);
}

void uiMoveAbs(int fd, int x, int y)
{
    uiWrite(fd, EV_ABS, ABS_X, x);
    uiWrite(fd, EV_ABS, ABS_Y, y);
    uiSync(fd);
}

void uiMoveAbsAndWait(int fd, int x, int y)
{
    uiMoveAbs(fd, x, y);
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
}

int openUinput()
{
    int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    const int devUinputErrno = errno;
    if (fd < 0)
        fd = ::open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
    const int devInputUinputErrno = errno;
    if (fd < 0)
    {
        const std::string devUinputError = ::strerror(devUinputErrno);
        const std::string devInputUinputError = ::strerror(devInputUinputErrno);
        LOG_ERROR("Failed to open uinput device: /dev/uinput errno={} ({}), "
                  "/dev/input/uinput errno={} ({}). Hint: ENOENT usually means the uinput "
                  "kernel module is not loaded; run `sudo modprobe uinput`. EACCES usually "
                  "means the udev rule/group is missing; install resource/linux/"
                  "60-airan-desk-uinput.rules, reload udev, add the user to the input group, "
                  "and log in again.",
                  devUinputErrno,
                  devUinputError,
                  devInputUinputErrno,
                  devInputUinputError);
    }
    return fd;
}

void updateScreenSize()
{
    QRect virtualRect;
    qreal maxDpr = 1.0;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens)
    {
        if (!screen)
            continue;
        virtualRect = virtualRect.isNull() ? screen->geometry() : virtualRect.united(screen->geometry());
        maxDpr = qMax(maxDpr, screen->devicePixelRatio());
    }
    if (!virtualRect.isValid())
        return;

    s_screenX = static_cast<int>(virtualRect.x() * maxDpr);
    s_screenY = static_cast<int>(virtualRect.y() * maxDpr);
    s_screenW = qMax(1, static_cast<int>(virtualRect.width() * maxDpr));
    s_screenH = qMax(1, static_cast<int>(virtualRect.height() * maxDpr));
}
 
int getKeyboardFd()
{
    if (s_keyboardFd >= 0)
        return s_keyboardFd;
     s_keyboardFd = openUinput();
    if (s_keyboardFd < 0)
        return -1;
     ioctl(s_keyboardFd, UI_SET_EVBIT, EV_KEY);
    ioctl(s_keyboardFd, UI_SET_EVBIT, EV_SYN);
    ioctl(s_keyboardFd, UI_SET_EVBIT, EV_REP);
     
    for (int i = 1; i < BTN_MISC; ++i)
        ioctl(s_keyboardFd, UI_SET_KEYBIT, i);
     struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    strncpy(uidev.name, "airan-desk virtual keyboard", UINPUT_MAX_NAME_SIZE - 1);
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor = 0x1d6b;
    uidev.id.product = 0x0101;
    uidev.id.version = 1;
     if (::write(s_keyboardFd, &uidev, sizeof(uidev)) != static_cast<ssize_t>(sizeof(uidev)) ||
        ioctl(s_keyboardFd, UI_DEV_CREATE) < 0)
    {
        LOG_ERROR("Failed to create uinput virtual keyboard, errno={}", errno);
        ::close(s_keyboardFd);
        s_keyboardFd = -1;
        return -1;
    }
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOG_INFO("Created uinput virtual keyboard device: fd={}", s_keyboardFd);
    return s_keyboardFd;
}
 
int getMouseFd()
{
    if (s_mouseFd >= 0)
        return s_mouseFd;
     s_mouseFd = openUinput();
    if (s_mouseFd < 0)
        return -1;
     updateScreenSize();
     ioctl(s_mouseFd, UI_SET_EVBIT, EV_KEY);
    ioctl(s_mouseFd, UI_SET_EVBIT, EV_ABS);
    ioctl(s_mouseFd, UI_SET_EVBIT, EV_REL);
    ioctl(s_mouseFd, UI_SET_EVBIT, EV_SYN);
#ifdef UI_SET_PROPBIT
    ioctl(s_mouseFd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
#endif
     ioctl(s_mouseFd, UI_SET_KEYBIT, BTN_MOUSE);
    ioctl(s_mouseFd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(s_mouseFd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(s_mouseFd, UI_SET_KEYBIT, BTN_MIDDLE);
     ioctl(s_mouseFd, UI_SET_ABSBIT, ABS_X);
    ioctl(s_mouseFd, UI_SET_ABSBIT, ABS_Y);
    ioctl(s_mouseFd, UI_SET_RELBIT, REL_WHEEL);
     struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    strncpy(uidev.name, "airan-desk virtual mouse", UINPUT_MAX_NAME_SIZE - 1);
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor = 0x1d6b;
    uidev.id.product = 0x0102;
    uidev.id.version = 1;
    uidev.absfuzz[ABS_X] = 0;
    uidev.absflat[ABS_X] = 0;
    uidev.absfuzz[ABS_Y] = 0;
    uidev.absflat[ABS_Y] = 0;
    uidev.absmin[ABS_X] = 0;
    uidev.absmax[ABS_X] = s_screenW - 1;
    uidev.absmin[ABS_Y] = 0;
    uidev.absmax[ABS_Y] = s_screenH - 1;
     if (::write(s_mouseFd, &uidev, sizeof(uidev)) != static_cast<ssize_t>(sizeof(uidev)) ||
        ioctl(s_mouseFd, UI_DEV_CREATE) < 0)
    {
        LOG_ERROR("Failed to create uinput virtual mouse, errno={}", errno);
        ::close(s_mouseFd);
        s_mouseFd = -1;
        return -1;
    }
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOG_INFO("Created uinput virtual mouse device: screen={}x{}, fd={}", s_screenW, s_screenH, s_mouseFd);
    return s_mouseFd;
}

} // namespace input_linux_internal
#endif
