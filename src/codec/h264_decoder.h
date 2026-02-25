#ifndef H264_DECODER_H
#define H264_DECODER_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QSet>
#include <memory>
#include "../common/constant.h"
#include "../util/ffmpeg_util.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/hwcontext.h>
}

#ifndef AV_ERROR_MAX_STRING_SIZE
#define AV_ERROR_MAX_STRING_SIZE 64
#endif

class H264Decoder : public QObject
{
    Q_OBJECT

public:
    explicit H264Decoder(QObject *parent = nullptr);
    ~H264Decoder();

    // 初始化解码器
    bool initialize();

    // 解码H264数据为QImage
    QImage decodeFrame(const rtc::binary &h264Data);

    // 释放资源
    void cleanup();

private:
    bool initializeCodec(std::shared_ptr<CodecInfo> codecInfo);
    bool initializeHardwareAccel(std::shared_ptr<CodecInfo> codecInfo);

    QImage avframeToQImage(AVFrame *frame);
    bool convertToTargetFmt(AVFrame *inputFrame, AVFrame *outputFrame); // 转换任意格式到NV12

    // 硬件解码回调函数
    static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts);

    std::shared_ptr<CodecInfo> m_codecInfo;
    std::shared_ptr<HwAccelInfo> m_hwAccelInfo;
    // FFmpeg 组件
    AVCodecContext *m_codecContext;
    AVBufferRef *m_hwDeviceCtx;
    
    // 线程安全
    QMutex m_mutex;

    // 运行时禁用的硬解后端（发生 transfer_data 失败后拉黑，自动切下一个）
    QSet<int> m_disabledHwTypes;

    bool m_initialized;
};

#endif // H264_DECODER_H