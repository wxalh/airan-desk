#include "h264_encoder.h"
#include "hardware_context_manager.h"
#include <QtGlobal>
#include <QGuiApplication>
#include <QScreen>
#include <cstdio>
#include <QTimer>
#include <QThread>
#include <QPointer>
#include <QFile>
#include <cstdlib>
#include <algorithm>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif
#if !defined(AIRAN_COMPATIBLE_WIN7) && (defined(Q_OS_WIN64) || defined(Q_OS_WIN32))
#include <d3d11.h>
#endif

H264Encoder::H264Encoder(QObject *parent)
    : QObject(parent), m_codecContext(nullptr),
      m_codecInfo(nullptr), m_frame(nullptr),
      m_hwFrame(nullptr), m_packet(nullptr),
      m_swsContext(nullptr), m_hwDeviceCtx(nullptr),
      m_dstW(0), m_dstH(0), m_fps(30), m_pts(0), m_bitrate(6000 * 1000),
      m_initialized(false), m_deviceCtx(nullptr),
      m_d3d11vaDeviceCtx(nullptr), m_d3d11vaFramesCtx(nullptr), m_filterGraph(nullptr),
      m_bufferSrcCtx(nullptr), m_bufferSinkCtx(nullptr), m_filterFramesCtx(nullptr),
      m_filterSrcW(0), m_filterSrcH(0), m_filterNeedScale(false), m_zeroCopyHealthy(true),
      m_qsvDeriveChecked(false), m_qsvDeriveOk(false),
      m_forcedQsvDeviceCtx(nullptr), m_forcedQsvFramesCtx(nullptr),
      m_forcedD3d11DeviceCtx(nullptr), m_forcedD3d11FramesCtx(nullptr),
      m_qsvSessionBound(false), m_qsvFramesBound(false), m_d3d11SessionBound(false)
{
    m_targetSwFmt = AV_PIX_FMT_NV12; // 默认
}

H264Encoder::~H264Encoder()
{
    QMutexLocker locker(&m_mutex);
    cleanup();
}

bool H264Encoder::initialize(int screen_index, int dstW, int dstH, int fps)
{
    QMutexLocker locker(&m_mutex);
    m_pts = 0;
    m_zeroCopyHealthy = true;
    m_qsvDeriveChecked = false;
    m_qsvDeriveOk = false;
    m_qsvSessionBound = false;
    m_qsvFramesBound = false;
    m_d3d11SessionBound = false;
    if (m_initialized && m_dstW == dstW && m_dstH == dstH && m_fps == fps)
    {
        LOG_INFO("Encoder already initialized with the same parameters");
        return true;
    }
    else if (m_initialized)
    {
        LOG_INFO("Encoder already initialized, resetting with new parameters");
        cleanup();
    }

    m_fps = fps;
    if (screen_index < 0)
    {
        screen_index = 0; // 默认使用主屏幕
    }
    QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
    {
        LOG_ERROR("No screens found for encoding");
        return false;
    }
    m_screenIndex = screen_index % screens.size(); // 防止越界
    QScreen *screen = screens.value(m_screenIndex);
    if (screen)
    {
        m_screen_width = screen->size().width();
        m_screen_height = screen->size().height();
    }
    // 验证分辨率参数 - 确保分辨率是偶数（H264要求）
    if (dstW % 16 != 0 || dstH % 16 != 0)
    {
        m_dstW = dstW & ~15;
        m_dstH = dstH & ~15;
        LOG_WARN("Adjusting resolution from {}x{} to {}x{} to make it even for H264 compatibility", dstW, dstH, m_dstW, m_dstH);
    }
    else
    {
        m_dstW = dstW;
        m_dstH = dstH;
    }
    // 以分辨率/场景经验值设置目标码率（单位：kbps）——不要用 width*height*fps 直接作为 kbps
    int targetKbps = 0;
    if (m_dstW <= 1280 && m_dstH <= 720)
        targetKbps = 3500; // 720p 推荐值范围 2500-6000
    else if (m_dstW <= 1920 && m_dstH <= 1080)
        targetKbps = 6000; // 1080p 推荐值范围 4000-8000
    else
        targetKbps = 10000; // 更高分辨率时提高码率

    m_bitrate = targetKbps * 1000; // 转换为 bps
    // 优先尝试硬件加速
    for (std::shared_ptr<CodecInfo> codecInfo : FFmpegUtil->getH264Encoders())
    {
        cleanup();
        m_codecInfo = codecInfo;
        LOG_INFO("Trying acceleration: {}", codecInfo->toString());
        m_initialized = initializeCodec(codecInfo);

        if (m_initialized)
        {
            break;
        }
        LOG_WARN("✗ Failed to initialize {} acceleration, trying next", codecInfo->name);
    }

    if (m_initialized)
    {
        LOG_INFO("✓ Successfully initialized H264 encoder with {} acceleration", m_codecInfo->name);
    }
    else
    {
        LOG_ERROR("❌ Failed to initialize H264 encoder with any method");
        cleanup();
    }
    return m_initialized;
}

bool H264Encoder::initializeCodec(std::shared_ptr<CodecInfo> codecInfo)
{
    const bool isMediaFoundation = codecInfo->name.contains("_mf", Qt::CaseInsensitive) ||
                                   codecInfo->longName.contains("MediaFoundation", Qt::CaseInsensitive);
    const bool isOpenH264 = codecInfo->name.contains("openh264", Qt::CaseInsensitive) ||
                            codecInfo->longName.contains("OpenH264", Qt::CaseInsensitive);

    // 创建编码器上下文
    m_codecContext = avcodec_alloc_context3(codecInfo->codec);
    if (!m_codecContext)
    {
        LOG_ERROR("Could not allocate video codec context");
        return false;
    }

    // 设置编码参数
    m_codecContext->width = m_dstW;
    m_codecContext->height = m_dstH;
    m_codecContext->bit_rate = m_bitrate;
    m_codecContext->rc_max_rate = m_bitrate;
    m_codecContext->rc_min_rate = std::max<int64_t>(100000, m_bitrate * 8 / 10);
    m_codecContext->rc_buffer_size = std::max<int>(m_bitrate / 2, 500000);
    m_codecContext->time_base = AVRational{1, m_fps};
    m_codecContext->framerate = AVRational{m_fps, 1};
    m_codecContext->gop_size = m_fps * 2; // 每2秒一个关键帧（更频繁，避免花屏）
    m_codecContext->max_b_frames = 0;     // 不使用B帧，只使用I帧和P帧
    m_codecContext->keyint_min = m_fps;   // 最小关键帧间隔1秒
    // 选择编码器像素格式：优先 NV12，其次 YUV420P/YUVJ420P，再退回 NV12
    m_codecContext->pix_fmt = AV_PIX_FMT_NV12; // 默认
    if (codecInfo->codec && codecInfo->codec->pix_fmts)
    {
        AVPixelFormat preferred[] = {AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_BGRA, AV_PIX_FMT_BGR0};
        AVPixelFormat chosen = AV_PIX_FMT_NONE;
        const enum AVPixelFormat *p = codecInfo->codec->pix_fmts;
        for (; *p != AV_PIX_FMT_NONE; ++p)
        {
            for (AVPixelFormat pref : preferred)
            {
                if (*p == pref)
                {
                    chosen = *p;
                    break;
                }
            }
            if (chosen != AV_PIX_FMT_NONE)
                break;
        }
        if (chosen != AV_PIX_FMT_NONE)
            m_codecContext->pix_fmt = chosen;
        LOG_DEBUG("Codec {} supports pix_fmts, chosen pix_fmt={} (preferred)", codecInfo->name, av_get_pix_fmt_name(m_codecContext->pix_fmt));
    }

    // 网络自适应优化：针对高延迟网络的编码参数
    // 禁用全局头部（强制输出 Annex-B）
    m_codecContext->flags &= ~AV_CODEC_FLAG_GLOBAL_HEADER;
    m_codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecContext->flags2 |= AV_CODEC_FLAG2_FAST;
    // 多 slice 在弱网下更易出现局部马赛克，默认使用单 slice 以提高稳定性
    m_codecContext->slices = 1;
    av_opt_set(m_codecContext->priv_data, "profile", "baseline", AV_OPT_SEARCH_CHILDREN); // 使用baseline profile提高兼容性
    av_opt_set(m_codecContext->priv_data, "annexb", "1", AV_OPT_SEARCH_CHILDREN);
    av_opt_set(m_codecContext->priv_data, "tune", "zerolatency", AV_OPT_SEARCH_CHILDREN);
    // --- 硬件类 (NVENC, QSV, AMF) ---
    // 绝大多数硬件编码器使用 repeat_headers
    av_opt_set_int(m_codecContext->priv_data, "repeat_headers", 1, AV_OPT_SEARCH_CHILDREN);
    // 针对 MediaFoundation (h264_mf) 的调优：常见于 Windows 屏幕远程场景
    if (isMediaFoundation)
    {
        LOG_INFO("Applying MediaFoundation-specific encoder options for {}", codecInfo->toString());
        // 使用 CBR 或低延迟 VBR，优先保证画面稳定
        av_opt_set_int(m_codecContext->priv_data, "rate_control", 0, AV_OPT_SEARCH_CHILDREN); // 0 = cbr
        // 场景为 display remoting，提高远程桌面质量/延迟优化
        av_opt_set_int(m_codecContext->priv_data, "scenario", 1, AV_OPT_SEARCH_CHILDREN); // 1 = display_remoting
        // 提高质量参数（0-100），-1 为默认
        av_opt_set_int(m_codecContext->priv_data, "quality", 90, AV_OPT_SEARCH_CHILDREN);
        // 高频变化画面下使用更短 GOP，减少错误传播时长
        m_codecContext->gop_size = std::max(1, m_fps / 2);
        m_codecContext->keyint_min = std::max(1, m_fps / 4);
        if (codecInfo->isHardware)
        {
            LOG_INFO("MediaFoundation encoder supports hardware acceleration, applying hwaccel-specific options");
            // 强制使用硬件编码（如可用）
            av_opt_set_int(m_codecContext->priv_data, "hw_encoding", 1, AV_OPT_SEARCH_CHILDREN);
            // 确保使用 NV12，避免额外颜色空间转换导致模糊
            m_codecContext->pix_fmt = AV_PIX_FMT_NV12;
        }
    }
    // 设置编码预设和调优
    if (codecInfo->isHardware)
    {
        if (!initializeHardwareAccel(codecInfo))
        {
            LOG_ERROR("Failed to initialize {}", codecInfo->toString());
            return false;
        }
    }
    else
    {
        // 软件编码优化
        m_hwDeviceCtx = nullptr;
        // libopenh264 在复杂场景下更容易出现块效应，使用更保守参数
        if (isOpenH264)
        {
            m_codecContext->thread_count = 1;
            m_codecContext->slices = 1;
            m_codecContext->gop_size = std::max(1, m_fps / 2);
            m_codecContext->keyint_min = std::max(1, m_fps / 4);
            av_opt_set_int(m_codecContext->priv_data, "allow_skip_frames", 0, AV_OPT_SEARCH_CHILDREN);
            LOG_INFO("Initialized software encoding (libopenh264) with conservative settings {}x{}, {}fps, {}bps", m_dstW, m_dstH, m_fps, m_bitrate);
        }

        // 仅对 libx264 设置 x264 特定参数；避免将 x264 参数传给 libopenh264 等不兼容编码器
        if (codecInfo->name.contains("x264", Qt::CaseInsensitive) || codecInfo->longName.contains("x264", Qt::CaseInsensitive))
        {
            // 基础编码选项
            av_opt_set(m_codecContext->priv_data, "nal-hrd", "cbr", AV_OPT_SEARCH_CHILDREN);
            QString x264Params = QString("keyint=%1:crf=18:min-keyint=%2:no-scenecut=1:repeat-headers=1:bframes=0:b-adapt=0")
                                     .arg(m_fps)
                                     .arg(m_fps / 2);
            av_opt_set(m_codecContext->priv_data, "x264-params", x264Params.toUtf8().constData(), AV_OPT_SEARCH_CHILDREN);
            // 使用短 GOP（允许 P 帧，避免 gop_size=1）
            m_codecContext->gop_size = m_fps; // 1秒一个 I 帧
            m_codecContext->keyint_min = std::max(1, m_fps / 2);
            LOG_INFO("Initialized software encoding (libx264) with {}x{}, {}fps, {}bps", m_dstW, m_dstH, m_fps, m_bitrate);
        }
        else if (!isOpenH264)
        {
            // 其他软件编码器使用更通用的设置
            m_codecContext->gop_size = m_fps; // 1秒一个 I 帧
            m_codecContext->keyint_min = std::max(1, m_fps / 2);
            LOG_INFO("Initialized software encoding (codec={}) with {}x{}, {}fps, {}bps", codecInfo->name, m_dstW, m_dstH, m_fps, m_bitrate);
        }
    }

    // 仅当编码器支持多线程且不是 openh264 时使用多线程，否则强制单线程以避免兼容性/画面问题
    if (!isOpenH264 && codecInfo->codec && (codecInfo->codec->capabilities & (AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_SLICE_THREADS))) {
        m_codecContext->thread_count = QThread::idealThreadCount();
        m_codecContext->thread_type = FF_THREAD_FRAME;
    } else {
        m_codecContext->thread_count = 1;
        LOG_DEBUG("Codec {} forced to single-threaded encode", codecInfo->name);
    }

    // 打开编码器
    int ret = avcodec_open2(m_codecContext, codecInfo->codec, nullptr);
    if (ret < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR("Could not open codec {} ({}x{}, {}fps, {}bps): {} (error code: {})",
                  codecInfo->name, m_dstW, m_dstH, m_fps, m_bitrate, errbuf, ret);
        return false;
    }

    // 对于软件编码器，确保后续的像素格式转换目标匹配编码器期望的像素格式
    if (!codecInfo->isHardware)
    {
        m_targetSwFmt = m_codecContext->pix_fmt;
        LOG_DEBUG("Software codec selected target sw format: {}", av_get_pix_fmt_name(m_targetSwFmt));
    }

    // 分配帧
    m_frame = av_frame_alloc();
    if (!m_frame)
    {
        LOG_ERROR("Could not allocate video frame");
        return false;
    }

    // 所有编码器都统一分配buffer，包括QSV
    m_frame->format = m_codecContext->pix_fmt;
    m_frame->width = m_codecContext->width;
    m_frame->height = m_codecContext->height;

    if (!codecInfo->isHardware)
    {
        // 只有软件编码模式才需要分配系统内存 Buffer
        ret = av_frame_get_buffer(m_frame, 32);
        if (ret < 0)
        {
            LOG_ERROR("Could not allocate video frame data (Software Mode)");
            return false;
        }
    }
    else
    {
        // 硬件模式下，m_frame 在这里只需要作为一个占位符
        LOG_INFO("Hardware mode: skipping manual frame buffer allocation.");
    }

    // 分配数据包
    m_packet = av_packet_alloc();
    if (!m_packet)
    {
        LOG_ERROR("Could not allocate packet");
        return false;
    }
    return true;
}

bool H264Encoder::initializeHardwareAccel(std::shared_ptr<CodecInfo> codecInfo)
{
    if (!m_codecContext)
    {
        LOG_ERROR("initializeHardwareAccel called with null codec context");
        return false;
    }
    int ret = 0;
    for (const auto &hwaccelInfo : codecInfo->supportedHwTypes)
    {
        if (hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_NONE)
        {
            LOG_WARN("Skipping unsupported hardware device type NONE");
            continue;
        }
        // 设置硬编码器特有参数
        switch (hwaccelInfo->hwDeviceType)
        {
        case AV_HWDEVICE_TYPE_QSV:
            // QSV 常用稳定参数
            av_opt_set(m_codecContext->priv_data, "async_depth", "1", 0);
            av_opt_set(m_codecContext->priv_data, "look_ahead", "0", 0);
            av_opt_set(m_codecContext->priv_data, "b", "0", 0);
            av_opt_set(m_codecContext->priv_data, "bf", "0", 0);
            av_opt_set(m_codecContext->priv_data, "repeat-headers", "1", 0);
            break;
        case AV_HWDEVICE_TYPE_VAAPI:
            av_opt_set(m_codecContext->priv_data, "rc_mode", "CBR", AV_OPT_SEARCH_CHILDREN);
            av_opt_set(m_codecContext->priv_data, "low_power", "1", AV_OPT_SEARCH_CHILDREN);
            av_opt_set(m_codecContext->priv_data, "idr_interval", "1", AV_OPT_SEARCH_CHILDREN);
            break;
        case AV_HWDEVICE_TYPE_CUDA:
            // 降低延迟：从 2 帧延迟降低到 0
            av_opt_set(m_codecContext->priv_data, "delay", "0", AV_OPT_SEARCH_CHILDREN);
            av_opt_set(m_codecContext->priv_data, "forced-idr", "1", AV_OPT_SEARCH_CHILDREN);
            av_opt_set(m_codecContext->priv_data, "tune", "ll", AV_OPT_SEARCH_CHILDREN);
            av_opt_set(m_codecContext->priv_data, "rc", "cbr", AV_OPT_SEARCH_CHILDREN);
            av_opt_set(m_codecContext->priv_data, "repeat-headers", "1", AV_OPT_SEARCH_CHILDREN);
            // 若驱动支持，启用 0-latency（不支持会被忽略/返回错误，FFmpeg 不会因此崩）
            av_opt_set(m_codecContext->priv_data, "zerolatency", "1", AV_OPT_SEARCH_CHILDREN);
            break;
        case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
            break;
        default:
            break;
        }

        // 尝试初始化硬编码器

        if (hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_QSV && m_forcedQsvDeviceCtx)
        {
            m_hwDeviceCtx = av_buffer_ref(m_forcedQsvDeviceCtx);
            LOG_INFO("Using first-frame derived QSV device context for encoder session binding");
        }
        else if (hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_D3D11VA && m_forcedD3d11DeviceCtx)
        {
            m_hwDeviceCtx = av_buffer_ref(m_forcedD3d11DeviceCtx);
            LOG_INFO("Using first-frame bound D3D11VA device context for NVENC session binding");
        }
        else
        {
            m_hwDeviceCtx = HardwareContextManager::instance().getDeviceContext(hwaccelInfo->hwDeviceType);
        }
        if (!m_hwDeviceCtx)
        {
            LOG_ERROR("Failed to create/get hardware device context for {}", hwaccelInfo->hwDeviceTypeName);
            continue;
        }
        m_hwaccelInfo = hwaccelInfo;
        m_codecContext->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);

        AVBufferRef *hwFramesRef = nullptr;
        if (hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_QSV && m_forcedQsvFramesCtx)
        {
            m_codecContext->pix_fmt = AV_PIX_FMT_QSV;
            m_codecContext->hw_frames_ctx = av_buffer_ref(m_forcedQsvFramesCtx);
            if (m_codecContext->hw_frames_ctx)
            {
                LOG_INFO("Using forced QSV hw_frames_ctx from first graph output for encoder session");
                return true;
            }
            LOG_WARN("Failed to reference forced QSV hw_frames_ctx, fallback to normal hwframe init");
        }
        else if (hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_D3D11VA && m_forcedD3d11FramesCtx)
        {
            m_codecContext->pix_fmt = AV_PIX_FMT_D3D11;
            m_codecContext->hw_frames_ctx = av_buffer_ref(m_forcedD3d11FramesCtx);
            if (m_codecContext->hw_frames_ctx)
            {
                LOG_INFO("Using first-frame bound D3D11VA hw_frames_ctx for NVENC session");
                return true;
            }
            LOG_WARN("Failed to reference forced D3D11VA hw_frames_ctx, fallback to normal hwframe init");
        }

        hwFramesRef = av_hwframe_ctx_alloc(m_hwDeviceCtx);
        if (!hwFramesRef)
        {
            LOG_ERROR("Failed to allocate hwframe context for {}", hwaccelInfo->hwDeviceTypeName);
            continue;
        }
        // 先查询设备支持的 system-memory (sw) 格式，再决定 framesCtx->sw_format ---
        AVHWFramesConstraints *constraints = av_hwdevice_get_hwframe_constraints(m_hwDeviceCtx, nullptr);
        AVPixelFormat chosenSw = AV_PIX_FMT_NONE;
        if (constraints && constraints->valid_sw_formats)
        {
            // 收集支持的格式，优先选项：优先匹配源像素格式（如果已知），否则优先 BGRA，再 BGR0，再 NV12
            bool has_bgra = false;
            bool has_bgr0 = false;
            bool has_nv12 = false;
            for (int k = 0; constraints->valid_sw_formats[k] != AV_PIX_FMT_NONE; ++k)
            {
                AVPixelFormat f = constraints->valid_sw_formats[k];
                if (f == AV_PIX_FMT_BGRA)
                    has_bgra = true;
                else if (f == AV_PIX_FMT_BGR0)
                    has_bgr0 = true;
                else if (f == AV_PIX_FMT_NV12)
                    has_nv12 = true;
            }
            // 如果我们已经知道采集源格式，优先匹配

            if (hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_D3D11VA ||
                hwaccelInfo->hwDeviceType == 12 /*AV_HWDEVICE_TYPE_D3D12*/ ||
                hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_DXVA2)
            {
                if (has_bgra)
                {
                    chosenSw = AV_PIX_FMT_BGRA;
                }
                else if (has_bgr0)
                {
                    chosenSw = AV_PIX_FMT_BGR0;
                }
                else if (has_nv12)
                {
                    chosenSw = AV_PIX_FMT_NV12;
                }
            }
            else if (has_bgra)
                chosenSw = AV_PIX_FMT_BGRA; // 更常见且避免通道重排
            else if (has_bgr0)
                chosenSw = AV_PIX_FMT_BGR0;
            else if (has_nv12)
                chosenSw = AV_PIX_FMT_NV12;
        }
        if (chosenSw == AV_PIX_FMT_NONE)
            chosenSw = AV_PIX_FMT_NV12; // 兜底
        // 记录诊断日志
        LOG_DEBUG("Device {} valid sw_format chosen: {}", hwaccelInfo->hwDeviceTypeName, av_get_pix_fmt_name(chosenSw));
        // 保存为成员，后续 capturePath 使用
        m_targetSwFmt = chosenSw;

        AVHWFramesContext *framesCtx = reinterpret_cast<AVHWFramesContext *>(hwFramesRef->data);

        framesCtx->sw_format = chosenSw;
        framesCtx->width = m_codecContext->width;
        framesCtx->height = m_codecContext->height;
        framesCtx->initial_pool_size = 20;

        // 初始化硬编码器像素格式
        int i = 0;
        for (i = 0; i < hwaccelInfo->supportedPixFormats.size(); i++)
        {
            AVPixelFormat pixelFormatTmp = hwaccelInfo->supportedPixFormats[i];
            if (pixelFormatTmp == AV_PIX_FMT_NONE)
            {
                LOG_WARN("Skipping unsupported pixel format NONE for {}", hwaccelInfo->supportedPixFormatNames[i]);
                continue;
            }
            m_codecContext->pix_fmt = pixelFormatTmp;

            framesCtx->format = pixelFormatTmp;
            ret = av_hwframe_ctx_init(hwFramesRef);
            if (ret < 0)
            {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                LOG_ERROR("Failed to init hwframe context for {}: {}", hwaccelInfo->supportedPixFormatNames[i], errbuf);
                av_buffer_unref(&hwFramesRef);
                continue;
            }
            LOG_INFO("Initialized hwframe context for {} with pix_fmt={}", hwaccelInfo->hwDeviceTypeName, hwaccelInfo->supportedPixFormatNames[i]);
            break;
        }
        if (i == hwaccelInfo->supportedPixFormats.size())
        {
            continue;
        }
        m_codecContext->hw_frames_ctx = hwFramesRef;

        LOG_INFO("Successfully initialized hardware acceleration with {} on {}", codecInfo->name, hwaccelInfo->hwDeviceTypeName);
        return true;
    }
    LOG_ERROR("Failed to initialize hwframe context for all supported pixel formats on {}", codecInfo->name);
    return false;
}

// 新增：通用系统内存像素格式转换
bool H264Encoder::convertToSwFormat(AVFrame *inputFrame, AVFrame *outputFrame, AVPixelFormat dstFormat)
{
    if (!inputFrame || !outputFrame)
        return false;
    AVPixelFormat inputFormat = static_cast<AVPixelFormat>(inputFrame->format);
    if (inputFrame->width <= 0 || inputFrame->height <= 0)
        return false;

    // 编码器内部不再进行缩放：输出尺寸始终等于输入尺寸
    int dstW = inputFrame->width;
    int dstH = inputFrame->height;

    outputFrame->format = dstFormat;
    outputFrame->width = dstW;
    outputFrame->height = dstH;

    int ret = av_frame_get_buffer(outputFrame, 32);
    if (ret < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR("convertToSwFormat: Failed to allocate buffer for {}: {}", av_get_pix_fmt_name(dstFormat), errbuf);
        return false;
    }

    // 拷贝色彩元数据
    outputFrame->colorspace = inputFrame->colorspace;
    outputFrame->color_primaries = inputFrame->color_primaries;
    outputFrame->color_trc = inputFrame->color_trc;
    outputFrame->color_range = inputFrame->color_range;

    SwsContext *swsContext = sws_getContext(
        inputFrame->width, inputFrame->height, inputFormat,
        dstW, dstH, dstFormat,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsContext)
    {
        LOG_ERROR("convertToSwFormat: Failed to create sws context from {} {}x{} to {} {}x{}",
                  av_get_pix_fmt_name(inputFormat), inputFrame->width, inputFrame->height,
                  av_get_pix_fmt_name(dstFormat), dstW, dstH);
        return false;
    }

    int scaled = sws_scale(swsContext,
                           inputFrame->data, inputFrame->linesize, 0, inputFrame->height,
                           outputFrame->data, outputFrame->linesize);
    sws_freeContext(swsContext);
    if (scaled != dstH)
    {
        LOG_ERROR("convertToSwFormat: sws_scale unexpected lines: expected {} got {} (input {}x{})",
                  dstH, scaled, inputFrame->width, inputFrame->height);
        return false;
    }

    LOG_TRACE("convertToSwFormat: converted {} {}x{} -> {} {}x{} linesize0={} color_range={}",
              av_get_pix_fmt_name(inputFormat), inputFrame->width, inputFrame->height,
              av_get_pix_fmt_name(dstFormat), dstW, dstH, outputFrame->linesize[0], (int)(outputFrame->color_range));
    return true;
}

void H264Encoder::cleanup()
{
    // 1. 先释放帧、包、SwsContext
    if (m_packet)
    {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }
    if (m_frame)
    {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }
    if (m_hwFrame)
    {
        av_frame_free(&m_hwFrame);
        m_hwFrame = nullptr;
    }
    if (m_swsContext)
    {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    // 2. 释放滤镜图
    if (m_filterGraph)
    {
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr;
    }
    m_bufferSrcCtx = nullptr;
    m_bufferSinkCtx = nullptr;
    if (m_filterFramesCtx)
    {
        av_buffer_unref(&m_filterFramesCtx);
        m_filterFramesCtx = nullptr;
    }
    m_filterSrcW = 0;
    m_filterSrcH = 0;
    m_filterNeedScale = false;
    m_zeroCopyHealthy = true;
    m_qsvDeriveChecked = false;
    m_qsvDeriveOk = false;
    m_qsvSessionBound = false;
    m_qsvFramesBound = false;
    m_d3d11SessionBound = false;

    // 3. 释放编码器上下文
    if (m_codecContext)
    {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }
    // 4. 释放硬件设备上下文
    if (m_hwDeviceCtx)
    {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }
    if (m_d3d11vaDeviceCtx)
    {
        av_buffer_unref(&m_d3d11vaDeviceCtx);
        m_d3d11vaDeviceCtx = nullptr;
    }
    if (m_d3d11vaFramesCtx)
    {
        av_buffer_unref(&m_d3d11vaFramesCtx);
        m_d3d11vaFramesCtx = nullptr;
    }
    if (m_forcedQsvDeviceCtx)
    {
        av_buffer_unref(&m_forcedQsvDeviceCtx);
        m_forcedQsvDeviceCtx = nullptr;
    }
    if (m_forcedQsvFramesCtx)
    {
        av_buffer_unref(&m_forcedQsvFramesCtx);
        m_forcedQsvFramesCtx = nullptr;
    }
    if (m_forcedD3d11DeviceCtx)
    {
        av_buffer_unref(&m_forcedD3d11DeviceCtx);
        m_forcedD3d11DeviceCtx = nullptr;
    }
    if (m_forcedD3d11FramesCtx)
    {
        av_buffer_unref(&m_forcedD3d11FramesCtx);
        m_forcedD3d11FramesCtx = nullptr;
    }
    // 5. 释放采集设备
    if (m_deviceCtx)
    {
        avformat_close_input(&m_deviceCtx);
    }
    if (m_codecInfo)
    {
        m_codecInfo = nullptr;
    }
    if (m_hwaccelInfo)
    {
        m_hwaccelInfo = nullptr;
    }
    m_pts = 0;
    m_codecInfo = nullptr;
    m_targetSwFmt = AV_PIX_FMT_NV12;
    m_initialized = false;

    LOG_DEBUG("H264Encoder cleanup completed");
}

// 将 QImage 拷贝到 AVFrame (BGRA) 并转换为编码器期望的目标 sw 格式/尺寸
AVFrame *H264Encoder::createFrameFromQImage(const QImage &img)
{
    if (img.isNull())
        return nullptr;
    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return nullptr;
    int w = img.width();
    int h = img.height();
    frame->format = AV_PIX_FMT_BGRA;
    frame->width = w;
    frame->height = h;
    int ret = av_frame_get_buffer(frame, 32);
    if (ret < 0)
    {
        av_frame_free(&frame);
        return nullptr;
    }
    // QImage stores BGRA on little-endian for Format_ARGB32 / Format_RGB32
    const uchar *src = img.constBits();
    int srcStride = img.bytesPerLine();
    int dstStride = frame->linesize[0];
    for (int y = 0; y < h; ++y)
    {
        memcpy(frame->data[0] + y * dstStride, src + y * srcStride, std::min(srcStride, dstStride));
    }
    return frame;
}

#if !defined(AIRAN_COMPATIBLE_WIN7) && (defined(Q_OS_WIN64) || defined(Q_OS_WIN32))
// Helper: map D3D11 texture to system memory via staging copy, return AVFrame (BGRA)
AVFrame *H264Encoder::createFrameFromD3D11Texture(ID3D11Texture2D *tex)
{
    if (!tex)
        return nullptr;
    ID3D11Device *dev = nullptr;
    tex->GetDevice(&dev);
    if (!dev)
        return nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    dev->GetImmediateContext(&ctx);
    if (!ctx)
    {
        dev->Release();
        return nullptr;
    }
    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);
    // create staging texture
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    ID3D11Texture2D *staging = nullptr;
    HRESULT hr = dev->CreateTexture2D(&stagingDesc, nullptr, &staging);
    if (FAILED(hr) || !staging)
    {
        ctx->Release();
        dev->Release();
        return nullptr;
    }
    ctx->CopyResource(staging, tex);
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
    {
        staging->Release();
        ctx->Release();
        dev->Release();
        return nullptr;
    }
    // create AVFrame BGRA and copy
    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        ctx->Unmap(staging, 0);
        staging->Release();
        ctx->Release();
        dev->Release();
        return nullptr;
    }
    frame->format = AV_PIX_FMT_BGRA;
    frame->width = desc.Width;
    frame->height = desc.Height;
    if (av_frame_get_buffer(frame, 32) < 0)
    {
        av_frame_free(&frame);
        ctx->Unmap(staging, 0);
        staging->Release();
        ctx->Release();
        dev->Release();
        return nullptr;
    }
    int srcStride = mapped.RowPitch;
    int dstStride = frame->linesize[0];
    for (int y = 0; y < desc.Height; ++y)
    {
        memcpy(frame->data[0] + y * dstStride, (uint8_t *)mapped.pData + y * srcStride, std::min(srcStride, dstStride));
    }
    ctx->Unmap(staging, 0);
    staging->Release();
    ctx->Release();
    dev->Release();
    return frame;
}

std::pair<std::shared_ptr<rtc::binary>, quint64> H264Encoder::encodeGPU(ID3D11Texture2D *in)
{
    QMutexLocker locker(&m_mutex);
    std::shared_ptr<rtc::binary> out = std::make_shared<rtc::binary>();
    quint64 timestampUs = 0;
    if (!m_codecContext || !in)
        return {out, timestampUs};

    // try best-effort: map texture to CPU (staging) then convert/upload
    AVFrame *frame = createFrameFromD3D11Texture(in);
    if (!frame)
        return {out, timestampUs};

    if (frame->width != m_dstW || frame->height != m_dstH)
    {
        LOG_ERROR("encodeGPU size mismatch: input={}x{}, encoder={}x{}. Internal scaling is disabled; scale in capture path.",
                  frame->width, frame->height, m_dstW, m_dstH);
        av_frame_free(&frame);
        return {out, timestampUs};
    }

    // convert to target sw fmt/size
    AVFrame *targetFrame = av_frame_alloc();
    if (!convertToSwFormat(frame, targetFrame, m_targetSwFmt))
    {
        av_frame_free(&frame);
        av_frame_free(&targetFrame);
        return {out, timestampUs};
    }
    av_frame_free(&frame);

    int ret = 0;
    if (m_codecInfo && m_codecInfo->isHardware && m_hwDeviceCtx && m_codecContext->hw_frames_ctx)
    {
        AVFrame *hwFrame = av_frame_alloc();
        if (av_hwframe_get_buffer(m_codecContext->hw_frames_ctx, hwFrame, 0) >= 0)
        {
            if (av_hwframe_transfer_data(hwFrame, targetFrame, 0) >= 0)
            {
                hwFrame->pts = m_pts++;
                ret = avcodec_send_frame(m_codecContext, hwFrame);
                av_frame_free(&hwFrame);
            }
            else
            {
                av_frame_free(&hwFrame);
                targetFrame->pts = m_pts++;
                ret = avcodec_send_frame(m_codecContext, targetFrame);
            }
        }
        else
        {
            targetFrame->pts = m_pts++;
            ret = avcodec_send_frame(m_codecContext, targetFrame);
        }
    }
    else
    {
        targetFrame->pts = m_pts++;
        ret = avcodec_send_frame(m_codecContext, targetFrame);
    }

    av_frame_free(&targetFrame);

    if (ret == AVERROR(EAGAIN))
    {
        // 编码器输出未及时取走导致 send 被背压：先排空可用包，本帧跳过
        while (true)
        {
            int recvRet = avcodec_receive_packet(m_codecContext, m_packet);
            if (recvRet == AVERROR(EAGAIN) || recvRet == AVERROR_EOF)
                break;
            if (recvRet < 0)
                break;

            if (m_packet->size > 0)
            {
                size_t old = out->size();
                out->resize(old + m_packet->size);
                memcpy(out->data() + old, m_packet->data, m_packet->size);
                int64_t pts = m_packet->pts;
                timestampUs = av_rescale_q(pts, m_codecContext->time_base, AVRational{1, 1000000});
            }
            av_packet_unref(m_packet);
        }
        ret = AVERROR(EAGAIN);
    }

    if (ret < 0)
    {
        if (ret == AVERROR(EAGAIN))
        {
            LOG_DEBUG("Encoder still back-pressured after retry in GPU path, skip this frame");
            return {out, timestampUs};
        }
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR("Error sending frame to encoder (GPU path): {}", errbuf);
        return {out, timestampUs};
    }

    while (true)
    {
        ret = avcodec_receive_packet(m_codecContext, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0)
        {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG_ERROR("Error receiving packet from encoder: {}", errbuf);
            break;
        }
        if (m_packet->size > 0)
        {
            size_t old = out->size();
            out->resize(old + m_packet->size);
            memcpy(out->data() + old, m_packet->data, m_packet->size);
            int64_t pts = m_packet->pts;
            timestampUs = av_rescale_q(pts, m_codecContext->time_base, AVRational{1, 1000000});
        }
        av_packet_unref(m_packet);
    }
    return {out, timestampUs};
}
#endif

// 将已经准备好的系统内存帧（任意pixfmt/尺寸）处理并送入编码器，返回编码输出
std::pair<std::shared_ptr<rtc::binary>, quint64> H264Encoder::encodeCPU(const QImage &in)
{
    QMutexLocker locker(&m_mutex);
    std::shared_ptr<rtc::binary> out = std::make_shared<rtc::binary>();
    quint64 timestampUs = 0;
    if (!m_codecContext)
        return {out, timestampUs};
    AVFrame *inputFrame = createFrameFromQImage(in);
    if (!inputFrame)
        return {out, timestampUs};

    if (inputFrame->width != m_dstW || inputFrame->height != m_dstH)
    {
        LOG_ERROR("encodeCPU size mismatch: input={}x{}, encoder={}x{}. Internal scaling is disabled; scale in capture path.",
                  inputFrame->width, inputFrame->height, m_dstW, m_dstH);
        av_frame_free(&inputFrame);
        return {out, timestampUs};
    }

    int ret = 0;
    AVFrame *processedFrame = nullptr;

    // 统一先做软件像素格式/尺寸转换，避免分支重复
    AVFrame *targetFrame = av_frame_alloc();
    if (!targetFrame)
    {
        av_frame_free(&inputFrame);
        return {out, timestampUs};
    }
    if (!convertToSwFormat(inputFrame, targetFrame, m_targetSwFmt))
    {
        av_frame_free(&inputFrame);
        av_frame_free(&targetFrame);
        return {out, timestampUs};
    }
    av_frame_free(&inputFrame);

    // 若当前是硬件编码器，优先上传到硬件帧；失败则回退直送 targetFrame
    bool tryHwUpload = (m_codecInfo && m_codecInfo->isHardware && m_hwDeviceCtx && m_codecContext->hw_frames_ctx);
    if (tryHwUpload)
    {
        AVFrame *hwFrame = av_frame_alloc();
        if (hwFrame && av_hwframe_get_buffer(m_codecContext->hw_frames_ctx, hwFrame, 0) >= 0)
        {
            if (av_hwframe_transfer_data(hwFrame, targetFrame, 0) >= 0)
            {
                processedFrame = hwFrame;
                av_frame_free(&targetFrame);
                LOG_DEBUG("encodeCPU path: software scale + hardware upload + encode");
            }
            else
            {
                av_frame_free(&hwFrame);
                processedFrame = targetFrame;
                LOG_WARN("Hardware frame upload failed, fallback to direct frame send");
            }
        }
        else
        {
            if (hwFrame)
                av_frame_free(&hwFrame);
            processedFrame = targetFrame;
            LOG_WARN("Hardware frame buffer alloc failed, fallback to direct frame send");
        }
    }
    else
    {
        processedFrame = targetFrame;
        LOG_DEBUG("encodeCPU path: direct frame send");
    }

    // 发送帧到编码器
    if (processedFrame)
    {
        processedFrame->pts = m_pts++;
        ret = avcodec_send_frame(m_codecContext, processedFrame);
        av_frame_free(&processedFrame);

        if (ret < 0)
        {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG_ERROR("Error sending frame to encoder: {}", errbuf);
            return {out, timestampUs};
        }
    }
    else
    {
        return {out, timestampUs};
    }

    // 接收编码后的数据包
    while (true)
    {
        ret = avcodec_receive_packet(m_codecContext, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0)
        {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG_ERROR("Error receiving packet from encoder: {}", errbuf);
            break;
        }
        if (m_packet->size > 0)
        {
            size_t old = out->size();
            out->resize(old + m_packet->size);
            memcpy(out->data() + old, m_packet->data, m_packet->size);
            int64_t pts = m_packet->pts;
            timestampUs = av_rescale_q(pts, m_codecContext->time_base, AVRational{1, 1000000});
        }
        av_packet_unref(m_packet);
    }
    return {out, timestampUs};
}
