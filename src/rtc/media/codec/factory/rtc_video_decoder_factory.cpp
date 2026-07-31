#include "rtc/peer/factory/rtc_peer_connection_factories.h"
#include "media/codec/webrtc/airan_codec_adapter.h"
#include "common/logger_manager.h"

namespace rtc
{
std::unique_ptr<webrtc::VideoDecoderFactory> createAiranVideoDecoderFactory()
{
    LOG_INFO("Creating Airan video decoder factory; platform software fallback is enabled when configured by the media backend");
    return airan::media::createAiranVideoDecoderFactory();
}
} /* namespace rtc */
