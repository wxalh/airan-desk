#ifndef AIRAN_TERMINAL_LOG_UTIL_H
#define AIRAN_TERMINAL_LOG_UTIL_H

#include <QString>

namespace TerminalLogUtil
{

QString defaultTerminalLogPath(const QString &remoteId, const QString &instanceId = QString());
} /* namespace TerminalLogUtil */

#endif /* AIRAN_TERMINAL_LOG_UTIL_H */
