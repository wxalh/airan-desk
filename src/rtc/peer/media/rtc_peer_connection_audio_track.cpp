#include "rtc/peer/media/rtc_peer_connection_media_helpers.h"

#include "common/logger_manager.h"
#include "rtc/peer/factory/rtc_shared_audio_device_module.h"
#include "util/config/config_util.h"

#include <QString>

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr const char *kAudioDeviceNoneValue = "__none__";

bool isNoneDeviceValue(const QString &device)
{
    return device.trimmed().compare(QString::fromLatin1(kAudioDeviceNoneValue), Qt::CaseInsensitive) == 0;
}

QString configuredRecordingDeviceName(bool systemLoopback)
{
    if (systemLoopback)
        return {};

    const QString configured = ConfigUtil->audio_mic_device.trimmed();
    if (isNoneDeviceValue(configured))
        return {};
    if (configured.endsWith(QStringLiteral(".monitor")))
    {
        LOG_WARN("Configured microphone device is a monitor source and will be ignored for call mode: {}", configured);
        return {};
    }
    return configured;
}

bool isMonitorDeviceName(const std::string &name)
{
    return name.find(".monitor") != std::string::npos;
}

bool matchesConfiguredDevice(const std::string &name, const std::string &guid, const std::string &configured)
{
    return !configured.empty() &&
           (name == configured || guid == configured ||
            name.find(configured) != std::string::npos ||
            guid.find(configured) != std::string::npos);
}
} // namespace

namespace rtc
{

void PeerConnection::configureAudioRecordingDevice(bool systemLoopback)
{
    if (m_workerThread && !m_workerThread->IsQuitting() && !m_workerThread->IsCurrent())
    {
        m_workerThread->BlockingCall([this, systemLoopback]() {
            configureAudioRecordingDevice(systemLoopback);
        });
        return;
    }

    if (!m_audioDeviceModule)
        return;

    setSharedAudioRecordingMode(m_audioDeviceModule, systemLoopback);
    if (systemLoopback)
    {
        LOG_INFO("Selected shared mixed audio recording mode for listen audio");
        return;
    }

    if (m_audioDeviceModule->Recording())
    {
        LOG_INFO("Keeping current WebRTC recording device because recording is already active");
        return;
    }

    const std::string configured = configuredRecordingDeviceName(systemLoopback).toStdString();
    int selectedIndex = -1;
    std::string selectedName;
    std::string selectedGuid;
    const int16_t count = m_audioDeviceModule->RecordingDevices();
    if (count <= 0)
    {
        LOG_WARN("No WebRTC recording devices are available for audio mode: systemLoopback={}", systemLoopback);
        return;
    }

    for (int16_t index = 0; index < count; ++index)
    {
        std::array<char, webrtc::kAdmMaxDeviceNameSize> name{};
        std::array<char, webrtc::kAdmMaxGuidSize> guid{};
        if (m_audioDeviceModule->RecordingDeviceName(static_cast<uint16_t>(index), name.data(), guid.data()) != 0)
            continue;

        const std::string deviceName(name.data());
        const std::string deviceGuid(guid.data());
        LOG_DEBUG("WebRTC recording device[{}]: name={}, guid={}", index, deviceName, deviceGuid);

        if (matchesConfiguredDevice(deviceName, deviceGuid, configured))
        {
            selectedIndex = index;
            selectedName = deviceName;
            selectedGuid = deviceGuid;
            break;
        }

        if (configured.empty())
        {
            const bool monitor = isMonitorDeviceName(deviceName) || isMonitorDeviceName(deviceGuid);
            if (!monitor)
            {
                selectedIndex = index;
                selectedName = deviceName;
                selectedGuid = deviceGuid;
                break;
            }
        }
    }

    if (selectedIndex < 0)
    {
        LOG_WARN("Preferred WebRTC recording device was not found: configured={}, systemLoopback={}",
                 configured, systemLoopback);
        return;
    }

    const int result = m_audioDeviceModule->SetRecordingDevice(static_cast<uint16_t>(selectedIndex));
    if (result != 0)
    {
        LOG_WARN("Failed to select WebRTC recording device: index={}, name={}, guid={}, result={}",
                 selectedIndex, selectedName, selectedGuid, result);
        return;
    }

    LOG_INFO("Selected WebRTC recording device: index={}, name={}, guid={}, systemLoopback={}",
             selectedIndex, selectedName, selectedGuid, systemLoopback);
}

void PeerConnection::setAudioRecordingMode(bool systemLoopback)
{
    configureAudioRecordingDevice(systemLoopback);
}

std::shared_ptr<Track> PeerConnection::addTrack(const Description::Audio &desc)
{
    if (m_signalingThread && !m_signalingThread->IsQuitting() && !m_signalingThread->IsCurrent())
    {
        return m_signalingThread->BlockingCall([this, desc]() {
            return addTrack(desc);
        });
    }

    if (m_closed.load() || !m_pc || !m_factory)
    {
        LOG_WARN("Ignoring audio track request because PeerConnection is closed or unavailable");
        return nullptr;
    }

    std::vector<std::string> streamIds{desc.name()};

    if (desc.direction() == Description::Direction::RecvOnly)
    {
        LOG_INFO("Adding recv-only audio transceiver: name={}", desc.name());
        webrtc::RtpTransceiverInit init;
        init.direction = toNativeDirection(desc.direction());
        init.stream_ids = streamIds;
        auto result = m_pc->AddTransceiver(PeerConnectionMedia::nativeAudioMediaType(), init);
        if (!result.ok())
            throw std::runtime_error("failed to add recv-only audio transceiver: " + std::string(result.error().message()));
        auto wrapped = std::make_shared<Track>(false, desc.name(), Description::Direction::RecvOnly);
        m_tracks.push_back(wrapped);
        return wrapped;
    }

    configureAudioRecordingDevice(desc.systemLoopback());

    PeerConnectionMedia::NativeAudioOptions options;
    auto source = m_factory->CreateAudioSource(options);
    auto track = m_factory->CreateAudioTrack(desc.name(), source.get());
    if (!track)
        throw std::runtime_error("failed to create Google WebRTC audio track");

    webrtc::RtpTransceiverInit init;
    init.direction = toNativeDirection(desc.direction());
    init.stream_ids = streamIds;
    auto result = m_pc->AddTransceiver(track, init);
    if (!result.ok())
        throw std::runtime_error("failed to add Google WebRTC audio transceiver: " + std::string(result.error().message()));
    if (desc.systemLoopback())
        LOG_INFO("Audio track requested as shared system-loopback source");
    auto wrapped = std::make_shared<Track>(source, track, result.value()->sender(), desc.name(), desc.direction());
    m_tracks.push_back(wrapped);
    return wrapped;
}
} // namespace rtc
