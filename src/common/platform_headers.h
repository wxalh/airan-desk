#ifndef PLATFORM_HEADERS_H
#define PLATFORM_HEADERS_H

#include <QtGlobal>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <windows.h>
#if defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
#ifndef MY_D3D11_INCLUDE
#define MY_D3D11_INCLUDE
#include <d3d11.h>
#endif
#include <dxgi1_6.h>
#include <wrl.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif
#else
#include <dlfcn.h>
#endif

#endif /* PLATFORM_HEADERS_H */
