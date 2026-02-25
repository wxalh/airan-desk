#ifndef DESKTOP_GRAB_DXGI_H
#define DESKTOP_GRAB_DXGI_H
#include "../common/constant.h"
// 仅在 Windows 平台编译此文件
#if !defined(AIRAN_COMPATIBLE_WIN7) && ( defined(Q_OS_WIN64) || defined(Q_OS_WIN32) )

#include <cstdint>
#include <vector>
#include <memory>
#include "desktop_grab.h"

class DesktopGrabDXGI : public DesktopGrab
{
    Q_OBJECT
public:
    struct Impl;

    struct Frame
    {
        int width = 0;
        int height = 0;
        int linesize = 0;          // bytes per row (RowPitch from D3D)
        std::vector<uint8_t> data; // contiguous buffer with linesize*height bytes (raw mapped rows)
        int64_t pts_us = 0;        // timestamp in microseconds (steady clock)
    };

    explicit DesktopGrabDXGI();

    ~DesktopGrabDXGI();
    /**
     * @brief Find the DXGI output index corresponding to the given QScreen index
     * @param qscreenIndex The QScreen index
     * @return The DXGI output index, or 0 if not found
     */
    int findDxgiOutputIndexForQScreen(int qscreenIndex);
    /**
     * @brief Initialize the DXGI desktop grabber for the given QScreen index
     * @param qscreenIndex The QScreen index
     * @return True on success, false on failure
     */
    bool init(int qscreenIndex) override;

    HRESULT create_device_and_duplication(Impl &s);

    bool grabFrameCPU(QImage &outImage) override { return false; }

    bool grabFrameGPU(ID3D11Texture2D *&outTexture) override;

    void releaseLastFrame(ID3D11Texture2D *&tex);

private:
    std::unique_ptr<Impl> impl_;
};

#endif // Q_OS_WIN64 || Q_OS_WIN32

#endif // DESKTOP_GRAB_DXGI_H