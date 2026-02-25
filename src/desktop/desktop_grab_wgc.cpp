#include "desktop_grab_wgc.h"
#include "../common/constant.h"
#if !defined(AIRAN_COMPATIBLE_WIN7) && ( defined(Q_OS_WIN64) || defined(Q_OS_WIN32) )

#include <vector>
#include <atomic>

#include <QGuiApplication>
#include <QScreen>

#include <d3d11_4.h>
#include <dxgi.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

using GraphicsCaptureItem = ::winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using GraphicsCaptureSession = ::winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using Direct3D11CaptureFramePool = ::winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using DirectXPixelFormat = ::winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using IDirect3DDevice = ::winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
using SizeInt32 = ::winrt::Windows::Graphics::SizeInt32;

namespace {

struct MonitorSelectCtx
{
    RECT target{};
    HMONITOR best{nullptr};
    int bestArea{-1};
};

int intersectArea(const RECT &a, const RECT &b)
{
    const int l = std::max(a.left, b.left);
    const int t = std::max(a.top, b.top);
    const int r = std::min(a.right, b.right);
    const int bt = std::min(a.bottom, b.bottom);
    if (r <= l || bt <= t)
    {
        return 0;
    }
    return (r - l) * (bt - t);
}

BOOL CALLBACK monitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam)
{
    auto *ctx = reinterpret_cast<MonitorSelectCtx *>(lParam);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMonitor, &mi))
    {
        return TRUE;
    }
    int area = intersectArea(mi.rcMonitor, ctx->target);
    if (area > ctx->bestArea)
    {
        ctx->bestArea = area;
        ctx->best = hMonitor;
    }
    return TRUE;
}

GraphicsCaptureItem createItemForMonitor(HMONITOR monitor)
{
    GraphicsCaptureItem item{nullptr};
    auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    winrt::check_hresult(interop->CreateForMonitor(
        monitor,
        winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
        winrt::put_abi(item)));
    return item;
}

IDirect3DDevice createWinRTDevice(ID3D11Device *d3d11Device)
{
    ComPtr<IDXGIDevice> dxgiDevice;
    winrt::check_hresult(d3d11Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(dxgiDevice.GetAddressOf())));
    IDirect3DDevice device{nullptr};
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(
        dxgiDevice.Get(),
        reinterpret_cast<::IInspectable **>(winrt::put_abi(device))));
    return device;
}

} // namespace

struct DesktopGrabWGC::Impl
{
    ComPtr<ID3D11Device> d3d11Device;
    ComPtr<ID3D11DeviceContext> d3d11Context;

    IDirect3DDevice winrtDevice{nullptr};
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool framePool{nullptr};
    GraphicsCaptureSession session{nullptr};
    winrt::event_token frameArrivedToken{};
    bool frameArrivedSubscribed{false};
    std::atomic_bool isStopping{false};

    SizeInt32 lastFramePoolSize{0, 0};

    QMutex frameMutex;
    ComPtr<ID3D11Texture2D> latestTexture;
    bool hasNewFrame{false};
    bool initialized{false};
    Qt::HANDLE ownerThreadId{nullptr};
    void shutdownNoThrow(bool closeCaptureObjects);
    ~Impl();
};

void DesktopGrabWGC::Impl::shutdownNoThrow(bool closeCaptureObjects)
{
    isStopping.store(true);

    const auto token = frameArrivedToken;
    const bool hadFrameArrivedSubscription = frameArrivedSubscribed;

    frameArrivedSubscribed = false;
    frameArrivedToken = {};

    const bool sameOwnerThread = (ownerThreadId == nullptr) || (ownerThreadId == QThread::currentThreadId());

    // WinRT capture objects are thread-affine in teardown on some systems.
    // If destruction happens on a foreign thread (common in late app shutdown),
    // avoid releasing them here to prevent crash in C++/WinRT release path.
    if (!sameOwnerThread)
    {
        QMutexLocker frameLocker(&frameMutex);
        latestTexture.Reset();
        hasNewFrame = false;

        if (d3d11Context)
            d3d11Context.Reset();
        if (d3d11Device)
            d3d11Device.Reset();

        // Do not release WinRT objects on a foreign thread; detach ABI pointers
        // to avoid cross-thread Release crash during late app teardown.
        (void)winrt::detach_abi(session);
        (void)winrt::detach_abi(framePool);
        (void)winrt::detach_abi(item);
        (void)winrt::detach_abi(winrtDevice);

        initialized = false;
        return;
    }

    if (hadFrameArrivedSubscription && framePool)
    {
        try
        {
            framePool.FrameArrived(token);
        }
        catch (...) {}
    }

    if (closeCaptureObjects)
    {
        try
        {
            if (session)
            {
                session.Close();
            }
        }
        catch (...) {}

        try
        {
            if (framePool)
            {
                framePool.Close();
            }
        }
        catch (...) {}
    }

    // Avoid explicit IClosable::Close() during late teardown. On some systems this
    // can crash inside C++/WinRT try_as(IClosable). Releasing WinRT references is
    // enough to stop capture in our shutdown flow.

    {
        QMutexLocker frameLocker(&frameMutex);
        latestTexture.Reset();
        hasNewFrame = false;
    }

    // Flush and clear context first so the device/context are in a clean state
    if (d3d11Context)
    {
        d3d11Context->Flush();
        d3d11Context->ClearState();
    }

    // Finally release context and device
    if (d3d11Context)
        d3d11Context.Reset();
    if (d3d11Device)
        d3d11Device.Reset();

    // Release WinRT objects
    session = nullptr;
    framePool = nullptr;
    item = nullptr;
    winrtDevice = nullptr;
    initialized = false;

    // keep isStopping=true after shutdown; init() will re-arm it when starting capture again
}

DesktopGrabWGC::Impl::~Impl()
{
    const bool sameOwnerThread = (ownerThreadId == nullptr) || (ownerThreadId == QThread::currentThreadId());
    if (!sameOwnerThread)
    {
        // shutdownNoThrow may early-return when isStopping is already true; make
        // sure WinRT members are detached before implicit member destruction.
        (void)winrt::detach_abi(session);
        (void)winrt::detach_abi(framePool);
        (void)winrt::detach_abi(item);
        (void)winrt::detach_abi(winrtDevice);
    }
    shutdownNoThrow(false);
}

DesktopGrabWGC::DesktopGrabWGC(QObject *parent)
    : DesktopGrab(parent)
{
    impl_ = std::make_shared<Impl>();
}

DesktopGrabWGC::~DesktopGrabWGC()
{
    if (impl_)
    {
        impl_->shutdownNoThrow(false);
    }
}

void DesktopGrabWGC::stopCapture()
{
    QMutexLocker locker(&m_mutex);
    if (!impl_)
        return;
    impl_->shutdownNoThrow(true);
    LOG_INFO("DesktopGrabWGC stopped capture");
}

bool DesktopGrabWGC::init(int screenIndex)
{
    QMutexLocker locker(&m_mutex);

    if (!impl_)
    {
        return false;
    }

    // Ensure previous/failed sessions are fully cleaned before starting again.
    impl_->shutdownNoThrow(true);
    impl_->isStopping.store(false);

    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    catch (...)
    {
    }

    if (!GraphicsCaptureSession::IsSupported())
    {
        LOG_WARN("DesktopGrabWGC: GraphicsCaptureSession is not supported on this system");
        return false;
    }

    QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
    {
        LOG_ERROR("DesktopGrabWGC: no screens found");
        return false;
    }

    if (screenIndex < 0 || screenIndex >= screens.size())
    {
        screenIndex = 0;
    }

    m_screenIndex = screenIndex;
    QScreen *screen = screens.at(screenIndex);
    QRect g = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
    m_screen_width = g.width();
    m_screen_height = g.height();

    RECT targetRect{g.left(), g.top(), g.right(), g.bottom()};
    MonitorSelectCtx ctx{};
    ctx.target = targetRect;
    EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc, reinterpret_cast<LPARAM>(&ctx));

    HMONITOR monitor = ctx.best;
    if (!monitor)
    {
        POINT center{(targetRect.left + targetRect.right) / 2, (targetRect.top + targetRect.bottom) / 2};
        monitor = MonitorFromPoint(center, MONITOR_DEFAULTTOPRIMARY);
    }
    if (!monitor)
    {
        LOG_ERROR("DesktopGrabWGC: failed to resolve monitor for screenIndex {}", screenIndex);
        return false;
    }

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL outLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        levels,
        ARRAYSIZE(levels),
        D3D11_SDK_VERSION,
        &impl_->d3d11Device,
        &outLevel,
        &impl_->d3d11Context);
    if (FAILED(hr) || !impl_->d3d11Device || !impl_->d3d11Context)
    {
        LOG_ERROR("DesktopGrabWGC: D3D11CreateDevice failed: {:#x}", static_cast<unsigned int>(hr));
        return false;
    }

    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(impl_->d3d11Context.As(&mt)) && mt)
    {
        mt->SetMultithreadProtected(TRUE);
    }

    try
    {
        impl_->winrtDevice = createWinRTDevice(impl_->d3d11Device.Get());
        impl_->item = createItemForMonitor(monitor);
        if (!impl_->item)
        {
            LOG_ERROR("DesktopGrabWGC: failed to create GraphicsCaptureItem");
            return false;
        }

        auto sz = impl_->item.Size();
        impl_->framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            impl_->winrtDevice,
            DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            sz);
        impl_->lastFramePoolSize = sz;
        impl_->session = impl_->framePool.CreateCaptureSession(impl_->item);

        try
        {
            impl_->session.IsBorderRequired(false);
            LOG_INFO("DesktopGrabWGC: requested to disable yellow border");
        }
        catch (...)
        {
            LOG_INFO("DesktopGrabWGC: border disable is not supported on current OS/runtime");
        }

        impl_->isStopping.store(false);
        impl_->ownerThreadId = QThread::currentThreadId();
        std::weak_ptr<Impl> weakImpl = impl_;
        impl_->frameArrivedToken = impl_->framePool.FrameArrived(
            [weakImpl](Direct3D11CaptureFramePool const &sender, ::winrt::Windows::Foundation::IInspectable const &) {
                auto impl = weakImpl.lock();
                if (!impl || impl->isStopping.load())
                {
                    return;
                }

                try
                {
                    auto frame = sender.TryGetNextFrame();
                    if (!frame)
                    {
                        return;
                    }

                    auto contentSize = frame.ContentSize();
                    if (contentSize.Width > 0 && contentSize.Height > 0)
                    {
                        if (contentSize.Width != impl->lastFramePoolSize.Width ||
                            contentSize.Height != impl->lastFramePoolSize.Height)
                        {
                            sender.Recreate(
                                impl->winrtDevice,
                                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                2,
                                contentSize);
                            impl->lastFramePoolSize = contentSize;

                            QMutexLocker frameLocker(&impl->frameMutex);
                            impl->latestTexture.Reset();
                            impl->hasNewFrame = false;
                        }
                    }

                    auto surface = frame.Surface();
                    auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();

                    ComPtr<ID3D11Texture2D> srcTexture;
                    winrt::check_hresult(winrt::get_abi(access)->GetInterface(
                        __uuidof(ID3D11Texture2D),
                        reinterpret_cast<void **>(srcTexture.GetAddressOf())));
                    if (!srcTexture)
                    {
                        return;
                    }

                    D3D11_TEXTURE2D_DESC srcDesc{};
                    srcTexture->GetDesc(&srcDesc);

                    {
                        QMutexLocker frameLocker(&impl->frameMutex);

                        bool needRecreate = !impl->latestTexture;
                        if (!needRecreate)
                        {
                            D3D11_TEXTURE2D_DESC curDesc{};
                            impl->latestTexture->GetDesc(&curDesc);
                            needRecreate = (curDesc.Width != srcDesc.Width ||
                                            curDesc.Height != srcDesc.Height ||
                                            curDesc.Format != srcDesc.Format);
                        }

                        if (needRecreate)
                        {
                            D3D11_TEXTURE2D_DESC copyDesc = srcDesc;
                            copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                            copyDesc.MiscFlags = 0;
                            copyDesc.CPUAccessFlags = 0;
                            copyDesc.Usage = D3D11_USAGE_DEFAULT;
                            copyDesc.ArraySize = 1;
                            copyDesc.MipLevels = 1;
                            copyDesc.SampleDesc.Count = 1;
                            impl->latestTexture.Reset();
                            HRESULT chr = impl->d3d11Device->CreateTexture2D(&copyDesc, nullptr, &impl->latestTexture);
                            if (FAILED(chr) || !impl->latestTexture)
                            {
                                return;
                            }
                        }

                        impl->d3d11Context->CopyResource(impl->latestTexture.Get(), srcTexture.Get());
                        impl->hasNewFrame = true;
                    }
                }
                catch (...)
                {
                }
            });
        impl_->frameArrivedSubscribed = true;

        impl_->session.StartCapture();
        impl_->initialized = true;
        LOG_INFO("DesktopGrabWGC initialized for screenIndex {}", screenIndex);
        return true;
    }
    catch (const winrt::hresult_error &e)
    {
        LOG_ERROR("DesktopGrabWGC init failed: {:#x}", static_cast<unsigned int>(e.code()));
        impl_->shutdownNoThrow(true);
        return false;
    }
    catch (...)
    {
        LOG_ERROR("DesktopGrabWGC init failed: unknown exception");
        impl_->shutdownNoThrow(true);
        return false;
    }
}

bool DesktopGrabWGC::grabFrameGPU(ID3D11Texture2D *&outTexture)
{
    outTexture = nullptr;
    if (!impl_ || !impl_->initialized)
    {
        return false;
    }

    QMutexLocker locker(&impl_->frameMutex);
    if (!impl_->latestTexture || !impl_->hasNewFrame)
    {
        return true;
    }

    outTexture = impl_->latestTexture.Get();
    outTexture->AddRef();
    impl_->hasNewFrame = false;
    return true;
}

void DesktopGrabWGC::releaseLastFrame(ID3D11Texture2D *&tex)
{
    if (!tex)
    {
        return;
    }
    tex->Release();
    tex = nullptr;
}

#endif // Q_OS_WIN64 || Q_OS_WIN32
