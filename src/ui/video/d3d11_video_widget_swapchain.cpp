#include "d3d11_video_widget.h"

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)

#include <algorithm>

using Microsoft::WRL::ComPtr;


bool D3D11VideoWidget::ensureSwapChain()
{
    if (!m_device)
        return false;

    const QSize targetSize((std::max)(1, width()), (std::max)(1, height()));
    if (m_swapChain && m_renderTargetView && m_swapChainSize == targetSize)
        return true;

    releaseSwapChain();

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory> factory;
    if (FAILED(m_device.As(&dxgiDevice)) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
    {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferDesc.Width = static_cast<UINT>(targetSize.width());
    desc.BufferDesc.Height = static_cast<UINT>(targetSize.height());
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = reinterpret_cast<HWND>(winId());
    desc.SampleDesc.Count = 1;
    desc.BufferCount = 2;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    if (FAILED(factory->CreateSwapChain(m_device.Get(), &desc, &m_swapChain)) || !m_swapChain)
        return false;

    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) ||
        FAILED(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTargetView)))
    {
        releaseSwapChain();
        return false;
    }

    m_swapChainSize = targetSize;
    return true;
}


void D3D11VideoWidget::releaseSwapChain()
{
    if (m_context)
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_renderTargetView.Reset();
    m_swapChain.Reset();
    m_swapChainSize = QSize();
}

#endif
