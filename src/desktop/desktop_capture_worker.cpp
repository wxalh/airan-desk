#include "desktop_capture_worker.h"
#include "desktop_grab.h"
#include "../codec/h264_encoder.h"
#include <QGuiApplication>
#include <QScreen>
#include <chrono>

#if !defined(AIRAN_COMPATIBLE_WIN7) && ( defined(Q_OS_WIN64) || defined(Q_OS_WIN32) )
static bool copyGpuTextureToQImage(ID3D11Texture2D *srcTexture, QImage &outImage)
{
    if (!srcTexture)
        return false;

    D3D11_TEXTURE2D_DESC srcDesc{};
    srcTexture->GetDesc(&srcDesc);
    if (srcDesc.Width == 0 || srcDesc.Height == 0)
        return false;

    ID3D11Device *device = nullptr;
    srcTexture->GetDevice(&device);
    if (!device)
        return false;

    ID3D11DeviceContext *ctx = nullptr;
    device->GetImmediateContext(&ctx);
    if (!ctx)
    {
        device->Release();
        return false;
    }

    D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.ArraySize = 1;
    stagingDesc.MipLevels = 1;
    stagingDesc.SampleDesc.Count = 1;

    ID3D11Texture2D *staging = nullptr;
    HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging);
    if (FAILED(hr) || !staging)
    {
        ctx->Release();
        device->Release();
        return false;
    }

    ctx->CopyResource(staging, srcTexture);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
    {
        staging->Release();
        ctx->Release();
        device->Release();
        return false;
    }

    QImage image(static_cast<int>(srcDesc.Width), static_cast<int>(srcDesc.Height), QImage::Format_ARGB32);
    if (image.isNull())
    {
        ctx->Unmap(staging, 0);
        staging->Release();
        ctx->Release();
        device->Release();
        return false;
    }

    const int srcStride = static_cast<int>(mapped.RowPitch);
    const int dstStride = image.bytesPerLine();
    const int copyBytes = std::min(srcStride, dstStride);
    for (UINT y = 0; y < srcDesc.Height; ++y)
    {
        memcpy(image.bits() + y * dstStride,
               static_cast<uint8_t *>(mapped.pData) + y * srcStride,
               copyBytes);
    }

    ctx->Unmap(staging, 0);
    staging->Release();
    ctx->Release();
    device->Release();

    outImage = image;
    return true;
}
#endif

DesktopCaptureWorker::DesktopCaptureWorker(QObject *parent)
    : QObject(parent), m_timer(nullptr), m_desktopGrab(nullptr)
{
    LOG_TRACE("Created DesktopCaptureWorker this={} in thread {}", (void *)this, (void *)QThread::currentThread());
}

DesktopCaptureWorker::~DesktopCaptureWorker()
{
    disconnect();
    if (m_timer)
    {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }

    if (m_desktopGrab)
    {
        disconnect(m_desktopGrab.get(), nullptr, this, nullptr);
        m_desktopGrab = nullptr;
    }

    for (auto info : m_subscribers)
    {
        info->encoder.reset();
        delete info;
    }
    m_subscribers.clear();

    LOG_INFO("Destroyed");
}

bool DesktopCaptureWorker::ensureDesktopGrabberLocked()
{
    if (m_desktopGrab)
    {
        return true;
    }

    m_desktopGrab = DesktopGrab::createBestDesktopGrabber(m_screenIndex, nullptr);
    if (!m_desktopGrab)
    {
        LOG_ERROR("Failed to create desktop grabber");
        emit errorOccurred("Failed to create desktop grabber");
        return false;
    }
    return true;
}

void DesktopCaptureWorker::initialize(int screenIndex, int fps)
{
    QMutexLocker locker(&m_mutex);

    if (m_desktopGrab)
    {
        LOG_WARN("Already initialized");
        return;
    }
    m_screenIndex = screenIndex;
    if (!ensureDesktopGrabberLocked())
    {
        return;
    }
    updateCaptureTimerLocked(fps);
}

void DesktopCaptureWorker::addSubscriber(const QString &subscriberId, int dstW, int dstH, int fps)
{
    QMutexLocker locker(&m_mutex);

    if (!ensureDesktopGrabberLocked())
    {
        return;
    }

    if (m_subscribers.contains(subscriberId))
    {
        LOG_WARN("Subscriber {} already exists in worker", subscriberId);
        return;
    }
    auto encoder = std::make_unique<H264Encoder>(this);
    if (!encoder->initialize(m_screenIndex, dstW, dstH, fps))
    {
        LOG_ERROR("Failed to create encoder for subscriber {}", subscriberId);
        emit errorOccurred(QString("Failed to create encoder for %1").arg(subscriberId));
        return;
    }

    SubscriberInfo *info = new SubscriberInfo();
    info->id = subscriberId;
    info->dstW = dstW;
    info->dstH = dstH;
    info->fps = fps;
    info->encoder = std::move(encoder);

    m_subscribers.insert(subscriberId, info);
    LOG_INFO("Worker added subscriber {} ({}x{} @ {}fps)", subscriberId, dstW, dstH, fps);

    // max fps 策略：只增不减可快速更新；整体仍调用一次重平衡确保一致
    reBalanceCaptureFps();
}

void DesktopCaptureWorker::removeSubscriber(const QString &subscriberId)
{
    QMutexLocker locker(&m_mutex);

    SubscriberInfo *info = m_subscribers.value(subscriberId);
    if (!info)
    {
        LOG_WARN("Subscriber {} not found in worker", subscriberId);
        return;
    }

    const int removedFps = info->fps;

    info->encoder.reset();
    delete info;
    m_subscribers.remove(subscriberId);

    LOG_INFO("Worker removed subscriber {}", subscriberId);

    if (m_subscribers.isEmpty() && m_timer && m_timer->isActive())
    {
        m_timer->stop();
        LOG_INFO("Stopped capture timer due to no subscribers");
        if (m_desktopGrab)
        {
            m_desktopGrab->stopCapture();
            m_desktopGrab = nullptr;
            LOG_INFO("Requested desktop grabber to stop capture due to no subscribers");
        }
        return;
    }

    // 只有删除的正好是当前采集 fps，才需要重算最大值
    if (removedFps >= m_captureFps)
    {
        reBalanceCaptureFps();
    }
}

void DesktopCaptureWorker::updateSubscriber(const QString &subscriberId, int dstW, int dstH, int fps)
{
    QMutexLocker locker(&m_mutex);

    SubscriberInfo *info = m_subscribers.value(subscriberId);
    if (!info)
    {
        LOG_WARN("Subscriber {} not found in worker for update", subscriberId);
        return;
    }
    if (info->dstW == dstW && info->dstH == dstH && info->fps == fps)
    {
        LOG_INFO("Subscriber {} parameters unchanged, no update needed", subscriberId);
        return;
    }
    const int oldFps = info->fps;

    info->dstW = dstW;
    info->dstH = dstH;
    info->fps = fps;

    if (!info->encoder)
    {
        auto encoder = std::make_unique<H264Encoder>(this);
        if (!encoder->initialize(m_screenIndex, dstW, dstH, fps))
        {
            LOG_ERROR("Failed to create encoder for subscriber {}", subscriberId);
            emit errorOccurred(QString("Failed to create encoder for %1").arg(subscriberId));
            return;
        }
        info->encoder = std::move(encoder);
    }

    LOG_INFO("Worker updated subscriber {} to {}x{} @ {}fps", subscriberId, dstW, dstH, fps);

    // max fps 策略：
    // - fps 变大，直接提升采集 fps
    // - fps 变小且原来占据最大值，需重算最大值
    if (fps > m_captureFps)
    {
        updateCaptureTimerLocked(fps);
    }
    else if (oldFps >= m_captureFps && fps < oldFps)
    {
        reBalanceCaptureFps();
    }
}

void DesktopCaptureWorker::stopCapture()
{
    QMutexLocker locker(&m_mutex);

    if (m_timer && m_timer->isActive())
    {
        m_timer->stop();
    }

    if (m_desktopGrab)
    {
        m_desktopGrab->stopCapture();
        m_desktopGrab = nullptr;
    }

    for (auto info : m_subscribers)
    {
        if (info)
        {
            info->encoder.reset();
            delete info;
        }
    }
    m_subscribers.clear();
}

void DesktopCaptureWorker::reBalanceCaptureFps()
{
    // 调用方必须已持有 m_mutex（当前仅在 add/update/remove 内调用）
    if (m_subscribers.empty())
    {
        LOG_WARN("No subscribers to rebalance fps");
        if (m_timer && m_timer->isActive())
        {
            m_timer->stop();
            LOG_INFO("Stopped capture timer due to no subscribers");
        }
        return;
    }
    int newMaxFps = 0;
    for (auto it : m_subscribers.values())
    {
        if (it && it->fps > newMaxFps)
        {
            newMaxFps = it->fps;
        }
    }

    updateCaptureTimerLocked(newMaxFps);
}

void DesktopCaptureWorker::updateCaptureTimerLocked(int fps)
{
    if (fps <= 0)
    {
        fps = 30;
    }

    if (!m_timer)
    {
        // initialize 里会创建并 connect，这里仅兜底
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &DesktopCaptureWorker::onTimeout);
    }

    if (fps == m_captureFps)
    {
        // 如果 fps 未变但定时器被停止（例如上一次无订阅时停止），需要重新启动
        if (m_timer && !m_timer->isActive())
        {
            m_timer->start();
            LOG_INFO("Restarted capture timer at {} fps", m_captureFps);
        }
        return;
    }

    LOG_INFO("Capture fps updated: {} -> {}", m_captureFps, fps);
    m_captureFps = fps;
    m_timer->setInterval(1000 / m_captureFps);

    // 保持原来的运行状态
    if (m_timer->isActive())
    {
        // setInterval 对已启动的 timer 生效，但部分平台/Qt 版本行为不一致，显式重启更稳
        m_timer->stop();
    }
    m_timer->start();
}

#if !defined(AIRAN_COMPATIBLE_WIN7) && ( defined(Q_OS_WIN64) || defined(Q_OS_WIN32) )
bool DesktopCaptureWorker::scaleTextureForSubscriberLocked(ID3D11Texture2D *srcTexture, SubscriberInfo *subscriber, ID3D11Texture2D *&outTexture)
{
    outTexture = nullptr;
    if (!srcTexture || !subscriber || subscriber->dstW <= 0 || subscriber->dstH <= 0)
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC srcDesc{};
    srcTexture->GetDesc(&srcDesc);

    ID3D11Device *rawDevice = nullptr;
    srcTexture->GetDevice(&rawDevice);
    if (!rawDevice)
    {
        return false;
    }

    const bool deviceChanged = !subscriber->scaleDevice || subscriber->scaleDevice.Get() != rawDevice;
    const bool configChanged = deviceChanged || !subscriber->scaleOutputTex ||
                               subscriber->scaleSrcW != srcDesc.Width ||
                               subscriber->scaleSrcH != srcDesc.Height ||
                               subscriber->scaleSrcFormat != srcDesc.Format;

    if (deviceChanged)
    {
        subscriber->scaleVideoDevice.Reset();
        subscriber->scaleVideoContext.Reset();
        subscriber->scaleVpEnum.Reset();
        subscriber->scaleProcessor.Reset();
        subscriber->scaleInputTex.Reset();
        subscriber->scaleInputView.Reset();
        subscriber->scaleOutputTex.Reset();
        subscriber->scaleOutputView.Reset();

        subscriber->scaleDevice = rawDevice;

        ComPtr<ID3D11VideoDevice> videoDev;
        if (FAILED(rawDevice->QueryInterface(__uuidof(ID3D11VideoDevice), reinterpret_cast<void **>(videoDev.GetAddressOf()))) || !videoDev)
        {
            LOG_ERROR("GPU scaler: QueryInterface(ID3D11VideoDevice) failed for subscriber {}", subscriber->id);
            rawDevice->Release();
            return false;
        }

        ComPtr<ID3D11DeviceContext> immediateCtx;
        rawDevice->GetImmediateContext(&immediateCtx);
        if (!immediateCtx)
        {
            LOG_ERROR("GPU scaler: GetImmediateContext failed for subscriber {}", subscriber->id);
            rawDevice->Release();
            return false;
        }

        ComPtr<ID3D11VideoContext> videoCtx;
        if (FAILED(immediateCtx->QueryInterface(__uuidof(ID3D11VideoContext), reinterpret_cast<void **>(videoCtx.GetAddressOf()))) || !videoCtx)
        {
            LOG_ERROR("GPU scaler: QueryInterface(ID3D11VideoContext) failed for subscriber {}", subscriber->id);
            rawDevice->Release();
            return false;
        }

        subscriber->scaleVideoDevice = videoDev;
        subscriber->scaleVideoContext = videoCtx;
    }

    rawDevice->Release();

    if (!subscriber->scaleVideoDevice || !subscriber->scaleVideoContext)
    {
        return false;
    }

    if (configChanged)
    {
        subscriber->scaleVpEnum.Reset();
        subscriber->scaleProcessor.Reset();
        subscriber->scaleInputTex.Reset();
        subscriber->scaleInputView.Reset();
        subscriber->scaleOutputTex.Reset();
        subscriber->scaleOutputView.Reset();

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc{};
        contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        contentDesc.InputWidth = srcDesc.Width;
        contentDesc.InputHeight = srcDesc.Height;
        contentDesc.OutputWidth = static_cast<UINT>(subscriber->dstW);
        contentDesc.OutputHeight = static_cast<UINT>(subscriber->dstH);
        contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        HRESULT hr = subscriber->scaleVideoDevice->CreateVideoProcessorEnumerator(&contentDesc, &subscriber->scaleVpEnum);
        if (FAILED(hr) || !subscriber->scaleVpEnum)
        {
            LOG_ERROR("GPU scaler: CreateVideoProcessorEnumerator failed for subscriber {} hr={:#x}",
                      subscriber->id,
                      static_cast<unsigned int>(hr));
            return false;
        }

        hr = subscriber->scaleVideoDevice->CreateVideoProcessor(subscriber->scaleVpEnum.Get(), 0, &subscriber->scaleProcessor);
        if (FAILED(hr) || !subscriber->scaleProcessor)
        {
            LOG_ERROR("GPU scaler: CreateVideoProcessor failed for subscriber {} hr={:#x}",
                      subscriber->id,
                      static_cast<unsigned int>(hr));
            return false;
        }

        UINT fmtFlags = 0;
        hr = subscriber->scaleVpEnum->CheckVideoProcessorFormat(
            srcDesc.Format,
            &fmtFlags);
        if (FAILED(hr) || ((fmtFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0))
        {
            LOG_ERROR("GPU scaler: source format {} is not VP-input compatible for subscriber {} hr={:#x} flags={:#x}",
                      static_cast<int>(srcDesc.Format),
                      subscriber->id,
                      static_cast<unsigned int>(hr),
                      static_cast<unsigned int>(fmtFlags));
            return false;
        }

        D3D11_TEXTURE2D_DESC inDesc{};
        inDesc.Width = srcDesc.Width;
        inDesc.Height = srcDesc.Height;
        inDesc.MipLevels = 1;
        inDesc.ArraySize = 1;
        inDesc.Format = srcDesc.Format;
        inDesc.SampleDesc.Count = 1;
        inDesc.Usage = D3D11_USAGE_DEFAULT;
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc{};
        inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        inputViewDesc.Texture2D.MipSlice = 0;
        inputViewDesc.Texture2D.ArraySlice = 0;

        const UINT bindCandidates[] = {
            D3D11_BIND_DECODER,
            D3D11_BIND_SHADER_RESOURCE,
            0u};

        bool inputReady = false;
        HRESULT lastCreateTexHr = S_OK;
        HRESULT lastCreateViewHr = S_OK;
        UINT chosenBindFlags = 0;

        for (UINT bindFlags : bindCandidates)
        {
            subscriber->scaleInputTex.Reset();
            subscriber->scaleInputView.Reset();

            inDesc.BindFlags = bindFlags;
            HRESULT texHr = subscriber->scaleDevice->CreateTexture2D(&inDesc, nullptr, &subscriber->scaleInputTex);
            if (FAILED(texHr) || !subscriber->scaleInputTex)
            {
                lastCreateTexHr = texHr;
                continue;
            }

            HRESULT viewHr = subscriber->scaleVideoDevice->CreateVideoProcessorInputView(
                subscriber->scaleInputTex.Get(),
                subscriber->scaleVpEnum.Get(),
                &inputViewDesc,
                &subscriber->scaleInputView);
            if (FAILED(viewHr) || !subscriber->scaleInputView)
            {
                lastCreateViewHr = viewHr;
                continue;
            }

            chosenBindFlags = bindFlags;
            inputReady = true;
            break;
        }

        if (!inputReady)
        {
            LOG_ERROR("GPU scaler: failed to prepare input texture/view for subscriber {} (fmt={}, lastCreateTexHr={:#x}, lastCreateViewHr={:#x})",
                      subscriber->id,
                      static_cast<int>(srcDesc.Format),
                      static_cast<unsigned int>(lastCreateTexHr),
                      static_cast<unsigned int>(lastCreateViewHr));
            return false;
        }

        LOG_INFO("GPU scaler: input texture/view ready for subscriber {} with bindFlags={:#x}",
                 subscriber->id,
                 static_cast<unsigned int>(chosenBindFlags));

        D3D11_TEXTURE2D_DESC outDesc{};
        outDesc.Width = static_cast<UINT>(subscriber->dstW);
        outDesc.Height = static_cast<UINT>(subscriber->dstH);
        outDesc.MipLevels = 1;
        outDesc.ArraySize = 1;
        outDesc.Format = srcDesc.Format;
        outDesc.SampleDesc.Count = 1;
        outDesc.Usage = D3D11_USAGE_DEFAULT;
        outDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        hr = subscriber->scaleDevice->CreateTexture2D(&outDesc, nullptr, &subscriber->scaleOutputTex);
        if (FAILED(hr) || !subscriber->scaleOutputTex)
        {
            LOG_ERROR("GPU scaler: CreateTexture2D(output) failed for subscriber {} hr={:#x}",
                      subscriber->id,
                      static_cast<unsigned int>(hr));
            return false;
        }

        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outViewDesc{};
        outViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        outViewDesc.Texture2D.MipSlice = 0;
        hr = subscriber->scaleVideoDevice->CreateVideoProcessorOutputView(
            subscriber->scaleOutputTex.Get(),
            subscriber->scaleVpEnum.Get(),
            &outViewDesc,
            &subscriber->scaleOutputView);
        if (FAILED(hr) || !subscriber->scaleOutputView)
        {
            LOG_ERROR("GPU scaler: CreateVideoProcessorOutputView failed for subscriber {} hr={:#x}",
                      subscriber->id,
                      static_cast<unsigned int>(hr));
            return false;
        }

        subscriber->scaleSrcW = srcDesc.Width;
        subscriber->scaleSrcH = srcDesc.Height;
        subscriber->scaleSrcFormat = srcDesc.Format;
        LOG_INFO("Configured GPU scaler for subscriber {}: {}x{} -> {}x{}",
                 subscriber->id,
                 srcDesc.Width,
                 srcDesc.Height,
                 subscriber->dstW,
                 subscriber->dstH);
    }

    if (!subscriber->scaleInputTex || !subscriber->scaleInputView)
    {
        LOG_ERROR("GPU scaler: scaler input resources not ready for subscriber {}", subscriber->id);
        return false;
    }

    ComPtr<ID3D11DeviceContext> immediateCtx;
    subscriber->scaleDevice->GetImmediateContext(&immediateCtx);
    if (!immediateCtx)
    {
        LOG_ERROR("GPU scaler: GetImmediateContext before CopyResource failed for subscriber {}", subscriber->id);
        return false;
    }

    immediateCtx->CopyResource(subscriber->scaleInputTex.Get(), srcTexture);

    RECT srcRect{0, 0, static_cast<LONG>(srcDesc.Width), static_cast<LONG>(srcDesc.Height)};
    RECT dstRect{0, 0, subscriber->dstW, subscriber->dstH};

    subscriber->scaleVideoContext->VideoProcessorSetStreamFrameFormat(
        subscriber->scaleProcessor.Get(),
        0,
        D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    subscriber->scaleVideoContext->VideoProcessorSetStreamSourceRect(
        subscriber->scaleProcessor.Get(),
        0,
        TRUE,
        &srcRect);
    subscriber->scaleVideoContext->VideoProcessorSetStreamDestRect(
        subscriber->scaleProcessor.Get(),
        0,
        TRUE,
        &dstRect);
    subscriber->scaleVideoContext->VideoProcessorSetOutputTargetRect(
        subscriber->scaleProcessor.Get(),
        TRUE,
        &dstRect);

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = subscriber->scaleInputView.Get();

    HRESULT hr = subscriber->scaleVideoContext->VideoProcessorBlt(
        subscriber->scaleProcessor.Get(),
        subscriber->scaleOutputView.Get(),
        0,
        1,
        &stream);
    if (FAILED(hr))
    {
        LOG_ERROR("GPU scaler: VideoProcessorBlt failed for subscriber {} hr={:#x}",
                  subscriber->id,
                  static_cast<unsigned int>(hr));
        return false;
    }

    outTexture = subscriber->scaleOutputTex.Get();
    outTexture->AddRef();
    return true;
}
#endif


bool DesktopCaptureWorker::captureAndEncodeGPU()
{
    #if !defined(AIRAN_COMPATIBLE_WIN7) && ( defined(Q_OS_WIN64) || defined(Q_OS_WIN32) )
    QMutexLocker locker(&m_mutex);
    ID3D11Texture2D *texture = nullptr;
    if (!m_desktopGrab)
    {
        return false;
    }

    const bool grabOk = m_desktopGrab->grabFrameGPU(texture);
    if (!grabOk)
    {
        if (!m_gpuLogTimer.isValid())
        {
            m_gpuLogTimer.start();
        }
        const qint64 nowMs = m_gpuLogTimer.elapsed();
        if (nowMs - m_lastGpuGrabWarnMs > 1000)
        {
            LOG_WARN("Failed to grab GPU frame");
            m_lastGpuGrabWarnMs = nowMs;
        }
        return false;
    }

    LOG_TRACE("grabFrameGPU returned grabOk={}, texture={}", grabOk, (void *)texture);

    if (!texture)
    {
        // GPU path reports success but no updated frame (DXGI timeout): normal, no fallback needed.
        LOG_TRACE("GPU grab succeeded but texture is null (no new frame)");
        return true;
    }
    LOG_TRACE("GPU texture obtained, subscribers to encode: {}", m_subscribers.size());
    D3D11_TEXTURE2D_DESC srcDesc{};
    texture->GetDesc(&srcDesc);
    // 遍历所有订阅者，使用各自的编码器编码
    for (auto it : m_subscribers.values())
    {
        ID3D11Texture2D *encodeTexture = texture;
        ID3D11Texture2D *scaledTexture = nullptr;

        if (it->dstW > 0 && it->dstH > 0 &&
            (static_cast<UINT>(it->dstW) != srcDesc.Width || static_cast<UINT>(it->dstH) != srcDesc.Height))
        {
            if (!scaleTextureForSubscriberLocked(texture, it, scaledTexture))
            {
                LOG_TRACE("GPU scaling failed for subscriber {} ({}x{} -> {}x{}), fallback to encoder path",
                         it->id,
                         srcDesc.Width,
                         srcDesc.Height,
                         it->dstW,
                         it->dstH);

                // 兜底：GPU缩放失败时，转CPU缩放避免黑屏（仅失败路径触发）
                QImage cpuImage;
                if (copyGpuTextureToQImage(texture, cpuImage) && !cpuImage.isNull())
                {
                    QImage scaledImage = cpuImage.scaled(it->dstW, it->dstH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    auto cpuResult = it->encoder->encodeCPU(scaledImage);
                    if (!cpuResult.first->empty())
                    {
                        LOG_TRACE("Encoded CPU-fallback frame for subscriber {} -> {} bytes", it->id, cpuResult.first->size());
                        emit frameEncoded(it->id, cpuResult.first, cpuResult.second);
                        continue;
                    }
                    LOG_TRACE("CPU-fallback encode returned empty for subscriber {}", it->id);
                }
                else
                {
                    LOG_TRACE("CPU-fallback texture copy failed for subscriber {}", it->id);
                }
            }
            else
            {
                encodeTexture = scaledTexture;
            }
        }

        if (it->encoder)
        {
            auto result = it->encoder->zeroCopyEncodeGPU(encodeTexture);
            if (result.first->empty())
            {
                result = it->encoder->encodeGPU(encodeTexture);
            }
            if (!result.first->empty())
            {
                LOG_TRACE("Encoded GPU frame for subscriber {} -> {} bytes", it->id, result.first->size());
                emit frameEncoded(it->id, result.first, result.second);
            }
            else
            {
                LOG_TRACE("encodeGPU returned empty for subscriber {}", it->id);
            }
        }

        if (scaledTexture)
        {
            scaledTexture->Release();
            scaledTexture = nullptr;
        }
    }
    if (m_desktopGrab && texture)
    {
        m_desktopGrab->releaseLastFrame(texture);
    }
    return true;
#endif
    return false;
}

bool DesktopCaptureWorker::captureAndEncodeCPU()
{
    QMutexLocker locker(&m_mutex);
    QImage frame;
    if (!m_desktopGrab || !m_desktopGrab->grabFrameCPU(frame))
    {
        LOG_WARN("Failed to grab CPU frame");
        return false;
    }
    // 遍历所有订阅者，使用各自的编码器编码
    for (auto it : m_subscribers.values())
    {
        // 根据订阅者的帧率进行限流
        qint64 interval = (it->fps > 0) ? (1000 / it->fps) : 33;

        if (it->encoder)
        {
            frame = frame.scaled(it->dstW, it->dstH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            auto result = it->encoder->encodeCPU(frame);
            if (!result.first->empty())
            {
                emit frameEncoded(it->id, result.first, result.second);

                LOG_TRACE("Encoded CPU frame for subscriber {} ({}x{} @ {}fps) in worker thread",
                          it->id, it->dstW, it->dstH, it->fps);
            }
        }
    }
    return true;
}

void DesktopCaptureWorker::onTimeout()
{
#if !defined(AIRAN_COMPATIBLE_WIN7) && ( defined(Q_OS_WIN64) || defined(Q_OS_WIN32) )
    if (captureAndEncodeGPU())
    {
        return;
    }
#endif
    captureAndEncodeCPU();
}
