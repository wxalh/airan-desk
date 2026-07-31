#pragma once

#include <api/data_channel_interface.h>
#include <api/jsep.h>
#include <api/media_stream_interface.h>
#include <api/peer_connection_interface.h>
#include <api/rtp_parameters.h>
#include <api/rtp_receiver_interface.h>
#include <api/rtp_sender_interface.h>
#include <api/rtp_transceiver_interface.h>
#include <api/scoped_refptr.h>
#include <api/video/video_sink_interface.h>
#include <api/video/video_source_interface.h>
#include <media/base/adapted_video_track_source.h>
#include <media/base/video_broadcaster.h>
#include <pc/video_track_source.h>
#include <rtc_base/copy_on_write_buffer.h>
#include <rtc_base/logging.h>
#include <rtc_base/ssl_adapter.h>
#include <rtc_base/thread.h>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace rtc
{
#if AIRAN_WEBRTC_MILESTONE >= 144
template <typename T>
using scoped_refptr = webrtc::scoped_refptr<T>;
template <typename T>
using VideoSinkInterface = webrtc::VideoSinkInterface<T>;
template <typename T>
using VideoSourceInterface = webrtc::VideoSourceInterface<T>;
using VideoSinkWants = webrtc::VideoSinkWants;
using VideoBroadcaster = webrtc::VideoBroadcaster;
using AdaptedVideoTrackSource = webrtc::AdaptedVideoTrackSource;
using Thread = webrtc::Thread;
using CopyOnWriteBuffer = webrtc::CopyOnWriteBuffer;
using LogSink = webrtc::LogSink;
using LogMessage = webrtc::LogMessage;
using LoggingSeverity = webrtc::LoggingSeverity;
using webrtc::CleanupSSL;
using webrtc::InitializeSSL;
using webrtc::make_ref_counted;
using NativeIceCandidate = webrtc::IceCandidate;
#else
using NativeIceCandidate = webrtc::IceCandidateInterface;
using AdaptedVideoTrackSource = ::rtc::AdaptedVideoTrackSource;
#endif

using binary = std::vector<std::byte>;
using message_variant = std::variant<binary, std::string>;


struct FrameInfo
{
    double timestamp{0.0};
    std::optional<std::chrono::duration<double>> timestampSeconds;

    FrameInfo() = default;
    explicit FrameInfo(std::chrono::duration<double> ts)
        : timestamp(static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(ts).count())), timestampSeconds(ts)
    {
    }
};

} // namespace rtc
