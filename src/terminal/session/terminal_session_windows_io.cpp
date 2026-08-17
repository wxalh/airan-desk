#include "terminal_session.h"

#if defined(Q_OS_WIN)

QString TerminalSession::windowsConPtyShellNativeArguments() const
{
    return QStringLiteral("/D /K prompt $E]7;file:///$P$E\\$G$S$P$G");
}


QString TerminalSession::windowsFallbackShellNativeArguments() const
{
    // The controller performs local echo in pipe mode. Disable cmd's own
    // command echo so typed input is rendered exactly once.
    return QStringLiteral("/D /Q /K prompt $E]7;file:///$P$E\\$G$S$P$G");
}
#endif
