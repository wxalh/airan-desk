#include "webrtc/ctl/webrtc_ctl.h"

#include "util/qt/qt_callback_util.h"


void WebRtcCtl::setupFileChannelCallbacks()
{
    if (!m_fileChannel)
        return;

    m_fileChannel->onClosed(makeWeakCallback(this, &WebRtcCtl::onFileChannelClosed, m_callbackLifetime));
    m_fileChannel->onError(makeWeakCallback(this, &WebRtcCtl::onFileChannelError, m_callbackLifetime));
    m_fileChannel->onMessage(makeWeakCallback(this, &WebRtcCtl::onFileChannelMessage, m_callbackLifetime));
    m_fileChannel->onOpen(makeWeakCallback(this, &WebRtcCtl::onFileChannelOpen, m_callbackLifetime));
}


void WebRtcCtl::setupFileTextChannelCallbacks()
{
    if (!m_fileTextChannel)
        return;

    m_fileTextChannel->onClosed(makeWeakCallback(this, &WebRtcCtl::onFileTextChannelClosed, m_callbackLifetime));
    m_fileTextChannel->onError(makeWeakCallback(this, &WebRtcCtl::onFileTextChannelError, m_callbackLifetime));
    m_fileTextChannel->onMessage(makeWeakCallback(this, &WebRtcCtl::onFileTextChannelMessage, m_callbackLifetime));
    m_fileTextChannel->onOpen(makeWeakCallback(this, &WebRtcCtl::onFileTextChannelOpen, m_callbackLifetime));
}


void WebRtcCtl::setupInputChannelCallbacks()
{
    if (!m_inputChannel)
        return;

    m_inputChannel->onClosed(makeWeakCallback(this, &WebRtcCtl::onInputChannelClosed, m_callbackLifetime));
    m_inputChannel->onError(makeWeakCallback(this, &WebRtcCtl::onInputChannelError, m_callbackLifetime));
    m_inputChannel->onMessage(makeWeakCallback(this, &WebRtcCtl::onInputChannelMessage, m_callbackLifetime));
    m_inputChannel->onOpen(makeWeakCallback(this, &WebRtcCtl::onInputChannelOpen, m_callbackLifetime));
}


void WebRtcCtl::setupInputMoveChannelCallbacks()
{
    if (!m_inputMoveChannel)
        return;

    m_inputMoveChannel->onClosed(makeWeakCallback(this, &WebRtcCtl::onInputMoveChannelClosed, m_callbackLifetime));
    m_inputMoveChannel->onError(makeWeakCallback(this, &WebRtcCtl::onInputMoveChannelError, m_callbackLifetime));
    m_inputMoveChannel->onOpen(makeWeakCallback(this, &WebRtcCtl::onInputMoveChannelOpen, m_callbackLifetime));
}


void WebRtcCtl::setupClipboardChannelCallbacks()
{
    if (!m_clipboardChannel)
        return;

    m_clipboardChannel->onClosed(makeWeakCallback(this, &WebRtcCtl::onClipboardChannelClosed, m_callbackLifetime));
    m_clipboardChannel->onError(makeWeakCallback(this, &WebRtcCtl::onClipboardChannelError, m_callbackLifetime));
    m_clipboardChannel->onMessage(makeWeakCallback(this, &WebRtcCtl::onClipboardChannelMessage, m_callbackLifetime));
    m_clipboardChannel->onOpen(makeWeakCallback(this, &WebRtcCtl::onClipboardChannelOpen, m_callbackLifetime));
}
