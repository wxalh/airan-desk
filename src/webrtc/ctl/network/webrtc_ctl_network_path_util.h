#ifndef WEBRTC_CTL_NETWORK_PATH_UTIL_H
#define WEBRTC_CTL_NETWORK_PATH_UTIL_H

#include "rtc/core/rtc_media_types.h"

#include <QString>
#include <QStringList>

namespace webrtc_ctl_network_path
{

QString networkPathFromCandidate(const QString &candidate);


QString selectedNetworkPathFromPair(const QString &localCandidate, const QString &remoteCandidate);

QString selectedNetworkPathFromPair(const rtc::SelectedCandidatePair &pair);


QStringList orderedNetworkPaths(QStringList paths);
} /* namespace webrtc_ctl_network_path */

#endif /* WEBRTC_CTL_NETWORK_PATH_UTIL_H */
