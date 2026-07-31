#include "util/input/input_util.h"

#if !defined(Q_OS_WIN64) && !defined(Q_OS_WIN32) && !defined(Q_OS_LINUX) && !defined(Q_OS_MACOS)
void InputUtil::execKeyboardEvent(int, const QString &)
{
}

void InputUtil::execKeyboardText(const QString &)
{
}

void InputUtil::execMouseEvent(int, qreal, qreal, int, const QString &, bool)
{
}

void InputUtil::execMouseEventOnScreen(int, qreal, qreal, int, const QString &, int, bool)
{
}

void InputUtil::execMouseEventInRect(int, qreal, qreal, int, const QString &, const QRect &, bool)
{
}

void InputUtil::execMouseEventOnDesktopSource(int, qreal, qreal, int, const QString &, int, bool)
{
}
#endif
