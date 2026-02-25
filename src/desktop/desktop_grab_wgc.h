#ifndef DESKTOP_GRAB_WGC_H
#define DESKTOP_GRAB_WGC_H

#include "desktop_grab.h"

#if !defined(AIRAN_COMPATIBLE_WIN7) && ( defined(Q_OS_WIN64) || defined(Q_OS_WIN32) )

#include <memory>

class DesktopGrabWGC : public DesktopGrab
{
    Q_OBJECT
public:
    explicit DesktopGrabWGC(QObject *parent = nullptr);
    ~DesktopGrabWGC();

    bool init(int screenIndex) override;

    bool grabFrameCPU(QImage &outImage) override { Q_UNUSED(outImage); return false; }

    bool grabFrameGPU(ID3D11Texture2D *&outTexture) override;

    void releaseLastFrame(ID3D11Texture2D *&tex) override;
    void stopCapture() override;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    // GPU conversion helper: whether to produce NV12 output texture
    bool m_forceNv12Output{true};
};

#endif // Q_OS_WIN64 || Q_OS_WIN32

#endif // DESKTOP_GRAB_WGC_H
