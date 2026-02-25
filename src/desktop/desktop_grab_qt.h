#ifndef DESKTOP_GRAB_QT_H
#define DESKTOP_GRAB_QT_H

#include "desktop_grab.h"
#include <QScreen>

class DesktopGrabQt : public DesktopGrab
{
    Q_OBJECT
public:
    explicit DesktopGrabQt(QObject *parent = nullptr);
    ~DesktopGrabQt();

    bool init(int screenIndex) override;

    bool grabFrameCPU(QImage &outImage) override;

#if !defined(AIRAN_COMPATIBLE_WIN7) && ( defined(Q_OS_WIN64) || defined(Q_OS_WIN32) )
    bool grabFrameGPU(ID3D11Texture2D *&outTexture) override { return false; }
    void releaseLastFrame(ID3D11Texture2D *&tex) override { tex = nullptr; }
#endif

private:
    QScreen *m_screen{nullptr};
};

#endif // DESKTOP_GRAB_QT_H