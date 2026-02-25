#include "h264_encoder.h"

#if !defined(AIRAN_COMPATIBLE_WIN7) && ( defined(Q_OS_WIN64) || defined(Q_OS_WIN32) )
#include <d3d11.h>
#include <d3d10.h>
#include <windows.h>
extern "C"
{
#include <libavutil/hwcontext_d3d11va.h>
}

namespace {

struct WinVersion
{
    DWORD major{0};
    DWORD minor{0};
};

WinVersion queryWindowsVersion()
{
    WinVersion v{};
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
    {
        return v;
    }

    using RtlGetVersionFn = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);
    auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtlGetVersion)
    {
        return v;
    }

    RTL_OSVERSIONINFOW osvi{};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    if (rtlGetVersion(&osvi) == 0)
    {
        v.major = osvi.dwMajorVersion;
        v.minor = osvi.dwMinorVersion;
    }
    return v;
}

bool isWin8OrGreaterRuntime()
{
    const WinVersion v = queryWindowsVersion();
    return (v.major > 6) || (v.major == 6 && v.minor >= 2);
}

} // namespace

static void enableD3D11MultithreadProtection(ID3D11Device *device)
{
    if (!device)
        return;

    ID3D10Multithread *multithread = nullptr;
    HRESULT hr = device->QueryInterface(__uuidof(ID3D10Multithread), reinterpret_cast<void **>(&multithread));
    if (SUCCEEDED(hr) && multithread)
    {
        multithread->SetMultithreadProtected(TRUE);
        multithread->Release();
    }
}

// 零拷贝：从 ID3D11Texture2D 创建 D3D11VA 硬件帧
AVFrame *H264Encoder::createHwFrameFromD3D11Texture(ID3D11Texture2D *texture)
{
    if (!texture)
        return nullptr;

    ID3D11Device *texDevice = nullptr;
    texture->GetDevice(&texDevice);
    if (!texDevice)
    {
        LOG_ERROR("Failed to get D3D11 device from input texture");
        return nullptr;
    }

    ID3D11DeviceContext *texImmediateCtx = nullptr;
    texDevice->GetImmediateContext(&texImmediateCtx);
    if (!texImmediateCtx)
    {
        texDevice->Release();
        LOG_ERROR("Failed to get immediate context from texture device");
        return nullptr;
    }

    enableD3D11MultithreadProtection(texDevice);

    // 确保 D3D11VA 设备上下文存在，且与当前纹理 device 同源
    bool needRecreateDeviceCtx = (m_d3d11vaDeviceCtx == nullptr);
    if (!needRecreateDeviceCtx)
    {
        AVHWDeviceContext *devCtx = reinterpret_cast<AVHWDeviceContext *>(m_d3d11vaDeviceCtx->data);
        AVD3D11VADeviceContext *d3d11Ctx = devCtx ? reinterpret_cast<AVD3D11VADeviceContext *>(devCtx->hwctx) : nullptr;
        ID3D11Device *boundDev = d3d11Ctx ? d3d11Ctx->device : nullptr;
        LOG_TRACE("Zero-copy device check: texDevice=0x{:x}, boundDevice=0x{:x}",
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(texDevice)),
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(boundDev)));
        if (!boundDev || boundDev != texDevice)
        {
            needRecreateDeviceCtx = true;
        }
    }

    if (needRecreateDeviceCtx)
    {
        if (m_d3d11vaFramesCtx)
        {
            av_buffer_unref(&m_d3d11vaFramesCtx);
            m_d3d11vaFramesCtx = nullptr;
        }
        if (m_d3d11vaDeviceCtx)
        {
            av_buffer_unref(&m_d3d11vaDeviceCtx);
            m_d3d11vaDeviceCtx = nullptr;
        }

        AVBufferRef *devRef = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        if (!devRef)
        {
            texImmediateCtx->Release();
            texDevice->Release();
            LOG_ERROR("Failed to allocate D3D11VA hwdevice context");
            return nullptr;
        }

        AVHWDeviceContext *devCtx = reinterpret_cast<AVHWDeviceContext *>(devRef->data);
        AVD3D11VADeviceContext *d3d11Ctx = reinterpret_cast<AVD3D11VADeviceContext *>(devCtx->hwctx);
        d3d11Ctx->device = texDevice;
        d3d11Ctx->device_context = texImmediateCtx;
        d3d11Ctx->device->AddRef();
        d3d11Ctx->device_context->AddRef();

        int ret = av_hwdevice_ctx_init(devRef);
        if (ret < 0)
        {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG_ERROR("Failed to init D3D11VA device context from texture device: {}", errbuf);
            av_buffer_unref(&devRef);
            texImmediateCtx->Release();
            texDevice->Release();
            return nullptr;
        }

        m_d3d11vaDeviceCtx = devRef;
        LOG_INFO("Bound D3D11VA device context to DesktopGrabDXGI texture device: texDevice=0x{:x}",
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(texDevice)));
    }

    // 获取纹理描述
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    // 复用（或按分辨率重建）D3D11VA frames ctx，避免每帧分配造成内存上涨
    bool needRecreateFramesCtx = (m_d3d11vaFramesCtx == nullptr);
    if (!needRecreateFramesCtx)
    {
        AVHWFramesContext *existing = reinterpret_cast<AVHWFramesContext *>(m_d3d11vaFramesCtx->data);
        if (!existing || existing->width != static_cast<int>(desc.Width) || existing->height != static_cast<int>(desc.Height))
        {
            needRecreateFramesCtx = true;
        }
    }

    if (needRecreateFramesCtx)
    {
        if (m_d3d11vaFramesCtx)
        {
            av_buffer_unref(&m_d3d11vaFramesCtx);
            m_d3d11vaFramesCtx = nullptr;
        }

        AVBufferRef *framesRef = av_hwframe_ctx_alloc(m_d3d11vaDeviceCtx);
        if (!framesRef)
        {
            LOG_ERROR("Failed to allocate reusable hwframe context for D3D11VA");
            return nullptr;
        }

        AVHWFramesContext *framesCtx = reinterpret_cast<AVHWFramesContext *>(framesRef->data);
        framesCtx->format = AV_PIX_FMT_D3D11;
        framesCtx->sw_format = AV_PIX_FMT_BGRA;
        framesCtx->width = desc.Width;
        framesCtx->height = desc.Height;
        framesCtx->initial_pool_size = 8;

        int ret = av_hwframe_ctx_init(framesRef);
        if (ret < 0)
        {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG_ERROR("Failed to init reusable D3D11VA hwframe context: {}", errbuf);
            av_buffer_unref(&framesRef);
            return nullptr;
        }
        m_d3d11vaFramesCtx = framesRef;
        LOG_INFO("Rebuilt reusable D3D11VA frames ctx: {}x{}", desc.Width, desc.Height);
    }

    // 从绑定输入纹理的 hwframes context 中申请帧，确保 frame 属于该 context
    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return nullptr;

    int getBufRet = av_hwframe_get_buffer(m_d3d11vaFramesCtx, frame, 0);
    if (getBufRet < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(getBufRet, errbuf, sizeof(errbuf));
        LOG_ERROR("Failed to get D3D11 frame from reusable hwframe context: {}", errbuf);
        av_frame_free(&frame);
        return nullptr;
    }

    ID3D11Texture2D *dstTex = reinterpret_cast<ID3D11Texture2D *>(frame->data[0]);
    int dstIndex = static_cast<int>(reinterpret_cast<intptr_t>(frame->data[1]));
    if (!dstTex)
    {
        texImmediateCtx->Release();
        texDevice->Release();
        av_frame_free(&frame);
        LOG_ERROR("Failed to get destination D3D11 texture from pooled hwframe");
        return nullptr;
    }

    UINT dstSubresource = D3D11CalcSubresource(0, static_cast<UINT>(dstIndex), 1);
    texImmediateCtx->CopySubresourceRegion(reinterpret_cast<ID3D11Resource *>(dstTex), dstSubresource,
                                           0, 0, 0,
                                           reinterpret_cast<ID3D11Resource *>(texture), 0, nullptr);

    texImmediateCtx->Release();
    texDevice->Release();

    return frame;
}

bool H264Encoder::reinitializeQsvSessionForZeroCopy()
{
    if (!m_codecInfo || !m_hwaccelInfo || m_hwaccelInfo->hwDeviceType != AV_HWDEVICE_TYPE_QSV)
        return true;

    if (!m_d3d11vaDeviceCtx)
    {
        LOG_ERROR("QSV session reinit skipped: d3d11va device ctx is null");
        return false;
    }

    AVBufferRef *derivedQsvCtx = nullptr;
    int deriveRet = av_hwdevice_ctx_create_derived(&derivedQsvCtx,
                                                   AV_HWDEVICE_TYPE_QSV,
                                                   m_d3d11vaDeviceCtx,
                                                   0);
    if (deriveRet < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(deriveRet, errbuf, sizeof(errbuf));
        LOG_ERROR("Failed to derive first-frame QSV device from d3d11va: {}", errbuf);
        return false;
    }

    if (m_filterGraph)
        avfilter_graph_free(&m_filterGraph);
    m_filterGraph = nullptr;
    m_bufferSrcCtx = nullptr;
    m_bufferSinkCtx = nullptr;
    if (m_filterFramesCtx)
        av_buffer_unref(&m_filterFramesCtx);
    m_filterFramesCtx = nullptr;
    m_filterSrcW = 0;
    m_filterSrcH = 0;
    m_filterNeedScale = false;

    if (m_packet)
        av_packet_free(&m_packet);
    m_packet = nullptr;
    if (m_frame)
        av_frame_free(&m_frame);
    m_frame = nullptr;
    if (m_hwFrame)
        av_frame_free(&m_hwFrame);
    m_hwFrame = nullptr;
    if (m_swsContext)
        sws_freeContext(m_swsContext);
    m_swsContext = nullptr;
    if (m_codecContext)
        avcodec_free_context(&m_codecContext);
    m_codecContext = nullptr;
    if (m_hwDeviceCtx)
        av_buffer_unref(&m_hwDeviceCtx);
    m_hwDeviceCtx = nullptr;
    if (m_forcedQsvDeviceCtx)
        av_buffer_unref(&m_forcedQsvDeviceCtx);
    if (m_forcedQsvFramesCtx)
        av_buffer_unref(&m_forcedQsvFramesCtx);
    m_forcedQsvFramesCtx = nullptr;
    m_forcedQsvDeviceCtx = derivedQsvCtx;

    bool ok = initializeCodec(m_codecInfo);
    if (!ok)
    {
        LOG_ERROR("Failed to reopen QSV encoder with first-frame derived session");
        return false;
    }

    m_qsvSessionBound = true;
    m_qsvFramesBound = false;
    m_initialized = true;
    LOG_INFO("QSV encoder session rebound once using first-frame D3D11 device");
    return true;
}

bool H264Encoder::reinitializeD3d11vaSessionForZeroCopy()
{
    if (!m_codecInfo || !m_hwaccelInfo || m_hwaccelInfo->hwDeviceType != AV_HWDEVICE_TYPE_D3D11VA)
        return true;

    if (!m_d3d11vaDeviceCtx || !m_d3d11vaFramesCtx)
    {
        LOG_ERROR("D3D11VA session reinit skipped: source D3D11VA contexts are null");
        return false;
    }

    if (m_filterGraph)
        avfilter_graph_free(&m_filterGraph);
    m_filterGraph = nullptr;
    m_bufferSrcCtx = nullptr;
    m_bufferSinkCtx = nullptr;
    if (m_filterFramesCtx)
        av_buffer_unref(&m_filterFramesCtx);
    m_filterFramesCtx = nullptr;
    m_filterSrcW = 0;
    m_filterSrcH = 0;
    m_filterNeedScale = false;

    if (m_packet)
        av_packet_free(&m_packet);
    m_packet = nullptr;
    if (m_frame)
        av_frame_free(&m_frame);
    m_frame = nullptr;
    if (m_hwFrame)
        av_frame_free(&m_hwFrame);
    m_hwFrame = nullptr;
    if (m_swsContext)
        sws_freeContext(m_swsContext);
    m_swsContext = nullptr;
    if (m_codecContext)
        avcodec_free_context(&m_codecContext);
    m_codecContext = nullptr;
    if (m_hwDeviceCtx)
        av_buffer_unref(&m_hwDeviceCtx);
    m_hwDeviceCtx = nullptr;

    if (m_forcedD3d11DeviceCtx)
        av_buffer_unref(&m_forcedD3d11DeviceCtx);
    if (m_forcedD3d11FramesCtx)
        av_buffer_unref(&m_forcedD3d11FramesCtx);

    m_forcedD3d11DeviceCtx = av_buffer_ref(m_d3d11vaDeviceCtx);
    m_forcedD3d11FramesCtx = av_buffer_ref(m_d3d11vaFramesCtx);
    if (!m_forcedD3d11DeviceCtx || !m_forcedD3d11FramesCtx)
    {
        LOG_ERROR("Failed to reference first-frame D3D11VA contexts for NVENC session binding");
        if (m_forcedD3d11DeviceCtx)
            av_buffer_unref(&m_forcedD3d11DeviceCtx);
        if (m_forcedD3d11FramesCtx)
            av_buffer_unref(&m_forcedD3d11FramesCtx);
        return false;
    }

    bool ok = initializeCodec(m_codecInfo);
    if (!ok)
    {
        LOG_ERROR("Failed to reopen NVENC encoder with first-frame D3D11VA session binding");
        return false;
    }

    m_d3d11SessionBound = true;
    m_initialized = true;
    LOG_INFO("NVENC(D3D11VA) encoder session rebound once using first-frame D3D11 device/frames context");
    return true;
}

bool H264Encoder::reinitializeQsvCodecWithGraphFrames(AVBufferRef *graphFramesCtx)
{
    if (!graphFramesCtx || !m_codecInfo || !m_hwaccelInfo || m_hwaccelInfo->hwDeviceType != AV_HWDEVICE_TYPE_QSV)
        return false;

    AVBufferRef *newForcedFrames = av_buffer_ref(graphFramesCtx);
    if (!newForcedFrames)
    {
        LOG_ERROR("Failed to reference graph output QSV hw_frames_ctx");
        return false;
    }

    if (m_packet)
        av_packet_free(&m_packet);
    m_packet = nullptr;
    if (m_frame)
        av_frame_free(&m_frame);
    m_frame = nullptr;
    if (m_hwFrame)
        av_frame_free(&m_hwFrame);
    m_hwFrame = nullptr;
    if (m_swsContext)
        sws_freeContext(m_swsContext);
    m_swsContext = nullptr;
    if (m_codecContext)
        avcodec_free_context(&m_codecContext);
    m_codecContext = nullptr;
    if (m_hwDeviceCtx)
        av_buffer_unref(&m_hwDeviceCtx);
    m_hwDeviceCtx = nullptr;

    if (m_forcedQsvFramesCtx)
        av_buffer_unref(&m_forcedQsvFramesCtx);
    m_forcedQsvFramesCtx = newForcedFrames;

    bool ok = initializeCodec(m_codecInfo);
    if (!ok)
    {
        LOG_ERROR("Failed to reopen QSV encoder with graph output hw_frames_ctx");
        return false;
    }

    m_qsvFramesBound = true;
    m_initialized = true;
    LOG_INFO("QSV encoder rebound once using graph output hw_frames_ctx");
    return true;
}

// 零拷贝编码：D3D11Texture2D -> 硬件编码器 零拷贝编码
std::pair<std::shared_ptr<rtc::binary>, quint64> H264Encoder::zeroCopyEncodeGPU(ID3D11Texture2D *in)
{
    QMutexLocker locker(&m_mutex);
    std::shared_ptr<rtc::binary> out = std::make_shared<rtc::binary>();
    quint64 timestamp90k = 0;

    if (!isWin8OrGreaterRuntime())
    {
        return {out, timestamp90k};
    }

    if (!m_codecContext || !in)
        return {out, timestamp90k};

    if (!m_zeroCopyHealthy)
    {
        locker.unlock();
        return encodeGPU(in);
    }

    if (!m_codecInfo || !m_codecInfo->isHardware || !m_hwaccelInfo)
    {
        LOG_WARN("zeroCopyEncodeGPU requires hardware encoder");
        return {out, timestamp90k};
    }

    if (m_hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_QSV)
    {
        ID3D11Device *texDev = nullptr;
        in->GetDevice(&texDev);
        if (!texDev)
        {
            m_zeroCopyHealthy = false;
            locker.unlock();
            return encodeGPU(in);
        }

        UINT vendorId = 0;
        ComPtr<IDXGIDevice> dxgiDev;
        HRESULT hrVendor = texDev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(dxgiDev.GetAddressOf()));
        if (SUCCEEDED(hrVendor) && dxgiDev)
        {
            ComPtr<IDXGIAdapter> adapter;
            hrVendor = dxgiDev->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void **>(adapter.GetAddressOf()));
            if (SUCCEEDED(hrVendor) && adapter)
            {
                DXGI_ADAPTER_DESC desc;
                if (SUCCEEDED(adapter->GetDesc(&desc)))
                {
                    vendorId = desc.VendorId;
                }
            }
        }
        texDev->Release();

        if (vendorId != 0x8086)
        {
            LOG_WARN("Skip QSV zero-copy: source texture adapter vendor is 0x{:x} (not Intel), fallback to compatible path",
                     static_cast<unsigned int>(vendorId));
            m_zeroCopyHealthy = false;
            locker.unlock();
            return encodeGPU(in);
        }
    }

    AVFrame *d3d11Frame = createHwFrameFromD3D11Texture(in);
    if (!d3d11Frame)
    {
        LOG_ERROR("Failed to create D3D11VA frame from texture");
        return {out, timestamp90k};
    }

    if (m_hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_QSV)
    {
        if (!m_qsvDeriveChecked)
        {
            AVBufferRef *derivedQsvCtx = nullptr;
            int deriveRet = av_hwdevice_ctx_create_derived(&derivedQsvCtx,
                                                           AV_HWDEVICE_TYPE_QSV,
                                                           m_d3d11vaDeviceCtx,
                                                           0);
            if (deriveRet < 0)
            {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(deriveRet, errbuf, sizeof(errbuf));
                LOG_WARN("QSV derive preflight failed (d3d11->qsv): {}. Disable zero-copy for this session", errbuf);
                m_qsvDeriveOk = false;
                m_qsvDeriveChecked = true;
            }
            else
            {
                m_qsvDeriveOk = true;
                m_qsvDeriveChecked = true;
                av_buffer_unref(&derivedQsvCtx);
                LOG_INFO("QSV derive preflight succeeded, zero-copy path enabled");
            }
        }

        if (!m_qsvDeriveOk)
        {
            m_zeroCopyHealthy = false;
            av_frame_free(&d3d11Frame);
            locker.unlock();
            return encodeGPU(in);
        }

        if (!m_qsvSessionBound)
        {
            if (!reinitializeQsvSessionForZeroCopy())
            {
                m_zeroCopyHealthy = false;
                av_frame_free(&d3d11Frame);
                locker.unlock();
                return encodeGPU(in);
            }
        }
    }

    D3D11_TEXTURE2D_DESC desc;
    in->GetDesc(&desc);

    bool needScale = (desc.Width != static_cast<UINT>(m_dstW) || desc.Height != static_cast<UINT>(m_dstH));
    if (needScale)
    {
        LOG_ERROR("zeroCopyEncodeGPU size mismatch: input={}x{}, encoder={}x{}. Internal scaling is disabled; scale in capture path.",
                  desc.Width, desc.Height, m_dstW, m_dstH);
        av_frame_free(&d3d11Frame);
        return {out, timestamp90k};
    }

    if (m_hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_D3D11VA && !needScale && !m_d3d11SessionBound)
    {
        if (!reinitializeD3d11vaSessionForZeroCopy())
        {
            m_zeroCopyHealthy = false;
            av_frame_free(&d3d11Frame);
            locker.unlock();
            return encodeGPU(in);
        }
    }

    AVFrame *qsvFrame = nullptr;
    int ret = 0;

    bool needGraph = (m_hwaccelInfo->hwDeviceType != AV_HWDEVICE_TYPE_D3D11VA);
    if (!needGraph)
    {
        qsvFrame = d3d11Frame;
        LOG_TRACE("Zero-copy fast path: direct D3D11 frame send");
    }
    else
    {
        bool needRebuildGraph = false;
        if (m_filterGraph)
        {
            if (m_filterSrcW != static_cast<int>(desc.Width) ||
                m_filterSrcH != static_cast<int>(desc.Height) ||
                m_filterNeedScale != false ||
                !m_filterFramesCtx || !d3d11Frame->hw_frames_ctx || m_filterFramesCtx->data != d3d11Frame->hw_frames_ctx->data)
            {
                needRebuildGraph = true;
            }
        }
        if (needRebuildGraph)
        {
            avfilter_graph_free(&m_filterGraph);
            m_filterGraph = nullptr;
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
            if (m_hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_QSV)
                m_qsvFramesBound = false;
            LOG_INFO("Rebuilding zero-copy filter graph: src {}x{}", desc.Width, desc.Height);
        }

        if (!m_filterGraph)
        {
            m_filterGraph = avfilter_graph_alloc();
            if (!m_filterGraph)
            {
                LOG_ERROR("Failed to allocate filter graph");
                av_frame_free(&d3d11Frame);
                m_zeroCopyHealthy = false;
                LOG_WARN("Disable zero-copy path after filter graph allocation failure");
                locker.unlock();
                return encodeGPU(in);
            }

            const AVFilter *bufferSrc = avfilter_get_by_name("buffer");
            char args[512];
            snprintf(args, sizeof(args),
                     "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                     desc.Width, desc.Height, AV_PIX_FMT_D3D11,
                     1, m_fps, 1, 1);

            ret = avfilter_graph_create_filter(&m_bufferSrcCtx, bufferSrc, "in",
                                               args, nullptr, m_filterGraph);
            if (ret < 0)
            {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                LOG_ERROR("Failed to create buffer source filter: {}", errbuf);
                av_frame_free(&d3d11Frame);
                m_zeroCopyHealthy = false;
                LOG_WARN("Disable zero-copy path after buffer source creation failure");
                locker.unlock();
                return encodeGPU(in);
            }

            AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();
            if (!par)
            {
                LOG_ERROR("Failed to allocate AVBufferSrcParameters");
                av_frame_free(&d3d11Frame);
                m_zeroCopyHealthy = false;
                LOG_WARN("Disable zero-copy path after AVBufferSrcParameters failure");
                locker.unlock();
                return encodeGPU(in);
            }
            par->hw_frames_ctx = d3d11Frame->hw_frames_ctx;
            av_buffersrc_parameters_set(m_bufferSrcCtx, par);
            av_free(par);

            AVFilterContext *hwmapCtx = nullptr;
            AVFilterContext *formatCtx = nullptr;

            const AVFilter *hwmap = avfilter_get_by_name("hwmap");
            const char *deriveDevice = nullptr;
            bool needHwmap = true;
            switch (m_hwaccelInfo->hwDeviceType)
            {
            case AV_HWDEVICE_TYPE_QSV:
                deriveDevice = "derive_device=qsv";
                break;
            case AV_HWDEVICE_TYPE_CUDA:
                deriveDevice = "derive_device=cuda";
                break;
            case AV_HWDEVICE_TYPE_VAAPI:
                deriveDevice = "derive_device=vaapi";
                break;
            case AV_HWDEVICE_TYPE_D3D11VA:
                needHwmap = false;
                break;
            case 12:
                deriveDevice = "derive_device=d3d12va";
                break;
            case 11:
                deriveDevice = "derive_device=vulkan";
                break;
            default:
                needHwmap = false;
                break;
            }

            if (needHwmap)
            {
                ret = avfilter_graph_create_filter(&hwmapCtx, hwmap, "hwmap",
                                                   deriveDevice, nullptr, m_filterGraph);
                if (ret < 0)
                {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(ret, errbuf, sizeof(errbuf));
                    LOG_ERROR("Failed to create hwmap filter: {}", errbuf);
                    av_frame_free(&d3d11Frame);
                    m_zeroCopyHealthy = false;
                    LOG_WARN("Disable zero-copy path after hwmap creation failure");
                    locker.unlock();
                    return encodeGPU(in);
                }
            }

            const char *pixFmts = nullptr;
            switch (m_hwaccelInfo->hwDeviceType)
            {
            case AV_HWDEVICE_TYPE_QSV:
                pixFmts = "pix_fmts=qsv";
                break;
            case AV_HWDEVICE_TYPE_CUDA:
                pixFmts = "pix_fmts=cuda";
                break;
            case AV_HWDEVICE_TYPE_VAAPI:
                pixFmts = "pix_fmts=vaapi";
                break;
            case AV_HWDEVICE_TYPE_D3D11VA:
                pixFmts = "pix_fmts=d3d11";
                break;
            case 12:
                pixFmts = "pix_fmts=d3d12va";
                break;
            case 11:
                pixFmts = "pix_fmts=vulkan";
                break;
            default:
                pixFmts = nullptr;
                break;
            }
            if (pixFmts)
            {
                const AVFilter *format = avfilter_get_by_name("format");
                ret = avfilter_graph_create_filter(&formatCtx, format, "format",
                                                   pixFmts, nullptr, m_filterGraph);
                if (ret < 0)
                {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(ret, errbuf, sizeof(errbuf));
                    LOG_ERROR("Failed to create format filter: {}", errbuf);
                    av_frame_free(&d3d11Frame);
                    m_zeroCopyHealthy = false;
                    LOG_WARN("Disable zero-copy path after format creation failure");
                    locker.unlock();
                    return encodeGPU(in);
                }
            }

            const char *usedScaleName = "none";

            const AVFilter *bufferSink = avfilter_get_by_name("buffersink");
            ret = avfilter_graph_create_filter(&m_bufferSinkCtx, bufferSink, "out",
                                               nullptr, nullptr, m_filterGraph);
            if (ret < 0)
            {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                LOG_ERROR("Failed to create buffer sink filter: {}", errbuf);
                av_frame_free(&d3d11Frame);
                m_zeroCopyHealthy = false;
                LOG_WARN("Disable zero-copy path after buffer sink creation failure");
                locker.unlock();
                return encodeGPU(in);
            }

            AVFilterContext *prevCtx = m_bufferSrcCtx;
            if (hwmapCtx)
            {
                ret = avfilter_link(prevCtx, 0, hwmapCtx, 0);
                if (ret < 0)
                {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(ret, errbuf, sizeof(errbuf));
                    LOG_ERROR("Failed to link buffer->hwmap: {}", errbuf);
                    av_frame_free(&d3d11Frame);
                    m_zeroCopyHealthy = false;
                    LOG_WARN("Disable zero-copy path after filter link failure");
                    locker.unlock();
                    return encodeGPU(in);
                }
                prevCtx = hwmapCtx;
            }
            if (formatCtx)
            {
                ret = avfilter_link(prevCtx, 0, formatCtx, 0);
                if (ret < 0)
                {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(ret, errbuf, sizeof(errbuf));
                    LOG_ERROR("Failed to link to format: {}", errbuf);
                    av_frame_free(&d3d11Frame);
                    m_zeroCopyHealthy = false;
                    LOG_WARN("Disable zero-copy path after filter link failure");
                    locker.unlock();
                    return encodeGPU(in);
                }
                prevCtx = formatCtx;
            }
            ret = avfilter_link(prevCtx, 0, m_bufferSinkCtx, 0);

            if (ret < 0)
            {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                LOG_ERROR("Failed to link filters: {}", errbuf);
                av_frame_free(&d3d11Frame);
                m_zeroCopyHealthy = false;
                LOG_WARN("Disable zero-copy path after filter link failure");
                locker.unlock();
                return encodeGPU(in);
            }

            ret = avfilter_graph_config(m_filterGraph, nullptr);
            if (ret < 0)
            {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                LOG_ERROR("Failed to configure filter graph: {}", errbuf);
                av_frame_free(&d3d11Frame);
                m_zeroCopyHealthy = false;
                LOG_WARN("Disable zero-copy path after graph config failure");
                locker.unlock();
                return encodeGPU(in);
            }

            m_filterSrcW = static_cast<int>(desc.Width);
            m_filterSrcH = static_cast<int>(desc.Height);
            m_filterNeedScale = false;
            if (m_filterFramesCtx)
                av_buffer_unref(&m_filterFramesCtx);
            m_filterFramesCtx = av_buffer_ref(d3d11Frame->hw_frames_ctx);

            if (m_hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_QSV && !m_qsvFramesBound)
            {
                AVBufferRef *sinkFramesCtx = av_buffersink_get_hw_frames_ctx(m_bufferSinkCtx);
                if (sinkFramesCtx)
                {
                    if (!reinitializeQsvCodecWithGraphFrames(sinkFramesCtx))
                    {
                        m_zeroCopyHealthy = false;
                        av_frame_free(&d3d11Frame);
                        locker.unlock();
                        return encodeGPU(in);
                    }
                }
                else
                {
                    LOG_WARN("QSV graph configured but buffersink has no hw_frames_ctx yet; will try after first output frame");
                }
            }

            LOG_INFO("Initialized zero-copy graph: mode={}, hw={}, scale={}",
                     "passthrough",
                     m_hwaccelInfo->hwDeviceTypeName,
                     usedScaleName);
        }

        d3d11Frame->pts = m_pts;
        ret = av_buffersrc_add_frame_flags(m_bufferSrcCtx, d3d11Frame, 0);
        if (ret < 0)
        {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG_ERROR("Failed to add frame to buffer source: {}", errbuf);

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
            m_zeroCopyHealthy = false;
            LOG_WARN("Disable zero-copy path after buffersrc add_frame failure");

            av_frame_free(&d3d11Frame);
            locker.unlock();
            return encodeGPU(in);
        }

        qsvFrame = av_frame_alloc();
        if (!qsvFrame)
        {
            av_frame_free(&d3d11Frame);
            m_zeroCopyHealthy = false;
            LOG_WARN("Disable zero-copy path after frame allocation failure");
            locker.unlock();
            return encodeGPU(in);
        }

        ret = av_buffersink_get_frame(m_bufferSinkCtx, qsvFrame);
        if (ret < 0)
        {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG_ERROR("Failed to get frame from buffer sink: {}", errbuf);
            av_frame_free(&qsvFrame);
            av_frame_free(&d3d11Frame);
            m_zeroCopyHealthy = false;
            LOG_WARN("Disable zero-copy path after buffersink failure");
            locker.unlock();
            return encodeGPU(in);
        }

        if (m_hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_QSV &&
            !m_qsvFramesBound && qsvFrame->hw_frames_ctx &&
            (!m_codecContext->hw_frames_ctx || m_codecContext->hw_frames_ctx->data != qsvFrame->hw_frames_ctx->data))
        {
            // 保存旧的 frames_ctx 指针用于重建尝试
            AVBufferRef *oldFramesCtx = av_buffer_ref(qsvFrame->hw_frames_ctx);
            // 释放当前从 buffersink 取到的帧（属于旧的 frames_ctx）
            av_frame_free(&qsvFrame);

            if (!reinitializeQsvCodecWithGraphFrames(oldFramesCtx))
            {
                av_buffer_unref(&oldFramesCtx);
                m_zeroCopyHealthy = false;
                av_frame_free(&d3d11Frame);
                locker.unlock();
                return encodeGPU(in);
            }

            // reinitialize 成功，尝试把触发帧重新 push 进 filter graph 并获取来自新会话的输出帧
            av_buffer_unref(&oldFramesCtx);

            // 重新 push 原始 d3d11Frame 到 buffersrc
            d3d11Frame->pts = m_pts; // 保持 pts
            ret = av_buffersrc_add_frame_flags(m_bufferSrcCtx, d3d11Frame, 0);
            if (ret < 0)
            {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                LOG_ERROR("After reinit: failed to re-add frame to buffer source: {}", errbuf);
                m_zeroCopyHealthy = false;
                av_frame_free(&d3d11Frame);
                locker.unlock();
                return encodeGPU(in);
            }

            // 获取新会话下的输出帧
            qsvFrame = av_frame_alloc();
            if (!qsvFrame)
            {
                LOG_ERROR("After reinit: failed to allocate qsvFrame");
                m_zeroCopyHealthy = false;
                av_frame_free(&d3d11Frame);
                locker.unlock();
                return encodeGPU(in);
            }

            ret = av_buffersink_get_frame(m_bufferSinkCtx, qsvFrame);
            if (ret < 0)
            {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                LOG_ERROR("After reinit: failed to get frame from buffer sink: {}", errbuf);
                av_frame_free(&qsvFrame);
                av_frame_free(&d3d11Frame);
                m_zeroCopyHealthy = false;
                LOG_WARN("Disable zero-copy path after buffersink failure (post-reinit)");
                locker.unlock();
                return encodeGPU(in);
            }
        }
    }

    AVFrame *encodeFrame = qsvFrame;
    AVFrame *compatFrame = nullptr;
    if (m_codecContext->hw_frames_ctx && qsvFrame && qsvFrame->hw_frames_ctx &&
        m_codecContext->hw_frames_ctx->data != qsvFrame->hw_frames_ctx->data)
    {
        if (m_hwaccelInfo && m_hwaccelInfo->hwDeviceType == AV_HWDEVICE_TYPE_D3D11VA)
        {
            LOG_WARN("zero-copy: d3d11 frame hw_frames_ctx is incompatible with encoder context, disable zero-copy and fallback");
            m_zeroCopyHealthy = false;
            if (qsvFrame != d3d11Frame)
                av_frame_free(&qsvFrame);
            av_frame_free(&d3d11Frame);
            locker.unlock();
            return encodeGPU(in);
        }

        compatFrame = av_frame_alloc();
        if (compatFrame)
        {
            int getRet = av_hwframe_get_buffer(m_codecContext->hw_frames_ctx, compatFrame, 0);
            if (getRet >= 0)
            {
                int txRet = av_hwframe_transfer_data(compatFrame, qsvFrame, 0);
                if (txRet >= 0)
                {
                    encodeFrame = compatFrame;
                    LOG_TRACE("zero-copy: transferred hw frame to encoder hw_frames_ctx for session compatibility");
                }
                else
                {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(txRet, errbuf, sizeof(errbuf));
                    LOG_WARN("zero-copy: hwframe transfer to encoder context failed: {}, use original frame", errbuf);
                    av_frame_free(&compatFrame);
                }
            }
            else
            {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(getRet, errbuf, sizeof(errbuf));
                LOG_WARN("zero-copy: alloc frame on encoder hw_frames_ctx failed: {}, use original frame", errbuf);
                av_frame_free(&compatFrame);
            }
        }
    }

    LOG_TRACE("zero-copy send: frame_fmt={}, frame_hwctx=0x{:x}, codec_hwctx=0x{:x}",
              av_get_pix_fmt_name(static_cast<AVPixelFormat>(encodeFrame->format)),
              static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(encodeFrame->hw_frames_ctx ? encodeFrame->hw_frames_ctx->data : nullptr)),
              static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(m_codecContext->hw_frames_ctx ? m_codecContext->hw_frames_ctx->data : nullptr)));

    encodeFrame->pts = m_pts++;
    ret = avcodec_send_frame(m_codecContext, encodeFrame);
    if (ret == AVERROR(EAGAIN))
    {
        // 编码器内部队列满：先排空一次旧包，再重试发送当前帧
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
                timestamp90k = av_rescale_q(pts, m_codecContext->time_base, AVRational{1, 90000});
            }
            av_packet_unref(m_packet);
        }
        ret = avcodec_send_frame(m_codecContext, encodeFrame);
    }

    if (compatFrame)
        av_frame_free(&compatFrame);
    if (qsvFrame != d3d11Frame)
        av_frame_free(&qsvFrame);
    av_frame_free(&d3d11Frame);

    if (ret < 0)
    {
        if (ret == AVERROR(EAGAIN))
        {
            LOG_TRACE("Encoder still back-pressured after retry in zero-copy path, skip this frame");
            return {out, timestamp90k};
        }
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR("Error sending frame to encoder (zero-copy): {}", errbuf);
        return {out, timestamp90k};
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
            LOG_ERROR("Error receiving packet from encoder (zero-copy): {}", errbuf);
            break;
        }
        if (m_packet->size > 0)
        {
            size_t old = out->size();
            out->resize(old + m_packet->size);
            memcpy(out->data() + old, m_packet->data, m_packet->size);
            int64_t pts = m_packet->pts;
            timestamp90k = av_rescale_q(pts, m_codecContext->time_base, AVRational{1, 90000});
        }
        av_packet_unref(m_packet);
    }

    return {out, timestamp90k};
}
#endif
