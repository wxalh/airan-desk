#include "webrtc/ctl/webrtc_ctl.h"
#include "webrtc/ctl/network/webrtc_ctl_network_path_util.h"

using webrtc_ctl_network_path::networkPathFromCandidate;
using webrtc_ctl_network_path::orderedNetworkPaths;


void WebRtcCtl::noteLocalNetworkCandidate(const QString &candidate)
{
    const QString path = networkPathFromCandidate(candidate);
    if (path.isEmpty())
        return;

    if (m_availableNetworkPaths.contains(path))
        return;

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

    const QStringList availablePaths = orderedNetworkPaths(m_availableNetworkPaths);
    if (m_hasPublishedNetworkPathState &&
        availablePaths == m_lastPublishedNetworkPaths &&
        m_selectedNetworkPath == m_lastPublishedSelectedNetworkPath &&
        m_networkPath == m_lastPublishedRequestedNetworkPath)
    {
        return;
    }

    m_hasPublishedNetworkPathState = true;
    m_lastPublishedNetworkPaths = availablePaths;
    m_lastPublishedSelectedNetworkPath = m_selectedNetworkPath;
    m_lastPublishedRequestedNetworkPath = m_networkPath;
    Q_EMIT networkPathStateChanged(availablePaths, m_selectedNetworkPath, m_networkPath);
}
