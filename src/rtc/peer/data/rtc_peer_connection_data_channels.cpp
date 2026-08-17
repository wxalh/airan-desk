#include "rtc/core/rtc_internal.h"

#include "common/logger_manager.h"

#include <api/data_channel_interface.h>

#include <initializer_list>
#include <memory>
#include <string>

namespace rtc
{
namespace
{

scoped_refptr<webrtc::DataChannelInterface> createNativeDataChannel(
    const scoped_refptr<webrtc::PeerConnectionInterface> &peerConnection,
    const std::string &label,
    const webrtc::DataChannelInit &init)
{
    if (!peerConnection)
        return nullptr;

    auto result = peerConnection->CreateDataChannelOrError(label, &init);
    if (!result.ok())
    {
        LOG_WARN("CreateDataChannelOrError failed: label={}, error={}", label, result.error().message());
        return nullptr;
    }
    return result.MoveValue();
}

} // namespace

std::shared_ptr<DataChannel> PeerConnection::createDataChannel(const std::string &label)
{
    if (m_signalingThread && !m_signalingThread->IsQuitting() && !m_signalingThread->IsCurrent())
    {
        return m_signalingThread->BlockingCall([this, label]() {
            return createDataChannel(label);
        });
    }
    if (m_closed.load() || !m_pc)
        return nullptr;

    webrtc::DataChannelInit init;
    auto native = createNativeDataChannel(m_pc, label, init);
    if (!native)
        return nullptr;
    auto channel = std::make_shared<DataChannel>(native);
    pruneClosedDataChannels();
    m_channels.push_back(channel);
    return channel;
}


std::shared_ptr<DataChannel> PeerConnection::createDataChannel(const std::string &label, std::initializer_list<Reliability> reliability)
{
    if (m_signalingThread && !m_signalingThread->IsQuitting() && !m_signalingThread->IsCurrent())
    {
        const bool hasReliability = reliability.size() > 0;
        const Reliability reliabilityValue = hasReliability ? *reliability.begin() : Reliability{};
        return m_signalingThread->BlockingCall([this, label, hasReliability, reliabilityValue]() {
            return hasReliability
                       ? createDataChannel(label, {reliabilityValue})
                       : createDataChannel(label, {});
        });
    }
    if (m_closed.load() || !m_pc)
        return nullptr;

    webrtc::DataChannelInit init;
    if (reliability.size() > 0)
    {
        const auto &rel = *reliability.begin();
        init.ordered = !rel.unordered;
        if (rel.maxRetransmits >= 0)
            init.maxRetransmits = rel.maxRetransmits;
    }
    auto native = createNativeDataChannel(m_pc, label, init);
    if (!native)
        return nullptr;
    auto channel = std::make_shared<DataChannel>(native);
    pruneClosedDataChannels();
    m_channels.push_back(channel);
    return channel;
}
} // namespace rtc
