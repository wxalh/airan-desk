#include "h264_decoder.h"
#include "hardware_context_manager.h"
#include <QMap>
#include <QMutex>
#include <algorithm>
#include <vector>

H264Decoder::H264Decoder(QObject *parent)
    : QObject(parent), m_codecContext(nullptr), m_hwDeviceCtx(nullptr), m_initialized(false)
{
}

H264Decoder::~H264Decoder()
{
    QMutexLocker locker(&m_mutex);
    cleanup();
}

bool H264Decoder::initialize()
{
    QMutexLocker locker(&m_mutex);

    if (m_initialized)
    {
        LOG_INFO("Decoder already initialized");
        return true;
    }

    // 逐一尝试硬件加速器
    for (const auto &codecInfo : FFmpegUtil->getH264Decoders())
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
        LOG_INFO("✓ Successfully initialized H264 decoder with {} acceleration", m_codecInfo->name);
    }
    else
    {
        LOG_ERROR("❌ Failed to initialize H264 decoder with any method");
        cleanup();
    }

    return m_initialized;
}

bool H264Decoder::initializeCodec(std::shared_ptr<CodecInfo> codecInfo)
{
    // 创建解码器上下文
    m_codecContext = avcodec_alloc_context3(codecInfo->codec);
    if (!m_codecContext)
    {
        LOG_ERROR("Could not allocate video codec context");
        return false;
    }

    m_codecContext->time_base = AVRational{1, ConfigUtil->fps};
    m_codecContext->framerate = AVRational{ConfigUtil->fps, 1};
    // 智能硬件加速初始化
    bool hardwareInitialized = false;
    if (codecInfo->isHardware)
    {
        LOG_DEBUG("Setting hardware decoding parameters for: {}", codecInfo->name);
        if (initializeHardwareAccel(codecInfo))
        {
            // 设置get_format回调函数，这是硬件解码的关键
            m_codecContext->get_format = get_hw_format;
            m_codecContext->opaque = this; // 传递this指针给回调函数
            hardwareInitialized = true;
            LOG_DEBUG("Hardware acceleration setup completed for: {}", codecInfo->name);
        }
        else
        {
            LOG_WARN("Hardware acceleration setup failed for: {}", codecInfo->name);
        }
    }

    // 打开解码器（硬件或软件）
    int ret = avcodec_open2(m_codecContext, codecInfo->codec, nullptr);
    if (ret < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR("Software decoder failed to open: {}", errbuf);
        return false;
    }

    // 分配帧
    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        LOG_ERROR("Could not allocate video frame");
        return false;
    }
    av_frame_free(&frame);
    // 分配数据包
    AVPacket *packet = av_packet_alloc();
    if (!packet)
    {
        LOG_ERROR("Could not allocate packet");
        return false;
    }
    av_packet_free(&packet);
    LOG_INFO("Decoder initialization completed for: {}", codecInfo->name);
    return true;
}

bool H264Decoder::initializeHardwareAccel(std::shared_ptr<CodecInfo> codecInfo)
{
    // 使用共享的硬件设备上下文管理器
    LOG_INFO("Getting shared hardware device context for: {}", codecInfo->name);

    const QList<std::shared_ptr<HwAccelInfo>> &candidates = codecInfo->supportedHwTypes;

    for (const auto &hwaccelInfo : candidates)
    {
        if (!hwaccelInfo)
            continue;

        if (m_disabledHwTypes.contains(static_cast<int>(hwaccelInfo->hwDeviceType)))
        {
            LOG_WARN("Skipping blacklisted hw backend {} for decoder {}",
                     hwaccelInfo->hwDeviceTypeName,
                     codecInfo->name);
            continue;
        }

        m_hwDeviceCtx = HardwareContextManager::instance().getDeviceContext(hwaccelInfo->hwDeviceType);

        if (m_hwDeviceCtx)
        {
            m_hwAccelInfo = hwaccelInfo;
            break;
        }
        LOG_ERROR("Failed to create/get hardware device context for {}", hwaccelInfo->hwDeviceTypeName);
    }
    if (!m_hwDeviceCtx)
    {
        LOG_ERROR("Failed to obtain hardware device context for: {}", codecInfo->name);
        return false;
    }
    // 将硬件设备上下文分配给解码器上下文
    m_codecContext->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
    LOG_INFO("Successfully assigned hardware device {} to decoder", m_hwAccelInfo->hwDeviceTypeName);
    return true;
}

QImage H264Decoder::decodeFrame(const rtc::binary &h264Data)
{
    QMutexLocker locker(&m_mutex);

    if (!m_initialized)
    {
        LOG_ERROR("Decoder not initialized");
        return QImage();
    }
    if (h264Data.empty())
    {
        return QImage();
    }

    AVPacket *packet = av_packet_alloc();
    if (!packet)
    {
        LOG_ERROR("Failed to allocate AVPacket");
        return QImage();
    }

    // 关键：不要直接让 packet->data 指向 rtc::binary 的内存。
    // 在部分平台/FFmpeg 版本/优化下会触发未定义行为（尤其是 ARM 上更容易 SIGBUS）。
    // 这里改为创建引用计数 packet buffer 并拷贝数据。
    int pktSize = static_cast<int>(h264Data.size());
    int ret = av_new_packet(packet, pktSize);
    if (ret < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR("av_new_packet failed: {}", errbuf);
        av_packet_free(&packet);
        return QImage();
    }
    memcpy(packet->data, h264Data.data(), pktSize);

    ret = avcodec_send_packet(m_codecContext, packet);
    av_packet_free(&packet);
    if (ret < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR("Error sending packet to decoder: {}", errbuf);
        return QImage();
    }

    QImage result;
    bool gotFrame = false;
    // 支持多帧输出（只返回首帧，后续可扩展为信号发送全部帧）
    while (true)
    {
        AVFrame *frameToConvert = av_frame_alloc();
        ret = avcodec_receive_frame(m_codecContext, frameToConvert);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            av_frame_free(&frameToConvert);
            break;
        }
        else if (ret < 0)
        {
            av_frame_free(&frameToConvert);
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG_ERROR("Error receiving frame from decoder: {}", errbuf);
            break;
        }
        gotFrame = true;

        if (m_codecInfo->isHardware)
        {
            AVPixelFormat frameFormat = static_cast<AVPixelFormat>(frameToConvert->format);
            // 简化硬件帧判断逻辑
            bool isHardwareFrame = m_codecInfo->isHardware && frameToConvert->hw_frames_ctx;
            if (isHardwareFrame)
            {
                std::vector<AVPixelFormat> transferCandidates;
                auto appendCandidate = [&transferCandidates](AVPixelFormat fmt) {
                    if (fmt == AV_PIX_FMT_NONE)
                        return;
                    if (std::find(transferCandidates.begin(), transferCandidates.end(), fmt) == transferCandidates.end())
                        transferCandidates.push_back(fmt);
                };

                // 优先尝试 codec 的 sw_pix_fmt
                appendCandidate(m_codecContext->sw_pix_fmt);

                // 从 hwframe 查询实际可 transfer 的系统内存格式
                AVPixelFormat *transferFormats = nullptr;
                int fmtRet = av_hwframe_transfer_get_formats(frameToConvert->hw_frames_ctx,
                                                             AV_HWFRAME_TRANSFER_DIRECTION_FROM,
                                                             &transferFormats,
                                                             0);
                if (fmtRet >= 0 && transferFormats)
                {
                    for (int idx = 0; transferFormats[idx] != AV_PIX_FMT_NONE; ++idx)
                    {
                        appendCandidate(transferFormats[idx]);
                    }
                }
                if (transferFormats)
                {
                    av_freep(&transferFormats);
                }

                // 通用兜底顺序
                appendCandidate(AV_PIX_FMT_NV12);
                appendCandidate(AV_PIX_FMT_YUV420P);
                if (frameFormat == AV_PIX_FMT_DRM_PRIME)
                {
                    appendCandidate(AV_PIX_FMT_YUV420P);
                }

                AVFrame *convertedFrame = nullptr;
                int transferRet = AVERROR(EINVAL);
                AVPixelFormat usedFmt = AV_PIX_FMT_NONE;
                for (AVPixelFormat transferTarget : transferCandidates)
                {
                    AVFrame *swFrame = av_frame_alloc();
                    if (!swFrame)
                    {
                        continue;
                    }
                    swFrame->format = transferTarget;
                    swFrame->width = frameToConvert->width;
                    swFrame->height = frameToConvert->height;

                    int allocRet = av_frame_get_buffer(swFrame, 32);
                    if (allocRet < 0)
                    {
                        av_frame_free(&swFrame);
                        continue;
                    }

                    transferRet = av_hwframe_transfer_data(swFrame, frameToConvert, 0);
                    if (transferRet >= 0)
                    {
                        convertedFrame = swFrame;
                        usedFmt = transferTarget;
                        break;
                    }
                    av_frame_free(&swFrame);
                }

                if (!convertedFrame)
                {
                    const bool canFallbackHw = (m_codecInfo && m_codecInfo->isHardware && m_hwAccelInfo);
                    AVHWDeviceType failedHwType = AV_HWDEVICE_TYPE_NONE;
                    QString failedHwName;
                    if (canFallbackHw)
                    {
                        failedHwType = m_hwAccelInfo->hwDeviceType;
                        failedHwName = m_hwAccelInfo->hwDeviceTypeName;
                    }

                    av_frame_free(&frameToConvert);
                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(transferRet, errbuf, sizeof(errbuf));
                    LOG_ERROR("Error transferring frame data from hardware after trying {} formats: {}",
                              transferCandidates.size(), errbuf);

                    if (canFallbackHw && failedHwType != AV_HWDEVICE_TYPE_NONE)
                    {
                        m_disabledHwTypes.insert(static_cast<int>(failedHwType));
                        LOG_WARN("Blacklisted failed hw backend {}, reinitializing decoder to try next hardware backend",
                                 failedHwName);

                        cleanup();
                        locker.unlock();
                        const bool reinitOk = initialize();
                        locker.relock();
                        if (!reinitOk)
                        {
                            LOG_ERROR("Decoder reinitialize failed after blacklisting {}", failedHwName);
                        }
                    }
                    continue;
                }

                av_frame_free(&frameToConvert);
                frameToConvert = convertedFrame;
                LOG_TRACE("Hardware frame transfer succeeded with sw format {}", av_get_pix_fmt_name(usedFmt));
            }
            else if (frameFormat != m_codecContext->sw_pix_fmt)
            {
                // 软件帧统一转m_codecContext->sw_pix_fmt，简化后续处理逻辑
                AVFrame *tmpFrame = av_frame_alloc();
                if (convertToTargetFmt(frameToConvert, tmpFrame))
                {
                    av_frame_free(&frameToConvert);
                    frameToConvert = tmpFrame;
                }
                else
                {
                    av_frame_free(&tmpFrame);
                }
            }
        }
        if (frameToConvert)
        {
            result = avframeToQImage(frameToConvert);
            av_frame_free(&frameToConvert);
        }
        break; // 只处理首帧，如需全部帧可去掉break
    }
    return result;
}

bool H264Decoder::convertToTargetFmt(AVFrame *inputFrame, AVFrame *outputFrame)
{
    if (!inputFrame || !outputFrame)
    {
        return false;
    }
    // 基本合法性检查
    if (inputFrame->width <= 0 || inputFrame->height <= 0)
    {
        LOG_ERROR("convertToTargetFmt: invalid input size: {}x{}", inputFrame->width, inputFrame->height);
        return false;
    }
    // 检查 data 指针与 linesize
    if (!inputFrame->data[0])
    {
        LOG_ERROR("convertToTargetFmt: inputFrame->data[0] is null");
        return false;
    }
    // 如果指针看起来像小数字（非有效地址），记录并失败
    uintptr_t d0 = reinterpret_cast<uintptr_t>(inputFrame->data[0]);
    if (d0 != 0 && d0 < 0x1000)
    {
        LOG_ERROR("convertToTargetFmt: suspicious input data[0] pointer: 0x{:x}", d0);
        return false;
    }
    int srcW = inputFrame->width;
    int srcH = inputFrame->height;
    // 基本 linesize 校验
    int ls0 = inputFrame->linesize[0];
    int ls1 = inputFrame->linesize[1];
    if (ls0 < srcW || ls1 < (srcW + 1) / 2)
    {
        LOG_ERROR("convertToTargetFmt: suspicious linesize: ls0={} ls1={} for width={}", ls0, ls1, srcW);
        return false;
    }

    outputFrame->format = m_codecContext->sw_pix_fmt;
    outputFrame->width = srcW;
    outputFrame->height = srcH;

    int ret = av_frame_get_buffer(outputFrame, 32);
    if (ret < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR("Failed to allocate {} frame buffer: {}", av_get_pix_fmt_name(m_codecContext->sw_pix_fmt), errbuf);
        return false;
    }
    AVPixelFormat inputFormat = static_cast<AVPixelFormat>(inputFrame->format);

    // 创建转换上下文
    SwsContext *swsContext = sws_getContext(
        inputFrame->width, inputFrame->height, inputFormat,
        outputFrame->width, outputFrame->height, m_codecContext->sw_pix_fmt,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsContext)
    {
        LOG_ERROR("Failed to create sws context for {} conversion from {}", av_get_pix_fmt_name(m_codecContext->sw_pix_fmt), av_get_pix_fmt_name(inputFormat));
        return false;
    }
    // 执行转换
    int scaledHeight = sws_scale(swsContext,
                                 inputFrame->data, inputFrame->linesize, 0, inputFrame->height,
                                 outputFrame->data, outputFrame->linesize);

    sws_freeContext(swsContext);

    if (scaledHeight != inputFrame->height)
    {
        LOG_ERROR("Failed to convert {} frame to {}: expected {} lines, got {}", av_get_pix_fmt_name(inputFormat), av_get_pix_fmt_name(m_codecContext->sw_pix_fmt), inputFrame->height, scaledHeight);
        return false;
    }

    LOG_DEBUG("Successfully converted {} frame to {}, videoSize: {}x{}", av_get_pix_fmt_name(inputFormat), av_get_pix_fmt_name((AVPixelFormat)outputFrame->format), outputFrame->width, outputFrame->height);
    return true;
}

QImage H264Decoder::avframeToQImage(AVFrame *frame)
{
    if (!frame)
    {
        return QImage();
    }
    // 使用 32位对齐，渲染更快，兼容性更好
    const AVPixelFormat avTargetFormat = AV_PIX_FMT_RGB32;
    const QImage::Format qtImageFormat = QImage::Format_RGB32;

    // ==========================================
    int width = frame->width;
    int height = frame->height;
    AVPixelFormat inputFormat = static_cast<AVPixelFormat>(frame->format);

    // 准备内存对齐的数据
    int numBytes = av_image_get_buffer_size(avTargetFormat, width, height, 32);
    if (numBytes < 0)
    {
        LOG_ERROR("Failed to get buffer size for image");
        return QImage();
    }
    uint8_t *buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    if (!buffer)
    {
        LOG_ERROR("Failed to allocate buffer for image");
        return QImage();
    }

    // 3. 执行转换
    uint8_t *dstData[4] = {0};
    int dstLinesize[4] = {0};
    av_image_fill_arrays(dstData, dstLinesize, buffer, avTargetFormat, width, height, 32);

    // 注意：这里目标格式用 AV_PIX_FMT_RGB32
    SwsContext *swsContext = sws_getContext(
        width, height, inputFormat,
        width, height, avTargetFormat,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsContext)
    {
        LOG_ERROR("Failed to create sws context for AVFrame to QImage conversion");
        av_free(buffer);
        return QImage();
    }
    int result = sws_scale(swsContext,
                           frame->data, frame->linesize, 0, height,
                           dstData, dstLinesize);
    sws_freeContext(swsContext);
    if (result < 0)
    {
        av_free(buffer);
        LOG_ERROR("sws_scale failed: expected {} lines, got {}", height, result);
        return QImage();
    }
    // 2. 创建 QImage
    QImage image(width, height, qtImageFormat);
    // ✅ 逐行拷贝，规避 linesize 差异和越界风险
    for (int i = 0; i < height; ++i)
    {
        memcpy(image.scanLine(i), dstData[0] + i * dstLinesize[0], width * 4);
    }
    av_free(buffer);
    return image;
}

void H264Decoder::cleanup()
{
    // 标记为未初始化，阻止新的解码请求进入
    m_initialized = false;

    if (m_codecContext)
    {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }

    // 释放硬件设备上下文的引用（共享管理器会处理实际的释放）
    if (m_hwDeviceCtx)
    {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }

    m_initialized = false;

    LOG_DEBUG("H264Decoder cleanup completed");
}

// 硬件解码的关键回调函数 - 根据FFmpeg官方示例
enum AVPixelFormat H264Decoder::get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    H264Decoder *decoder = static_cast<H264Decoder *>(ctx->opaque);
    if (!decoder)
    {
        LOG_ERROR("Decoder instance is null in get_hw_format callback");
        return AV_PIX_FMT_NONE;
    }
    for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
    {
        if (decoder->m_hwAccelInfo && decoder->m_hwAccelInfo->supportedPixFormats.contains(*p))
        {
            LOG_DEBUG("Selected hardware pixel format: {}", av_get_pix_fmt_name(*p));
            return *p;
        }
    }

    if (pix_fmts && pix_fmts[0] != AV_PIX_FMT_NONE)
    {
        LOG_WARN("No matching hardware pixel format found, fallback to software pixel format: {}", av_get_pix_fmt_name(pix_fmts[0]));
        return pix_fmts[0];
    }

    LOG_ERROR("No suitable pixel format found");
    return AV_PIX_FMT_NONE;
}
