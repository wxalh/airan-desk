#pragma once

#include "rtc/core/rtc_base_types.h"

#include <api/data_channel_interface.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace rtc
{


class DataChannel final : public std::enable_shared_from_this<DataChannel>, private webrtc::DataChannelObserver
{
public:
    using OpenCallback = std::function<void()>;
    using ClosedCallback = std::function<void()>;
    using ErrorCallback = std::function<void(std::string)>;
    using MessageCallback = std::function<void(message_variant)>;
    using SendCallback = std::function<void(bool success)>;

    explicit DataChannel(scoped_refptr<webrtc::DataChannelInterface> channel);
    ~DataChannel() override;

    bool send(const std::string &message);
    bool send(const binary &data);
    bool send(const message_variant &message);
    void sendAsync(const message_variant &message, SendCallback callback);
    void close();
    bool isOpen() const;
    bool isClosed() const;
    uint64_t bufferedAmount() const;
    std::string label() const;
    void resetCallbacks();

    void onOpen(OpenCallback cb);
    void onClosed(ClosedCallback cb);
    void onError(ErrorCallback cb);
    void onMessage(MessageCallback cb);

private:
    void OnStateChange() override;
    void OnMessage(const webrtc::DataBuffer &buffer) override;

    scoped_refptr<webrtc::DataChannelInterface> m_channel;
    mutable std::mutex m_callbackMutex;
    OpenCallback m_onOpen;
    ClosedCallback m_onClosed;
    ErrorCallback m_onError;
    MessageCallback m_onMessage;
    bool m_openCallbackDelivered{false};
};

} // namespace rtc
