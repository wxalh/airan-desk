#include "rtc/signaling/rtc_sdp_simulcast_answer.h"

#include <api/media_types.h>
#include <media/base/rid_description.h>
#include <pc/session_description.h>
#include <pc/simulcast_description.h>

#include <algorithm>
#include <vector>

namespace rtc
{
namespace
{
#if AIRAN_WEBRTC_MILESTONE >= 144
using NativeRidDescription = webrtc::RidDescription;
using NativeRidDirection = webrtc::RidDirection;
using NativeSimulcastDescription = webrtc::SimulcastDescription;
using NativeSimulcastLayerList = webrtc::SimulcastLayerList;
constexpr auto kNativeVideoMediaType = webrtc::MediaType::VIDEO;
#else
using NativeRidDescription = cricket::RidDescription;
using NativeRidDirection = cricket::RidDirection;
using NativeSimulcastDescription = cricket::SimulcastDescription;
using NativeSimulcastLayerList = cricket::SimulcastLayerList;
constexpr auto kNativeVideoMediaType = cricket::MEDIA_TYPE_VIDEO;
#endif
} // namespace

size_t acceptRemoteVideoSimulcastInAnswer(webrtc::PeerConnectionInterface &pc,
                                          webrtc::SessionDescriptionInterface &answer)
{
    if (answer.type() != "answer" || !pc.remote_description() ||
        !answer.description() || !pc.remote_description()->description())
        return 0;

    size_t acceptedLayers = 0;
    const auto &remoteContents = pc.remote_description()->description()->contents();
    auto &answerContents = answer.description()->contents();
    const size_t count = (std::min)(remoteContents.size(), answerContents.size());
    for (size_t index = 0; index < count; ++index)
    {
        const auto *remoteMedia = remoteContents[index].media_description();
        auto *answerMedia = answerContents[index].media_description();
        if (!remoteMedia || !answerMedia || remoteMedia->type() != kNativeVideoMediaType ||
            answerMedia->type() != kNativeVideoMediaType || !remoteMedia->HasSimulcast())
            continue;

        const auto sendLayers = remoteMedia->simulcast_description().send_layers().GetAllLayers();
        if (sendLayers.size() <= 1)
            continue;

        std::vector<NativeRidDescription> receiveRids;
        receiveRids.reserve(sendLayers.size());
        for (const auto &layer : sendLayers)
            receiveRids.emplace_back(layer.rid, NativeRidDirection::kReceive);

        NativeSimulcastDescription simulcast = answerMedia->simulcast_description();
        simulcast.receive_layers() = NativeSimulcastLayerList();
        for (const auto &layer : sendLayers)
            simulcast.receive_layers().AddLayer(layer);
        answerMedia->set_receive_rids(receiveRids);
        answerMedia->set_simulcast_description(simulcast);
        acceptedLayers += sendLayers.size();
    }
    return acceptedLayers;
}

} // namespace rtc
