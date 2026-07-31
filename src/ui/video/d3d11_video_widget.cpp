#include "d3d11_video_widget.h"

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)

#include "common/logger_manager.h"
#include "rtc/core/native_d3d11_video_frame_buffer.h"

#include <QResizeEvent>
#include <QShowEvent>


D3D11VideoWidget::D3D11VideoWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
}

D3D11VideoWidget::~D3D11VideoWidget()
{
    releaseSwapChain();
}


bool D3D11VideoWidget::setFrame(const rtc::D3D11VideoFrame &frame)
{
    if (m_failed || !frame.isValid())
        return false;

    if (m_device.Get() != frame.device.Get())
    {
        releaseSwapChain();
        m_device = frame.device;
        m_device->GetImmediateContext(&m_context);
        rtc::enableD3D11MultithreadProtection(m_device.Get());
        m_vertexShader.Reset();
        m_nv12PixelShader.Reset();
        m_bgraPixelShader.Reset();
        m_sampler.Reset();
    }

    m_frame = frame;
    m_hasFrame = true;
    return render();
}


void D3D11VideoWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_hasFrame)
        render();
}


void D3D11VideoWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_hasFrame)
        render();
}


bool D3D11VideoWidget::ensureDeviceResources()
{
    if (!m_device || !m_context)
        return false;
    return ensureShaders();
}


void D3D11VideoWidget::failRenderer(const char *reason)
{
    if (m_failed)
        return;
    m_failed = true;
    LOG_WARN("D3D11 video renderer failed: {}", reason ? reason : "unknown");
}

#endif
