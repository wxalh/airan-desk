/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "desktop_capture/win/d3d_device.h"

#include <utility>
#include <vector>

#include "desktop_capture/win/desktop_capture_utils.h"
#include "rtc_base/logging.h"
#include "common/logger_manager.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

using Microsoft::WRL::ComPtr;

D3dDevice::D3dDevice() = default;
D3dDevice::D3dDevice(const D3dDevice& other) = default;
D3dDevice::D3dDevice(D3dDevice&& other) = default;
D3dDevice::~D3dDevice() = default;

bool D3dDevice::Initialize(const ComPtr<IDXGIAdapter>& adapter) {
  dxgi_adapter_ = adapter;
  if (!dxgi_adapter_) {
    LOG_WARN("An empty IDXGIAdapter instance has been received.");
    return false;
  }

  D3D_FEATURE_LEVEL feature_level;
  // Default feature levels contain D3D 9.1 through D3D 11.0.
  _com_error error = D3D11CreateDevice(
      adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT,
      nullptr, 0, D3D11_SDK_VERSION, d3d_device_.GetAddressOf(), &feature_level,
      context_.GetAddressOf());
  if (error.Error() != S_OK || !d3d_device_ || !context_) {
    LOG_WARN("D3D11CreateDevice returned: {}", desktop_capture::utils::ComErrorToString(error));
    return false;
  }

  if (feature_level < D3D_FEATURE_LEVEL_11_0) {
    LOG_WARN("D3D11CreateDevice returned an instance without DirectX 11 support, level {}. Following initialization may fail.",
             static_cast<int>(feature_level));
    // D3D_FEATURE_LEVEL_11_0 is not officially documented on MSDN to be a
    // requirement of Dxgi duplicator APIs.
  }

  ComPtr<ID3D10Multithread> multithread;
  error = d3d_device_.As(&multithread);
  if (error.Error() == S_OK && multithread) {
    multithread->SetMultithreadProtected(TRUE);
  } else {
    LOG_WARN("ID3D10Multithread is unavailable: {}", desktop_capture::utils::ComErrorToString(error));
  }

  error = d3d_device_.As(&dxgi_device_);
  if (error.Error() != S_OK || !dxgi_device_) {
    LOG_WARN("ID3D11Device is not an implementation of IDXGIDevice, this usually means the system does not support DirectX 11. Error received: {}",
             desktop_capture::utils::ComErrorToString(error));
    return false;
  }

  return true;
}

// static
std::vector<D3dDevice> D3dDevice::EnumDevices() {
  ComPtr<IDXGIFactory1> factory;
  _com_error error =
      CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                         reinterpret_cast<void**>(factory.GetAddressOf()));
  if (error.Error() != S_OK || !factory) {
    LOG_WARN("Cannot create IDXGIFactory1: {}", desktop_capture::utils::ComErrorToString(error));
    return std::vector<D3dDevice>();
  }

  std::vector<D3dDevice> result;
  for (int i = 0;; i++) {
    ComPtr<IDXGIAdapter> adapter;
    error = factory->EnumAdapters(i, adapter.GetAddressOf());
    if (error.Error() == S_OK) {
      D3dDevice device;
      if (device.Initialize(adapter)) {
        result.push_back(std::move(device));
      }
    } else if (error.Error() == DXGI_ERROR_NOT_FOUND) {
      break;
    } else {
      LOG_WARN("IDXGIFactory1::EnumAdapters returned an unexpected error: {}",
               desktop_capture::utils::ComErrorToString(error));
    }
  }
  return result;
}

}  // namespace airan::desktop_capture
