#include "rtc/core/rtc_internal.h"
#include "rtc/media/codec/preferences/rtc_codec_preference_ranking.h"

#include "common/logger_manager.h"

#include <QStringList>

namespace rtc
{

void applyAiranVideoCodecPreferences(const scoped_refptr<webrtc::RtpTransceiverInterface> &transceiver,
                                     const scoped_refptr<webrtc::PeerConnectionFactoryInterface> &factory,
                                     bool senderCapabilities,
                                     const std::vector<VideoCodecCapability> *remoteCapabilities,
                                     int targetWidth,
                                     int targetHeight,
                                     int targetFps)
{
    if (!transceiver)
        return;

    auto codecs = hardwareFirstVideoCodecs(factory,
                                           senderCapabilities,
                                           remoteCapabilities,
                                           targetWidth,
                                           targetHeight,
                                           targetFps);
    if (codecs.empty())
    {
        LOG_WARN("Cannot apply Airan video codec preferences: no codec capabilities");
        return;
    }

    QStringList ordered;
    for (const auto &codec : codecs)
        ordered.append(QString::fromStdString(codec.mime_type()));

    const auto error = transceiver->SetCodecPreferences(codecs);
    if (error.ok())
        LOG_INFO("Applied Airan video codec preferences: direction={}, remoteCapabilities={}, target={}x{}@{}, codecs={}",
                 senderCapabilities ? "send" : "receive",
                 remoteCapabilities ? remoteCapabilities->size() : 0,
                 targetWidth,
                 targetHeight,
                 targetFps,
                 ordered.join(","));
    else
        LOG_WARN("Failed to apply video codec preferences: {}", error.message());
}
} // namespace rtc
