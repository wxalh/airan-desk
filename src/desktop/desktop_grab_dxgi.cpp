#include "desktop_grab_dxgi.h"
#if !defined(AIRAN_COMPATIBLE_WIN7) && ( defined(Q_OS_WIN64) || defined(Q_OS_WIN32) )
#include <iostream>
#include <QGuiApplication>
#include <QScreen>
#include <QRect>

struct DesktopGrabDXGI::Impl
{
    Impl(int idx) : output_index(idx) {}

    int output_index = 0;

    // D3D objects (WRL ComPtr for RAII)
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;

    // staging texture for CPU read. We keep staging with same format as source so we can map raw bytes.
    ComPtr<ID3D11Texture2D> staging;

    int src_w = 0;
    int src_h = 0;
};

// Create device + duplication (tries DuplicateOutput1 if available, falls back to DuplicateOutput)
HRESULT DesktopGrabDXGI::create_device_and_duplication(Impl &s)
{
    HRESULT hr = S_OK;

    D3D_DRIVER_TYPE driverTypes[] = {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE};
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_1};

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL chosenFL = D3D_FEATURE_LEVEL_11_0;

    for (auto dt : driverTypes)
    {
        hr = D3D11CreateDevice(nullptr, dt, nullptr, 0,
                               featureLevels, ARRAYSIZE(featureLevels),
                               D3D11_SDK_VERSION, &device, &chosenFL, &context);
        if (SUCCEEDED(hr))
            break;
    }
    if (FAILED(hr))
        return hr;

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = device->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgiDevice);
    if (FAILED(hr))
        return hr;

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void **)&adapter);
    if (FAILED(hr))
        return hr;

    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(s.output_index, &output);
    if (FAILED(hr))
        return hr;

    // Prefer DuplicateOutput1 (IDXGIOutput5) when available
    bool used_dup1 = false;
    ComPtr<IDXGIOutput5> output5;
    hr = output->QueryInterface(__uuidof(IDXGIOutput5), (void **)&output5);
    if (SUCCEEDED(hr) && output5)
    {
        DXGI_FORMAT formats[] = {
            // DXGI_FORMAT_R16G16B16A16_FLOAT,
            // DXGI_FORMAT_R10G10B10A2_UNORM,
            DXGI_FORMAT_B8G8R8A8_UNORM};
        UINT nb_formats = ARRAYSIZE(formats);
        hr = output5->DuplicateOutput1(device.Get(), 0, nb_formats, formats, &s.duplication);
        if (SUCCEEDED(hr))
        {
            used_dup1 = true;
        }
        else
        {
            s.duplication.Reset();
        }
    }

    if (!used_dup1)
    {
        ComPtr<IDXGIOutput1> output1;
        hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void **)&output1);
        if (FAILED(hr))
            return hr;
        hr = output1->DuplicateOutput(device.Get(), &s.duplication);
        if (FAILED(hr))
            return hr;
    }

    s.device = device;
    s.context = context;
    return S_OK;
}

DesktopGrabDXGI::DesktopGrabDXGI()
    : impl_(new Impl(0))
{
}

DesktopGrabDXGI::~DesktopGrabDXGI()
{
    // cleanup DXGI resources
    if (!impl_)
        return;
    Impl &s = *impl_;
    if (s.duplication)
        s.duplication.Reset();
    if (s.staging)
        s.staging.Reset();
    if (s.context)
        s.context.Reset();
    if (s.device)
        s.device.Reset();
    // DO NOT CoUninitialize here to avoid mismatches if caller manages COM lifecycle.
    impl_.reset();
}
// 根据 QScreen 获取对应的 DXGI output index
int DesktopGrabDXGI::findDxgiOutputIndexForQScreen(int qscreenIndex)
{
    auto screens = QGuiApplication::screens();
    if (qscreenIndex < 0 || qscreenIndex >= screens.size())
        return 0;
    QRect qtGeom = screens[qscreenIndex]->geometry();

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory)))
        return 0;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT ai = 0; factory->EnumAdapters1(ai, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++ai)
    {
        ComPtr<IDXGIOutput> output;
        for (UINT oi = 0; adapter->EnumOutputs(oi, &output) != DXGI_ERROR_NOT_FOUND; ++oi)
        {
            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);
            RECT r = desc.DesktopCoordinates;
            QRect dxgiRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
            if (dxgiRect.intersects(qtGeom) || dxgiRect == qtGeom)
            {
                return (int)oi; // 找到匹配的 output index，返回即可（可扩展返回 adapter+output）
            }
            output.Reset();
        }
        adapter.Reset();
    }
    return 0; // 默认 0
}

bool DesktopGrabDXGI::init(int qscreenIndex)
{
    QMutexLocker locker(&m_mutex);
    if (!impl_)
        return false;

    Impl &s = *impl_;
    s.output_index = findDxgiOutputIndexForQScreen(qscreenIndex);
    // Create device + duplication
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        return false;
    }
    hr = create_device_and_duplication(s);
    if (FAILED(hr))
    {
        return false;
    }

    // Probe a frame quickly to get format/size
    IDXGIResource *res = nullptr;
    DXGI_OUTDUPL_FRAME_INFO fi;
    hr = s.duplication->AcquireNextFrame(100, &fi, &res);
    if (SUCCEEDED(hr) && res)
    {
        ID3D11Texture2D *tex = nullptr;
        hr = res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&tex);
        res->Release();
        if (SUCCEEDED(hr) && tex)
        {
            D3D11_TEXTURE2D_DESC desc;
            tex->GetDesc(&desc);
            s.src_w = desc.Width;
            s.src_h = desc.Height;
            m_screen_width = desc.Width;
            m_screen_height = desc.Height;
            tex->Release();
        }
        s.duplication->ReleaseFrame();
    }
    return true;
}

// Ensure staging texture exists and matches source desc (format/size)
static HRESULT ensure_staging_texture(DesktopGrabDXGI::Impl &s, const D3D11_TEXTURE2D_DESC &src_desc, ID3D11Texture2D **out_staging)
{
    if (!s.staging)
    {
        D3D11_TEXTURE2D_DESC sd = {};
        sd.Width = src_desc.Width;
        sd.Height = src_desc.Height;
        sd.MipLevels = 1;
        sd.ArraySize = 1;
        sd.SampleDesc.Count = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.BindFlags = 0;
        sd.MiscFlags = 0;
        HRESULT hr = s.device->CreateTexture2D(&sd, nullptr, &s.staging);
        if (FAILED(hr))
            return hr;
    }
    else
    {
        D3D11_TEXTURE2D_DESC sd;
        s.staging->GetDesc(&sd);
        if (sd.Width != src_desc.Width || sd.Height != src_desc.Height)
        {
            s.staging.Reset();
            D3D11_TEXTURE2D_DESC nsd = {};
            nsd.Width = src_desc.Width;
            nsd.Height = src_desc.Height;
            nsd.MipLevels = 1;
            nsd.ArraySize = 1;
            nsd.SampleDesc.Count = 1;
            nsd.Usage = D3D11_USAGE_STAGING;
            nsd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            nsd.BindFlags = 0;
            nsd.MiscFlags = 0;
            HRESULT hr = s.device->CreateTexture2D(&nsd, nullptr, &s.staging);
            if (FAILED(hr))
                return hr;
        }
    }
    if (out_staging)
        *out_staging = s.staging.Get();
    return S_OK;
}

bool DesktopGrabDXGI::grabFrameGPU(ID3D11Texture2D *&tex)
{
    tex = nullptr;
    if (!impl_)
        return false;
    Impl &s = *impl_;
    if (!s.duplication)
        return false;

    IDXGIResource *desktop_resource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frame_info;
    HRESULT hr = s.duplication->AcquireNextFrame(100, &frame_info, &desktop_resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
    {
        if (desktop_resource)
            desktop_resource->Release();
        // no new desktop update in this interval, not an error
        return true;
    }
    else if (FAILED(hr))
    {
        if (desktop_resource)
            desktop_resource->Release();
        LOG_ERROR("DXGI AcquireNextFrame failed");
        return false;
    }

    hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&tex);
    desktop_resource->Release();
    desktop_resource = nullptr;

    if (FAILED(hr) || !tex)
    {
        if (tex)
            tex->Release();
        s.duplication->ReleaseFrame();
        return false;
    }

    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);

    if (s.src_w == 0 || s.src_h == 0)
    {
        s.src_w = desc.Width;
        s.src_h = desc.Height;
        m_screen_width = desc.Width;
        m_screen_height = desc.Height;
    }
    return true;
}

void DesktopGrabDXGI::releaseLastFrame(ID3D11Texture2D *&tex)
{
    if (!impl_)
        return;
    Impl &s = *impl_;
    if (!s.duplication)
        return;
    s.duplication->ReleaseFrame();
    if (!tex)
        return;
    tex->Release();
    tex = nullptr;
}

#endif // Q_OS_WIN64 || Q_OS_WIN32
