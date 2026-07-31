#include "webrtc/ctl/webrtc_ctl.h"
#include "webrtc/ctl/network/webrtc_ctl_network_path_util.h"

using webrtc_ctl_network_path::networkPathFromCandidate;
using webrtc_ctl_network_path::orderedNetworkPaths;


void WebRtcCtl::noteLocalNetworkCandidate(const QString &candidate)
{
    const QString path = networkPathFromCandidate(candidate);
    if (path.isEmpty())
        return;

    if (!m_availableNetworkPaths.contains(path))
        m_availableNetworkPaths.append(path);
    publishNetworkPathState();
}


void WebRtcCtl::publishNetworkPathState(const QString &selectedPath)
{
    if (!selectedPath.isEmpty())
    {
        m_selectedNetworkPath = selectedPath;
        if (!m_availableNetworkPaths.contains(selectedPath))
            m_availableNetworkPaths.append(selectedPath);
    }

    Q_EMIT networkPathStateChanged(orderedNetworkPaths(m_availableNetworkPaths),
                                   m_selectedNetworkPath,
                                   m_networkPath);
}
