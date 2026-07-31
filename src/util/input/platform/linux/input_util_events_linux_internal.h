#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <cstdint>

#if defined(Q_OS_LINUX)
namespace input_linux_internal
{

extern int s_screenW;
extern int s_screenH;
extern int s_screenX;
extern int s_screenY;

struct ClipboardTool
{
    QString program;
    QStringList readArgs;
    QStringList writeArgs;
};


int vkToLinux(int vk);
bool uiWrite(int fd, uint16_t type, uint16_t code, int32_t val);
void uiSync(int fd);
void tapKey(int fd, int key);
void pasteShortcut(int fd);
void uiMoveAbs(int fd, int x, int y);
void uiMoveAbsAndWait(int fd, int x, int y);
void updateScreenSize();
int getKeyboardFd();
int getMouseFd();
ClipboardTool findClipboardTool();
bool setClipboardText(const ClipboardTool &tool, const QByteArray &bytes);
bool readClipboardText(const ClipboardTool &tool, QByteArray *out);

} // namespace input_linux_internal
#endif
