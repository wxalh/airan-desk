#ifndef SIGNALING_URL_RESOLVER_H
#define SIGNALING_URL_RESOLVER_H

#include <QString>

namespace SignalingUrlResolver
{
enum class State
{
    NotConfigured,
    Invalid,
    Ready
};

struct Result
{
    State state{State::NotConfigured};
    QString url;
};

Result resolve(const QString &configuredUrl,
               const QString &sessionId,
               const QString &hostname,
               const QString &installId);
}

#endif /* SIGNALING_URL_RESOLVER_H */
