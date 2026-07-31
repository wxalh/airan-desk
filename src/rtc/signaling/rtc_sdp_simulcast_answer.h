#pragma once

#include <api/jsep.h>
#include <api/peer_connection_interface.h>

namespace rtc
{

size_t acceptRemoteVideoSimulcastInAnswer(webrtc::PeerConnectionInterface &pc,
                                          webrtc::SessionDescriptionInterface &answer);

} // namespace rtc
