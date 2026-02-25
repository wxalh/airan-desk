#ifndef FFMPEG_UTIL_H
#define FFMPEG_UTIL_H

#include <QString>
#include <QList>
#include <QMutex>
#include "../codec/codec_info.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

class FFmpegUtilData
{
private:
    QMutex mutex;
    bool m_inited = false;
    QList<std::shared_ptr<CodecInfo>> m_h264Encoders;
    QList<std::shared_ptr<CodecInfo>> m_h264Decoders;

    void init();
    FFmpegUtilData();
    ~FFmpegUtilData();

public:
    static FFmpegUtilData *instance();
    QList<std::shared_ptr<CodecInfo>> getH264Encoders();
    QList<std::shared_ptr<CodecInfo>> getH264Decoders();
    // 在程序退出前显式清理内部缓存，避免 VLD 在静态析构顺序问题下误报泄漏
    void cleanup();
};

#define FFmpegUtil FFmpegUtilData::instance()
#endif // FFMPEG_UTIL_H