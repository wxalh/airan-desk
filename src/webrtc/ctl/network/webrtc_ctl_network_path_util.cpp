#include "webrtc/ctl/network/webrtc_ctl_network_path_util.h"

namespace webrtc_ctl_network_path
{
QString networkPathFromCandidate(const QString &candidate)
{
    const QString normalized = candidate.simplified().toLower();
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList parts = normalized.split(' ', Qt::SkipEmptyParts);
#else
    const QStringList parts = normalized.split(' ', QString::SkipEmptyParts);
#endif
    const QString protocol = parts.size() > 2 ? parts.at(2) : QString();

    if (normalized.contains(QStringLiteral(" typ relay")))
        return protocol == QStringLiteral("tcp") ? QStringLiteral("turn_tcp") : QStringLiteral("turn_udp");

    if (normalized.contains(QStringLiteral(" typ host")) ||
        normalized.contains(QStringLiteral(" typ srflx")) ||
        normalized.contains(QStringLiteral(" typ prflx")))
        return QStringLiteral("direct");

    return QString();
}

QString selectedNetworkPathFromPair(const QString &localCandidate, const QString &remoteCandidate)
{
    const QString localPath = networkPathFromCandidate(localCandidate);
    const QString remotePath = networkPathFromCandidate(remoteCandidate);

    if (localPath == QStringLiteral("turn_tcp") || remotePath == QStringLiteral("turn_tcp"))
        return QStringLiteral("turn_tcp");
    if (localPath == QStringLiteral("turn_udp") || remotePath == QStringLiteral("turn_udp"))
        return QStringLiteral("turn_udp");
    if (localPath == QStringLiteral("direct") || remotePath == QStringLiteral("direct"))
        return QStringLiteral("direct");
    return QString();
}

QStringList orderedNetworkPaths(QStringList paths)
{
    paths.removeDuplicates();
    QStringList ordered;
    for (const QString &path : {QStringLiteral("auto"), QStringLiteral("direct"), QStringLiteral("turn_udp"), QStringLiteral("turn_tcp")})
    {
        if (paths.contains(path))
            ordered.append(path);
    }
    return ordered;
}
} /* namespace webrtc_ctl_network_path */
