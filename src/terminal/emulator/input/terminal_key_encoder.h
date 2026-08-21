#ifndef TERMINAL_KEY_ENCODER_H
#define TERMINAL_KEY_ENCODER_H

#include <QByteArray>
#include <Qt>

extern "C"
{
#include <vterm_keycodes.h>
}

namespace TerminalKeyEncoder
{
QByteArray controlByteForKey(int key, Qt::KeyboardModifiers modifiers);
QByteArray escapeSequenceForQtKey(int key, Qt::KeyboardModifiers modifiers);
VTermKey vtermKeyForQtKey(int key, Qt::KeyboardModifiers modifiers);
}

#endif /* TERMINAL_KEY_ENCODER_H */
