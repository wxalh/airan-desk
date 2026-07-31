#ifndef AIRAN_D3D11_VIDEO_WIDGET_H
#define AIRAN_D3D11_VIDEO_WIDGET_H

#include "rtc/core/d3d11_video_frame.h"

#include <QWidget>

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

class D3D11VideoWidget final : public QWidget
{
public:
    explicit D3D11VideoWidget(QWidget *parent = nullptr);
    ~D3D11VideoWidget() override;

    bool setFrame(const rtc::D3D11VideoFrame &frame);
    bool hasFrame() const { return m_hasFrame; }
    bool isFailed() const { return m_failed; }
    QSize sourceSize() const { return QSize(m_frame.width, m_frame.height); }

protected:
    QPaintEngine *paintEngine() const override { return nullptr; }
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    bool ensureDeviceResources();
    bool ensureSwapChain();
    bool ensureShaders();
    bool render();
    bool createSourceViews(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &view0,
                           Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &view1) const;
    void releaseSwapChain();
    void failRenderer(const char *reason);

    rtc::D3D11VideoFrame m_frame;
    bool m_hasFrame{false};
    bool m_failed{false};
    QSize m_swapChainSize;

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_nv12PixelShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_bgraPixelShader;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
};
#endif

#endif // AIRAN_D3D11_VIDEO_WIDGET_H
