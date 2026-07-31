#include "terminal_session.h"

#if defined(Q_OS_WIN)

QString TerminalSession::windowsConPtyShellNativeArguments() const
{
    return QStringLiteral("/D /K prompt $E]7;file:///$P$E\\$P$G");
}


QString TerminalSession::windowsFallbackShellNativeArguments() const
{
    return QStringLiteral("/D /K prompt $E]7;file:///$P$E\\$P$G");
}
#endif
