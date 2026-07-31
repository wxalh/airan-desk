#include "signaling_url_resolver.h"

#include <QUrl>
#include <QUrlQuery>

namespace SignalingUrlResolver
{
Result resolve(const QString &configuredUrl,
               const QString &sessionId,
               const QString &hostname,
               const QString &installId)
{
    const QString trimmed = configuredUrl.trimmed();
    if (trimmed.isEmpty())
        return {State::NotConfigured, QString()};

    QUrl url(trimmed, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty() ||
        (scheme != QStringLiteral("ws") && scheme != QStringLiteral("wss")))
    {
        return {State::Invalid, QString()};
    }

    QUrlQuery query(url);
    query.removeQueryItem(QStringLiteral("sessionId"));
    query.removeQueryItem(QStringLiteral("hostname"));
    query.removeQueryItem(QStringLiteral("installId"));
    query.addQueryItem(QStringLiteral("sessionId"), sessionId);
    query.addQueryItem(QStringLiteral("hostname"), hostname);
    query.addQueryItem(QStringLiteral("installId"), installId);
    url.setQuery(query);
    return {State::Ready, url.toString()};
}
}
