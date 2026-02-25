#ifndef DESKTOP_GRAB_H
#define DESKTOP_GRAB_H

#include <QObject>
#include <QMutex>
#include <chrono>
#include "../common/constant.h"

class DesktopGrab : public QObject
{
    Q_OBJECT
public:
    static std::shared_ptr<DesktopGrab> createBestDesktopGrabber(int screenIndex, QObject *parent = nullptr);

    explicit DesktopGrab(QObject *parent = nullptr)
        : QObject(parent) {}
    ~DesktopGrab() {}
    virtual bool init(int screenIndex) = 0;
    /**
     * Synchronously grab one frame into CPU QImage (if implementation supports CPU output).
     */
    virtual bool grabFrameCPU(QImage &outImage) = 0;

    /**
     * Stop capture activity and release any active session/framepool.
     * Safe to call multiple times.
     */
    virtual void stopCapture() {}

#if !defined(AIRAN_COMPATIBLE_WIN7) && ( defined(Q_OS_WIN64) || defined(Q_OS_WIN32) )
    /**
     * Synchronously grab one frame into GPU texture (if implementation supports GPU output).
     */
    virtual bool grabFrameGPU(ID3D11Texture2D *&outTexture) = 0;
    /**
     * Release the last grabbed GPU frame.
     */
    virtual void releaseLastFrame(ID3D11Texture2D *&tex) = 0;
#endif

protected:
    QMutex m_mutex;
    int m_screenIndex{0};
    int m_screen_width{0};
    int m_screen_height{0};
};

#endif // DESKTOP_GRAB_H