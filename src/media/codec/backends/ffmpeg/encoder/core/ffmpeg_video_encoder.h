#pragma once

#include "media/capture/core/airan_capture_interface.h"
#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_types.h"

#include <api/video_codecs/video_encoder.h>

#if defined(AIRAN_HAVE_FFMPEG)
namespace airan::media::ffmpeg
{

enum class FfmpegEncodeAttemptStage
{
    NativeGpu,
    CpuHardware,
    Software,
};

class FfmpegVideoEncoder final : public webrtc::VideoEncoder
{
public:
    
    explicit FfmpegVideoEncoder(CodecKind codec, webrtc::H264PacketizationMode packetizationMode);

    
    ~FfmpegVideoEncoder() override;

    
    int InitEncode(const webrtc::VideoCodec *codecSettings, const Settings &settings) override;

    
    int32_t RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback *callback) override;

    
    int32_t Release() override;

    
    int32_t Encode(const webrtc::VideoFrame &frame, const std::vector<webrtc::VideoFrameType> *frameTypes) override;

    
    void SetRates(const RateControlParameters &parameters) override;
    void OnPacketLossRateUpdate(float packet_loss_rate) override;
    void OnRttUpdate(int64_t rtt_ms) override;
    void OnLossNotification(const LossNotification &loss_notification) override;

    
    EncoderInfo GetEncoderInfo() const override;

private:
    
    int configureProbe(size_t probeIndex);

    
    bool configureStage(FfmpegEncodeAttemptStage stage, size_t startProbeIndex, const char *reason);

    
    bool advanceProbe(const char *reason);

    
    bool shouldDelayNativeSessionOpen() const;

    
    bool openCurrentContext();

    
    bool setupHardwareFrames();

    
    AVFrame *createHardwareFrameFromNativeD3D11(const webrtc::VideoFrame &frame);

    
    bool rebindD3D11EncoderSession(AVBufferRef *device, AVBufferRef *frames);

    
    bool rebindQsvEncoderSession(AVBufferRef *device, AVBufferRef *frames);

    
    bool rebindQsvEncoderSessionWithFrames(AVBufferRef *frames);

    
    bool ensureQsvHwMapGraph(AVFrame *d3d11Frame);

    
    AVFrame *mapD3D11FrameToQsv(AVFrame *d3d11Frame);

    
    void releaseQsvHwMapGraph();

    
    void releaseD3D11VideoProcessor();

    
    int32_t encodeFrame(const webrtc::VideoFrame &frame,
                        const std::vector<webrtc::VideoFrameType> *frameTypes,
                        AVFrame *inputFrame);

    int configuredTemporalLayers() const;
    int configuredSpatialLayers() const;
    bool hasHardwareEncodeProbe() const;
    bool hasNativeHandleEncodeProbe() const;
    uint32_t currentTargetBitrateBps() const;
    uint32_t currentMaxBitrateBps() const;
    int currentMaxQp() const;

    CodecKind m_codec;
    std::vector<const CodecProbe *> m_probes;
    const CodecProbe *m_probe = nullptr;
    size_t m_probeIndex = 0;
    FfmpegEncodeAttemptStage m_attemptStage = FfmpegEncodeAttemptStage::NativeGpu;
    webrtc::EncodedImageCallback *m_callback = nullptr;
    webrtc::H264PacketizationMode m_packetizationMode = webrtc::H264PacketizationMode::NonInterleaved;
    webrtc::VideoCodec m_codecSettings{};
    webrtc::VideoEncoder::Settings m_encoderSettings{
        webrtc::VideoEncoder::Capabilities(false),
        1,
        0};
    webrtc::VideoBitrateAllocation m_targetBitrateAllocation;
    webrtc::VideoBitrateAllocation m_adjustedBitrateAllocation;
    bool m_hasCodecSettings = false;
    AVCodecContext *m_ctx = nullptr;
    AVPacket *m_packet = nullptr;
    AVBufferRef *m_hwDevice = nullptr;
    AVBufferRef *m_hwFrames = nullptr;
    AVBufferRef *m_nativeD3D11Device = nullptr;
    AVBufferRef *m_nativeD3D11Frames = nullptr;
    void *m_d3d11VideoDevice = nullptr;
    void *m_d3d11VideoContext = nullptr;
    void *m_d3d11VideoEnumerator = nullptr;
    void *m_d3d11VideoProcessor = nullptr;
    void *m_d3d11Nv12Texture = nullptr;
    int m_d3d11InputWidth = 0;
    int m_d3d11InputHeight = 0;
    int m_d3d11OutputWidth = 0;
    int m_d3d11OutputHeight = 0;
    int m_d3d11IntermediateFormat = 0;
    AVFilterGraph *m_qsvFilterGraph = nullptr;
    AVFilterContext *m_qsvBufferSrc = nullptr;
    AVFilterContext *m_qsvBufferSink = nullptr;
    AVBufferRef *m_qsvFilterFrames = nullptr;
    int m_qsvFilterWidth = 0;
    int m_qsvFilterHeight = 0;
    bool m_nativeD3D11SessionBound = false;
    bool m_nativeD3D11Healthy = true;
    bool m_nativeD3D11DisabledForSession = false;
    bool m_fatalEncoderError = false;
    bool m_mediaPaused = false;
    bool m_forceKeyFrameOnResume = false;
    bool m_forceKeyFrameForRecovery = false;
    bool m_dropNextFrameByCallback = false;
    uint32_t m_targetBitrateBps = 0;
    uint32_t m_bandwidthAllocationBps = 0;
    double m_targetFramerateFps = 0.0;
    float m_packetLossRate = 0.0f;
    int64_t m_rttMs = 0;
    airan::media::EncodeFallbackStateMachine m_encodeFallback;
    int64_t m_nextPts = 0;
    uint64_t m_encodedFrameCount = 0;
};

} // namespace airan::media::ffmpeg
#endif
