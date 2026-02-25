#include "ffmpeg_util.h"
#include "../codec/hardware_context_manager.h"
#include "../common/logger_manager.h"

QList<std::shared_ptr<CodecInfo>> FFmpegUtilData::getH264Encoders()
{
    return m_h264Encoders;
}
QList<std::shared_ptr<CodecInfo>> FFmpegUtilData::getH264Decoders()
{
    return m_h264Decoders;
}

void FFmpegUtilData::init()
{
    QMutexLocker locker(&mutex);
    if (m_inited)
    {
        return;
    }

    const AVCodec *codec = NULL;
    void *iterator = NULL;

    // 遍历所有编码器
    while ((codec = av_codec_iterate(&iterator)))
    {
        // 筛选H.264编码器
        if (codec->id != AV_CODEC_ID_H264)
            continue;

        std::shared_ptr<CodecInfo> info = std::make_shared<CodecInfo>();
        info->name = codec->name ? codec->name : "";
        info->longName = codec->long_name ? codec->long_name : "";
        info->type = CodecInfo::UNKNOWN;
        if (av_codec_is_encoder(codec))
        {
            info->type = CodecInfo::ENCODER;
        }
        else if (av_codec_is_decoder(codec))
        {
            info->type = CodecInfo::DECODER;
        }
        info->isHardware = false;
        info->codec = codec;
        const AVCodecHWConfig *config = NULL;
        int hw_config_index = 0;
        while ((config = avcodec_get_hw_config(codec, hw_config_index++)))
        {
            info->isHardware = true;
            if (config->pix_fmt == AV_PIX_FMT_NONE)
            {
                // 如果像素格式是 AV_PIX_FMT_NONE，表示该配置适用于所有像素格式，跳过以避免冗余
                continue;
            }
            AVBufferRef *hwDeviceCtx = HardwareContextManager::instance().getDeviceContext(config->device_type);
            if (!hwDeviceCtx)
            {
                LOG_ERROR("Failed to create/get hardware device context for {}: {}", info->name, av_hwdevice_get_type_name(config->device_type));
                continue;
            }
            auto hwAccelInfo = std::make_shared<HwAccelInfo>();
            hwAccelInfo->config = config;
            hwAccelInfo->hwDeviceType = config->device_type;
            hwAccelInfo->hwDeviceTypeName = av_hwdevice_get_type_name(config->device_type);

            hwAccelInfo->supportedPixFormats.append(config->pix_fmt);
            hwAccelInfo->supportedPixFormatNames.append(av_get_pix_fmt_name(config->pix_fmt));
            info->supportedHwTypes.append(hwAccelInfo);
            switch (hwAccelInfo->hwDeviceType)
            {
            case AV_HWDEVICE_TYPE_D3D11VA:
#if defined(Q_OS_WIN32) && !defined(_WIN64)
                hwAccelInfo->score = 80;
#else
                hwAccelInfo->score = 1100;
#endif
                break;
            case AV_HWDEVICE_TYPE_CUDA:
                hwAccelInfo->score = 1000;
                break;
            case AV_HWDEVICE_TYPE_QSV:
#if defined(Q_OS_WIN32) && !defined(_WIN64)
                hwAccelInfo->score = 1200;
#else
                hwAccelInfo->score = 990;
#endif
                break;
            case 11: // AV_HWDEVICE_TYPE_VULKAN
#if defined(Q_OS_WIN32) && !defined(_WIN64)
                hwAccelInfo->score = 60;
#else
                hwAccelInfo->score = 900;
#endif
                break;
            case 12: // AV_HWDEVICE_TYPE_D3D12VA
#if defined(Q_OS_WIN32) && !defined(_WIN64)
                hwAccelInfo->score = 50;
#else
                hwAccelInfo->score = 350;
#endif
                break;
            case AV_HWDEVICE_TYPE_VAAPI:
                hwAccelInfo->score = 700;
                break;
            case AV_HWDEVICE_TYPE_DXVA2:
#if defined(Q_OS_WIN32) && !defined(_WIN64)
                hwAccelInfo->score = 1300;
#else
                hwAccelInfo->score = 600;
#endif
                break;
            case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
                hwAccelInfo->score = 500;
                break;
            case AV_HWDEVICE_TYPE_DRM:
                hwAccelInfo->score = 300;
                break;
            case 9: // AV_HWDEVICE_TYPE_OPENCL
                hwAccelInfo->score = 200;
                break;
            case 10: // AV_HWDEVICE_TYPE_MEDIACODEC
                hwAccelInfo->score = 100;
                break;
            case AV_HWDEVICE_TYPE_VDPAU:
                hwAccelInfo->score = 50;
                break;
            default:
                break;
            }
        }
        if (info->isHardware && info->supportedHwTypes.isEmpty())
        {
            // 如果标记为支持硬件加速但没有有效的硬件配置，跳过该编码器
            continue;
        }
        if (info->isHardware)
        {
            info->score += 10000;             // 支持硬件加速配置，分数加1000
            info->score += info->name.size(); // 名字越长，分数越高，表示功能可能越全面
        }
        else
        {
            info->score = 100 - info->name.size(); // 软解码或者编码  名字越短，分数越高
        }
        if (info->name.contains("nvenc") || info->name.contains("cuvid") || info->name.contains("nvdec"))
        {
            info->score += 9000; // NVENC 硬编码器，分数加9000
        }
        else if (info->name.contains("amf"))
        {
            info->score += 8500; // AMF 硬编码器，分数加8500
        }
        else if (info->name.contains("qsv"))
        {
            info->score += 8000; // QSV 硬编码器，分数加8000
        }
        else if (info->name.contains("v4l2m2m"))
        {
            info->score += 7500; // V4L2M2M 硬编码器，分数加7500
        }
        else if (info->name.contains("vaapi"))
        {
            info->score += 7000; // VAAPI 硬编码器，分数加7000
        }
        else if (info->name.contains("vulkan"))
        {
            info->score += 6500; // Vulkan 硬编码器，分数加6500
        }
        else if (info->name.contains("videotoolbox"))
        {
            info->score += 5000; // VideoToolbox 硬编码器，分数加5000
        }
        else if (info->name.contains("x264"))
        {
            info->score += 500; // x264 软编码器，分数加500
        }
        // 对支持的硬件类型按分数排序
        std::sort(info->supportedHwTypes.begin(), info->supportedHwTypes.end(), [](const std::shared_ptr<HwAccelInfo> &a, const std::shared_ptr<HwAccelInfo> &b)
                  { return a->score > b->score; });
        if (info->type == CodecInfo::DECODER)
        {
            m_h264Decoders.push_back(info);
        }
        else if (info->type == CodecInfo::ENCODER)
        {
            if (info->name.contains("_mf"))
            {
                std::shared_ptr<CodecInfo> info2 = std::make_shared<CodecInfo>();
                *info2 = *info;
                info2->score += 1000;     // Media Foundation 编码器，分数加1000
                info2->isHardware = true; // 强制标记为硬件编码器，优先使用
                m_h264Encoders.push_back(info2);
            }
            m_h264Encoders.push_back(info);
        }
    }
    // 硬编码优先
    std::sort(m_h264Encoders.begin(), m_h264Encoders.end(), [](const std::shared_ptr<CodecInfo> &a, const std::shared_ptr<CodecInfo> &b)
              { return a->score > b->score; });

    LOG_DEBUG("FFMPEG h264 encoders :{}", m_h264Encoders.size());
    for (const std::shared_ptr<CodecInfo> &info : m_h264Encoders)
    {
        LOG_DEBUG("{} ", info->toString());
    }
    // 硬解码优先
    std::sort(m_h264Decoders.begin(), m_h264Decoders.end(), [](const std::shared_ptr<CodecInfo> &a, const std::shared_ptr<CodecInfo> &b)
              { return a->score > b->score; });
    LOG_DEBUG("FFMPEG h264 decoders :{}", m_h264Decoders.size());
    for (const std::shared_ptr<CodecInfo> &info : m_h264Decoders)
    {
        LOG_DEBUG("{} ", info->toString());
    }

    m_inited = true;
}

FFmpegUtilData::~FFmpegUtilData()
{
    cleanup();
}

void FFmpegUtilData::cleanup()
{
    QMutexLocker locker(&mutex);
    m_h264Encoders.clear();
    m_h264Decoders.clear();
}

FFmpegUtilData *FFmpegUtilData::instance()
{
    static FFmpegUtilData ffmpegUtilData;
    ffmpegUtilData.init();
    return &ffmpegUtilData;
}

FFmpegUtilData::FFmpegUtilData()
{
    avdevice_register_all();
}
