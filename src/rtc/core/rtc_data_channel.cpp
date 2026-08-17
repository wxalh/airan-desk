#include "rtc/core/rtc_internal.h"

#include "common/logger_manager.h"

#include <api/data_channel_interface.h>
#include <rtc_base/copy_on_write_buffer.h>

#include <atomic>
#include <utility>

namespace rtc
{
DataChannel::DataChannel(scoped_refptr<webrtc::DataChannelInterface> channel)
    : m_channel(channel)
{
    LOG_DEBUG("Wrapping Google WebRTC data channel: label={}", label());
    if (m_channel)
        m_channel->RegisterObserver(this);
}

DataChannel::~DataChannel()
{
    resetCallbacks();
    if (m_channel)
        m_channel->UnregisterObserver();
}

bool DataChannel::send(const std::string &message)
{
    if (!m_channel || !isOpen())
        return false;
    return m_channel->Send(webrtc::DataBuffer(message));
}

bool DataChannel::send(const binary &data)
{
    if (!m_channel || !isOpen())
        return false;
    CopyOnWriteBuffer buffer(reinterpret_cast<const uint8_t *>(data.data()), data.size());
    return m_channel->Send(webrtc::DataBuffer(buffer, true));
}

bool DataChannel::send(const message_variant &message)
{
    if (std::holds_alternative<std::string>(message))
        return send(std::get<std::string>(message));
    return send(std::get<binary>(message));
}

void DataChannel::sendAsync(const message_variant &message, SendCallback callback)
{
    if (!callback)
        return;
    auto callbackHolder = std::make_shared<SendCallback>(std::move(callback));
    const auto completed = std::make_shared<std::atomic_bool>(false);
    const auto complete = [callbackHolder, completed](bool success) {
        if (completed->exchange(true))
            return;
        try
        {
            if (*callbackHolder)
                (*callbackHolder)(success);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Exception in asynchronous data channel send callback: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("Unknown exception in asynchronous data channel send callback");
        }
    };
    if (!m_channel || !isOpen())
    {
        complete(false);
        return;
    }

    webrtc::DataBuffer buffer = [&message]() {
        if (std::holds_alternative<std::string>(message))
            return webrtc::DataBuffer(std::get<std::string>(message));

        const binary &data = std::get<binary>(message);
        CopyOnWriteBuffer copy(reinterpret_cast<const uint8_t *>(data.data()), data.size());
        return webrtc::DataBuffer(copy, true);
    }();

#if AIRAN_WEBRTC_MILESTONE >= 144
    try
    {
        m_channel->SendAsync(std::move(buffer), [complete](webrtc::RTCError error) mutable {
            complete(error.ok());
        });
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception while queueing asynchronous data channel send: {}", e.what());
        complete(false);
    }
    catch (...)
    {
        LOG_ERROR("Unknown exception while queueing asynchronous data channel send");
        complete(false);
    }
#else
    // WebRTC m109 (the Win7 package) predates SendAsync; keep the same
    // completion contract while using its only available send primitive.
    try
    {
        complete(m_channel->Send(buffer));
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception while sending data channel message: {}", e.what());
        complete(false);
    }
    catch (...)
    {
        LOG_ERROR("Unknown exception while sending data channel message");
        complete(false);
    }
#endif
}

void DataChannel::close()
{
    if (m_channel)
        m_channel->Close();
}

bool DataChannel::isOpen() const
{
    return m_channel && m_channel->state() == webrtc::DataChannelInterface::kOpen;
}

bool DataChannel::isClosed() const
{
    return !m_channel || m_channel->state() == webrtc::DataChannelInterface::kClosed;
}

uint64_t DataChannel::bufferedAmount() const
{
    return m_channel ? m_channel->buffered_amount() : 0;
}

std::string DataChannel::label() const
{
    return m_channel ? m_channel->label() : std::string();
}

void DataChannel::resetCallbacks()
{
    std::lock_guard<std::mutex> locker(m_callbackMutex);
    m_onOpen = nullptr;
    m_onClosed = nullptr;
    m_onError = nullptr;
    m_onMessage = nullptr;
}

void DataChannel::onOpen(OpenCallback cb)
{
    {
        std::lock_guard<std::mutex> locker(m_callbackMutex);
        m_onOpen = std::move(cb);
    }

    // A remotely-created channel can already be open by the time its owner
    // reaches the thread where callbacks are installed. In that case
    // libwebrtc has already emitted OnStateChange(), so replay the current
    // state to the late subscriber. Query the native state without holding
    // m_callbackMutex to avoid lock-order inversion with OnStateChange().
    if (!isOpen())
        return;

    OpenCallback onOpen;
    {
        std::lock_guard<std::mutex> locker(m_callbackMutex);
        if (!m_openCallbackDelivered && m_onOpen)
        {
            m_openCallbackDelivered = true;
            onOpen = m_onOpen;
        }
    }
    if (onOpen)
        onOpen();
}

void DataChannel::onClosed(ClosedCallback cb)
{
    std::lock_guard<std::mutex> locker(m_callbackMutex);
    m_onClosed = std::move(cb);
}

void DataChannel::onError(ErrorCallback cb)
{
    std::lock_guard<std::mutex> locker(m_callbackMutex);
    m_onError = std::move(cb);
}

void DataChannel::onMessage(MessageCallback cb)
{
    std::lock_guard<std::mutex> locker(m_callbackMutex);
    m_onMessage = std::move(cb);
}

void DataChannel::OnStateChange()
{
    if (!m_channel)
        return;

    const auto state = m_channel->state();
    LOG_DEBUG("Data channel state changed: label={}, state={}", label(), webrtc::DataChannelInterface::DataStateString(state));
    OpenCallback onOpen;
    ClosedCallback onClosed;
    ErrorCallback onError;
    {
        std::lock_guard<std::mutex> locker(m_callbackMutex);
        if (state == webrtc::DataChannelInterface::kOpen &&
            !m_openCallbackDelivered && m_onOpen)
        {
            m_openCallbackDelivered = true;
            onOpen = m_onOpen;
        }
        onClosed = m_onClosed;
        onError = m_onError;
    }
    if (onOpen)
        onOpen();
    else if (state == webrtc::DataChannelInterface::kClosed)
    {
        const auto error = m_channel->error();
        if (!error.ok() && onError)
            onError(error.message());
        if (onClosed)
            onClosed();
    }
}

void DataChannel::OnMessage(const webrtc::DataBuffer &buffer)
{
    MessageCallback onMessage;
    {
        std::lock_guard<std::mutex> locker(m_callbackMutex);
        onMessage = m_onMessage;
    }
    if (!onMessage)
        return;
    if (buffer.binary)
        onMessage(bytesToBinary(buffer.data.data(), buffer.data.size()));
    else
        onMessage(std::string(reinterpret_cast<const char *>(buffer.data.data()), buffer.data.size()));
}

} // namespace rtc
