#pragma once

#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_types.h"

#include <api/video_codecs/video_decoder.h>

#if defined(AIRAN_HAVE_FFMPEG)
namespace airan::media::ffmpeg
{

class FfmpegVideoDecoder final : public webrtc::VideoDecoder
{
public:
    explicit FfmpegVideoDecoder(CodecKind codec);
    ~FfmpegVideoDecoder() override;

    bool Configure(const Settings &settings) override;
#if AIRAN_WEBRTC_MILESTONE >= 144
    int32_t Decode(const webrtc::EncodedImage &inputImage, int64_t renderTimeMs) override;
#endif
    int32_t Decode(const webrtc::EncodedImage &inputImage,
                   bool missingFrames,
                   int64_t renderTimeMs) override;
    int32_t RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback *callback) override;
    int32_t Release() override;
    DecoderInfo GetDecoderInfo() const override;

private:
    bool configureProbe(size_t probeIndex);
    bool advanceProbe(const char *reason);
    static AVPixelFormat getFormat(AVCodecContext *ctx, const AVPixelFormat *formats);
    bool emitD3D11Frame(AVFrame *frame, const webrtc::EncodedImage &inputImage, int64_t renderTimeMs);
    bool emitFrame(AVFrame *frame, const webrtc::EncodedImage &inputImage, int64_t renderTimeMs);

    CodecKind m_codec;
    std::vector<const CodecProbe *> m_probes;
    const CodecProbe *m_probe = nullptr;
    size_t m_probeIndex = 0;
    webrtc::DecodedImageCallback *m_callback = nullptr;
    Settings m_settings;
    AVCodecContext *m_ctx = nullptr;
    AVPacket *m_packet = nullptr;
    AVBufferRef *m_hwDevice = nullptr;
    SwsContext *m_sws = nullptr;
    bool m_fatalDecoderError = false;
    bool m_keyFrameRequired = true;
    int m_encodedWidth = 0;
    int m_encodedHeight = 0;
    uint64_t m_missingFrameCount = 0;
};

} // namespace airan::media::ffmpeg
#endif
