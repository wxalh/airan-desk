#include "d3d11_video_widget.h"

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)

#include "common/logger_manager.h"

#include <algorithm>

using Microsoft::WRL::ComPtr;


bool D3D11VideoWidget::render()
{
    if (!m_hasFrame || !m_frame.isValid())
        return false;
    if (!ensureDeviceResources() || !ensureSwapChain())
    {
        failRenderer("resource initialization failed");
        return false;
    }

    ComPtr<ID3D11ShaderResourceView> view0;
    ComPtr<ID3D11ShaderResourceView> view1;
    if (!createSourceViews(view0, view1))
    {
        failRenderer("source texture view creation failed");
        return false;
    }

    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);
    m_context->ClearRenderTargetView(m_renderTargetView.Get(), clear);

    float viewportX = 0.0f;
    float viewportY = 0.0f;
    float viewportW = static_cast<float>((std::max)(1, width()));
    float viewportH = static_cast<float>((std::max)(1, height()));
    const float widgetAspect = viewportW / viewportH;
    const float frameAspect = static_cast<float>(m_frame.width) / static_cast<float>(m_frame.height);
    if (widgetAspect > frameAspect)
    {
        viewportW = viewportH * frameAspect;
        viewportX = (static_cast<float>(width()) - viewportW) * 0.5f;
    }
    else
    {
        viewportH = viewportW / frameAspect;
        viewportY = (static_cast<float>(height()) - viewportH) * 0.5f;
    }

    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = viewportX;
    viewport.TopLeftY = viewportY;
    viewport.Width = viewportW;
    viewport.Height = viewportH;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    ID3D11ShaderResourceView *views[2] = {view0.Get(), view1.Get()};
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    if (m_frame.format == DXGI_FORMAT_NV12)
    {
        m_context->PSSetShaderResources(0, 2, views);
        m_context->PSSetShader(m_nv12PixelShader.Get(), nullptr, 0);
    }
    else
    {
        m_context->PSSetShaderResources(0, 1, views);
        m_context->PSSetShader(m_bgraPixelShader.Get(), nullptr, 0);
    }
    m_context->Draw(4, 0);

    ID3D11ShaderResourceView *nullViews[2] = {nullptr, nullptr};
    m_context->PSSetShaderResources(0, 2, nullViews);
    m_swapChain->Present(0, 0);
    return true;
}


bool D3D11VideoWidget::createSourceViews(ComPtr<ID3D11ShaderResourceView> &view0,
                                         ComPtr<ID3D11ShaderResourceView> &view1) const
{
    if (!m_device || !m_frame.texture)
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipLevels = 1;

    if (m_frame.format == DXGI_FORMAT_NV12)
    {
        desc.Format = DXGI_FORMAT_R8_UNORM;
        HRESULT hr = m_device->CreateShaderResourceView(m_frame.texture.Get(), &desc, &view0);
        if (FAILED(hr))
        {
            D3D11_TEXTURE2D_DESC textureDesc = {};
            m_frame.texture->GetDesc(&textureDesc);
            LOG_WARN("D3D11 video renderer failed to create NV12 Y view: hr={}, textureFormat={}, bindFlags={}, size={}x{}",
                     static_cast<long>(hr),
                     static_cast<int>(textureDesc.Format),
                     textureDesc.BindFlags,
                     textureDesc.Width,
                     textureDesc.Height);
            return false;
        }
        desc.Format = DXGI_FORMAT_R8G8_UNORM;
        hr = m_device->CreateShaderResourceView(m_frame.texture.Get(), &desc, &view1);
        if (FAILED(hr))
        {
            D3D11_TEXTURE2D_DESC textureDesc = {};
            m_frame.texture->GetDesc(&textureDesc);
            LOG_WARN("D3D11 video renderer failed to create NV12 UV view: hr={}, textureFormat={}, bindFlags={}, size={}x{}",
                     static_cast<long>(hr),
                     static_cast<int>(textureDesc.Format),
                     textureDesc.BindFlags,
                     textureDesc.Width,
                     textureDesc.Height);
            return false;
        }
        return true;
    }

    desc.Format = m_frame.format;
    const HRESULT hr = m_device->CreateShaderResourceView(m_frame.texture.Get(), &desc, &view0);
    if (FAILED(hr))
    {
        D3D11_TEXTURE2D_DESC textureDesc = {};
        m_frame.texture->GetDesc(&textureDesc);
        LOG_WARN("D3D11 video renderer failed to create texture view: hr={}, frameFormat={}, textureFormat={}, bindFlags={}, size={}x{}",
                 static_cast<long>(hr),
                 static_cast<int>(m_frame.format),
                 static_cast<int>(textureDesc.Format),
                 textureDesc.BindFlags,
                 textureDesc.Width,
                 textureDesc.Height);
        return false;
    }
    return true;
}

#endif
