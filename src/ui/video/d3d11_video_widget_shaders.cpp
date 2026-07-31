#include "d3d11_video_widget.h"

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)

#include "common/logger_manager.h"

#include <d3dcompiler.h>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace
{
const char *kVertexShader =
    "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };"
    "VSOut main(uint id : SV_VertexID) {"
    "  float2 p[4] = { float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, -1) };"
    "  float2 t[4] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1) };"
    "  VSOut o; o.pos = float4(p[id], 0, 1); o.uv = t[id]; return o;"
    "}";

const char *kNv12PixelShader =
    "Texture2D texY : register(t0);"
    "Texture2D texUV : register(t1);"
    "SamplerState samp0 : register(s0);"
    "float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {"
    "  float y = saturate((texY.Sample(samp0, uv).r * 255.0 - 16.0) / 219.0);"
    "  float2 uvv = (texUV.Sample(samp0, uv).rg * 255.0 - float2(128.0, 128.0)) / 224.0;"
    "  float3 rgb;"
    "  rgb.r = y + 1.5748 * uvv.y;"
    "  rgb.g = y - 0.187324 * uvv.x - 0.468124 * uvv.y;"
    "  rgb.b = y + 1.8556 * uvv.x;"
    "  return float4(saturate(rgb), 1);"
    "}";

const char *kBgraPixelShader =
    "Texture2D tex0 : register(t0);"
    "SamplerState samp0 : register(s0);"
    "float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {"
    "  return tex0.Sample(samp0, uv);"
    "}";


bool compileShader(const char *source, const char *profile, ID3DBlob **blob)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG;
#endif
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                                  "main", profile, flags, 0, blob, &errors);
    if (FAILED(hr) && errors)
        LOG_WARN("D3D11 shader compile failed: {}", static_cast<const char *>(errors->GetBufferPointer()));
    return SUCCEEDED(hr);
}
} /* namespace */


bool D3D11VideoWidget::ensureShaders()
{
    if (m_vertexShader && m_nv12PixelShader && m_bgraPixelShader && m_sampler)
        return true;

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> nv12Blob;
    ComPtr<ID3DBlob> bgraBlob;
    if (!compileShader(kVertexShader, "vs_4_0", &vsBlob) ||
        !compileShader(kNv12PixelShader, "ps_4_0", &nv12Blob) ||
        !compileShader(kBgraPixelShader, "ps_4_0", &bgraBlob))
    {
        return false;
    }

    if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader)) ||
        FAILED(m_device->CreatePixelShader(nv12Blob->GetBufferPointer(), nv12Blob->GetBufferSize(), nullptr, &m_nv12PixelShader)) ||
        FAILED(m_device->CreatePixelShader(bgraBlob->GetBufferPointer(), bgraBlob->GetBufferSize(), nullptr, &m_bgraPixelShader)))
    {
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    return SUCCEEDED(m_device->CreateSamplerState(&samplerDesc, &m_sampler));
}

#endif
