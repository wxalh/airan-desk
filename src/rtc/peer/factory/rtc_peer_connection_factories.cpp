#include "rtc/peer/factory/rtc_peer_connection_factories.h"

#include "common/logger_manager.h"
#include "rtc/peer/factory/rtc_shared_audio_device_module.h"

#if AIRAN_WEBRTC_MILESTONE >= 144
#include <api/audio/create_audio_device_module.h>
#include <api/environment/environment_factory.h>
#endif

#if defined(__linux__)
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>
#endif

namespace rtc
{

#if AIRAN_WEBRTC_MILESTONE >= 144 && defined(__linux__)
namespace
{

bool hasNonEmptyEnv(const char *name)
{
    const char *value = std::getenv(name);
    return value && value[0];
}

bool pathExists(const std::filesystem::path &path)
{
    std::error_code error;
    return std::filesystem::exists(path, error);
}

bool linuxPulseAudioServerLooksAvailable()
{
    if (hasNonEmptyEnv("PULSE_SERVER"))
        return true;

    if (const char *runtimeDir = std::getenv("XDG_RUNTIME_DIR"))
    {
        if (runtimeDir[0] && pathExists(std::filesystem::path(runtimeDir) / "pulse" / "native"))
            return true;
    }

    const std::filesystem::path userRuntimeDir = std::filesystem::path("/run/user") / std::to_string(static_cast<long long>(getuid()));
    return pathExists(userRuntimeDir / "pulse" / "native");
}

} // namespace
#endif




scoped_refptr<webrtc::AudioDeviceModule> createAudioDeviceModule(bool enabled)
{
#if AIRAN_WEBRTC_MILESTONE >= 144
    auto env = webrtc::CreateEnvironment();
    bool usePlatformAudio = enabled;
#if defined(__linux__)
    if (usePlatformAudio && !linuxPulseAudioServerLooksAvailable())
    {
        LOG_WARN("Linux PulseAudio server is not available; using dummy WebRTC audio device module");
        usePlatformAudio = false;
    }
#endif
    const auto audioLayer = usePlatformAudio
#if defined(__linux__)
                                ? webrtc::AudioDeviceModule::kLinuxPulseAudio
#else
                                ? webrtc::AudioDeviceModule::kPlatformDefaultAudio
#endif
                                : webrtc::AudioDeviceModule::kDummyAudio;
    const char *audioLayerName = usePlatformAudio
#if defined(__linux__)
                                     ? "linux-pulseaudio"
#else
                                     ? "platform-default"
#endif
                                     : "dummy";
    LOG_INFO("Creating WebRTC audio device module: requested={}, layer={}",
             enabled,
             audioLayerName);
    return createSharedAudioDeviceModule(webrtc::CreateAudioDeviceModule(env, audioLayer));
#else
    (void)enabled;
    return nullptr;
#endif
}

} // namespace rtc
