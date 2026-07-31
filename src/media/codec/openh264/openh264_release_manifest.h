#pragma once

#include <array>
#include <cstdint>

namespace airan::media::openh264::detail
{

struct ReleaseManifestEntry
{
    const char *version;
    const char *platform;
    const char *architecture;
    const char *fileName;
    std::int64_t fileSize;
    const char *sha256;
    const char *downloadUrl;
};

inline constexpr const char *kReleaseUrl =
    "https://github.com/cisco/openh264/releases/tag/v2.6.0";

inline constexpr std::array<ReleaseManifestEntry, 9> kReleaseManifest{{
    {"2.6.0", "windows", "x86", "openh264-2.6.0-win32.dll", 816216,
     "b0098db6acbd290a1fe13997d61d461e7327e39b42bf868db41faf498b7621a2",
     "https://ciscobinary.openh264.org/openh264-2.6.0-win32.dll.bz2"},
    {"2.6.0", "windows", "x64", "openh264-2.6.0-win64.dll", 978520,
     "2076cb5675ec6c1a4c70e7a2a322552f547b6eeed649d6dfcd9e02a543b24691",
     "https://ciscobinary.openh264.org/openh264-2.6.0-win64.dll.bz2"},
    {"2.6.0", "windows", "arm64", "openh264-2.6.0-win-arm64.dll", 810584,
     "fb75103938f4f47d119b983e06334df41a803bc72fb5c46e3623f6fea5782732",
     "https://ciscobinary.openh264.org/openh264-2.6.0-win-arm64.dll.bz2"},
    {"2.6.0", "linux", "x86", "libopenh264-2.6.0-linux32.8.so", 1600716,
     "a46589ccc95df7565ff8b1722d3dead29c0809be28322dc763767e0aa35a6443",
     "https://ciscobinary.openh264.org/libopenh264-2.6.0-linux32.8.so.bz2"},
    {"2.6.0", "linux", "x64", "libopenh264-2.6.0-linux64.8.so", 1731128,
     "2f0cde7c6a6abcf5cae76942894ea42897fa677bce4ed6c91a24dd1b041d5f04",
     "https://ciscobinary.openh264.org/libopenh264-2.6.0-linux64.8.so.bz2"},
    {"2.6.0", "linux", "arm", "libopenh264-2.6.0-linux-arm.8.so", 1419672,
     "df91866de0e93773019e30a8f2bdee8b15de4abe2bf89a228ae9f064ff1e85bb",
     "https://ciscobinary.openh264.org/libopenh264-2.6.0-linux-arm.8.so.bz2"},
    {"2.6.0", "linux", "arm64", "libopenh264-2.6.0-linux-arm64.8.so", 1492120,
     "12e7b33623667cdab0e575170c147b1b36eadb77d0d2aa7ceb5afd3e58902140",
     "https://ciscobinary.openh264.org/libopenh264-2.6.0-linux-arm64.8.so.bz2"},
    {"2.6.0", "macos", "x64", "libopenh264-2.6.0-mac-x64.dylib", 1381104,
     "e3dc8bc01fe69363f61fd3c02fd27798537a585eadd38cd808f303d1ee505a19",
     "https://ciscobinary.openh264.org/libopenh264-2.6.0-mac-x64.dylib.bz2"},
    {"2.6.0", "macos", "arm64", "libopenh264-2.6.0-mac-arm64.dylib", 1207136,
     "052e98bfcf7a9167d22f3bbb3f5988ef79065591f36af8b52924b22b13624551",
     "https://ciscobinary.openh264.org/libopenh264-2.6.0-mac-arm64.dylib.bz2"},
}};

} // namespace airan::media::openh264::detail
