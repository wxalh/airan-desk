#pragma once

#include <cstdint>

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#endif

namespace rtc
{

struct D3D11VideoFrame
{
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    uint32_t subresource{0};
    int width{0};
    int height{0};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
#else
    void *device{nullptr};
    void *texture{nullptr};
    uint32_t subresource{0};
    int width{0};
    int height{0};
    int format{0};
#endif
    int64_t timestampUs{0};

    bool isValid() const
    {
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
        return device && texture && width > 0 && height > 0 && format != DXGI_FORMAT_UNKNOWN;
#else
        return false;
#endif
    }
};

} // namespace rtc
