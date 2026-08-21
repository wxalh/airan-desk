#include "terminal/emulator/input/terminal_key_encoder.h"

namespace TerminalKeyEncoder
{
QByteArray controlByteForKey(int key, Qt::KeyboardModifiers modifiers)
{
    if (!modifiers.testFlag(Qt::ControlModifier) ||
        modifiers.testFlag(Qt::AltModifier) || modifiers.testFlag(Qt::MetaModifier))
        return QByteArray();

    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return QByteArray(1, static_cast<char>(key - Qt::Key_A + 1));

    switch (key)
    {
    case Qt::Key_Exclam:
        return QByteArray(1, '\x01');
    case Qt::Key_Space:
    case Qt::Key_At:
    case Qt::Key_QuoteLeft:
    case Qt::Key_2:
        return QByteArray(1, '\0');
    case Qt::Key_NumberSign:
    case Qt::Key_3:
        return QByteArray(1, '\x1b');
    case Qt::Key_Dollar:
    case Qt::Key_4:
        return QByteArray(1, '\x1c');
    case Qt::Key_Percent:
    case Qt::Key_5:
        return QByteArray(1, '\x1d');
    case Qt::Key_Ampersand:
        return QByteArray(1, '\x06');
    case Qt::Key_Asterisk:
        return QByteArray(1, '\x07');
    case Qt::Key_ParenLeft:
        return QByteArray(1, '\x08');
    case Qt::Key_ParenRight:
        return QByteArray(1, '\x09');
    case Qt::Key_6:
        return QByteArray(1, '\x1e');
    case Qt::Key_7:
        return QByteArray(1, '\x1f');
    case Qt::Key_BracketLeft:
    case Qt::Key_BraceLeft:
        return QByteArray(1, '\x1b');
    case Qt::Key_Backslash:
    case Qt::Key_Bar:
        return QByteArray(1, '\x1c');
    case Qt::Key_BracketRight:
    case Qt::Key_BraceRight:
        return QByteArray(1, '\x1d');
    case Qt::Key_AsciiCircum:
    case Qt::Key_AsciiTilde:
        return QByteArray(1, '\x1e');
    case Qt::Key_Underscore:
    case Qt::Key_Minus:
        return QByteArray(1, '\x1f');
    case Qt::Key_Question:
    case Qt::Key_8:
        return QByteArray(1, '\x7f');
    default:
        return QByteArray();
    }
}

QByteArray escapeSequenceForQtKey(int key, Qt::KeyboardModifiers modifiers)
{
    if (key < Qt::Key_F13 || key > Qt::Key_F24 || modifiers.testFlag(Qt::MetaModifier))
        return QByteArray();

    // libvterm currently emits F1-F12; keep the standard xterm F13-F24 forms here.
    static const int functionCodes[] = {
        25, 26, 28, 29, 31, 32, 33, 34, 35, 36, 37, 38};
    const int code = functionCodes[key - Qt::Key_F13];
    int vtermModifiers = VTERM_MOD_NONE;
    if (modifiers.testFlag(Qt::ShiftModifier))
        vtermModifiers |= VTERM_MOD_SHIFT;
    if (modifiers.testFlag(Qt::AltModifier))
        vtermModifiers |= VTERM_MOD_ALT;
    if (modifiers.testFlag(Qt::ControlModifier))
        vtermModifiers |= VTERM_MOD_CTRL;

    QByteArray sequence = QByteArrayLiteral("\x1b[") + QByteArray::number(code);
    if (vtermModifiers != VTERM_MOD_NONE)
        sequence += QByteArrayLiteral(";") + QByteArray::number(vtermModifiers + 1);
    sequence += '~';
    return sequence;
}

VTermKey vtermKeyForQtKey(int key, Qt::KeyboardModifiers modifiers)
{
    if (modifiers.testFlag(Qt::KeypadModifier))
    {
        switch (key)
        {
        case Qt::Key_0: return VTERM_KEY_KP_0;
        case Qt::Key_1: return VTERM_KEY_KP_1;
        case Qt::Key_2: return VTERM_KEY_KP_2;
        case Qt::Key_3: return VTERM_KEY_KP_3;
        case Qt::Key_4: return VTERM_KEY_KP_4;
        case Qt::Key_5: return VTERM_KEY_KP_5;
        case Qt::Key_6: return VTERM_KEY_KP_6;
        case Qt::Key_7: return VTERM_KEY_KP_7;
        case Qt::Key_8: return VTERM_KEY_KP_8;
        case Qt::Key_9: return VTERM_KEY_KP_9;
        case Qt::Key_Asterisk: return VTERM_KEY_KP_MULT;
        case Qt::Key_Plus: return VTERM_KEY_KP_PLUS;
        case Qt::Key_Comma: return VTERM_KEY_KP_COMMA;
        case Qt::Key_Minus: return VTERM_KEY_KP_MINUS;
        case Qt::Key_Period: return VTERM_KEY_KP_PERIOD;
        case Qt::Key_Slash: return VTERM_KEY_KP_DIVIDE;
        case Qt::Key_Enter:
        case Qt::Key_Return: return VTERM_KEY_KP_ENTER;
        case Qt::Key_Equal: return VTERM_KEY_KP_EQUAL;
        default: break;
        }
    }

    switch (key)
    {
    case Qt::Key_Return:
    case Qt::Key_Enter: return VTERM_KEY_ENTER;
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return VTERM_KEY_TAB;
    case Qt::Key_Backspace: return VTERM_KEY_BACKSPACE;
    case Qt::Key_Escape: return VTERM_KEY_ESCAPE;
    case Qt::Key_Up: return VTERM_KEY_UP;
    case Qt::Key_Down: return VTERM_KEY_DOWN;
    case Qt::Key_Left: return VTERM_KEY_LEFT;
    case Qt::Key_Right: return VTERM_KEY_RIGHT;
    case Qt::Key_Insert: return VTERM_KEY_INS;
    case Qt::Key_Delete: return VTERM_KEY_DEL;
    case Qt::Key_Home: return VTERM_KEY_HOME;
    case Qt::Key_End: return VTERM_KEY_END;
    case Qt::Key_PageUp: return VTERM_KEY_PAGEUP;
    case Qt::Key_PageDown: return VTERM_KEY_PAGEDOWN;
    default:
        if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
            return static_cast<VTermKey>(VTERM_KEY_FUNCTION(key - Qt::Key_F1 + 1));
        return VTERM_KEY_NONE;
    }
}
} // namespace TerminalKeyEncoder
