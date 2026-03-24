#include "webrtc_ctl.h"
#include "../common/constant.h"
#include "../codec/h264_decoder.h"
#include "../util/json_util.h"
#include "../util/file_packet_util.h"
#include <QTimer>
#include <QApplication>
#include <QScreen>
#include <QThread>
#include <QDataStream>
#include <QUuid>
#include <QDateTime>
#include <iostream>
#include <QPointer>

/**
 * WebRtcCtl类实现
 * 该类负责处理WebRTC客户端的所有功能，包括连接、媒体处理、数据
 * init pc -> setup tracks and datachannels -> on recv remote sdp ->send remote sdp -> send local sdp
 * -> on remote ice candidates -> send local ice candidates
 */
WebRtcCtl::WebRtcCtl(const QString &remoteId, const QString &remotePwdMd5,
                     bool isOnlyFile, bool adaptiveResolution, QObject *parent)
    : QObject(parent),
      m_remoteId(remoteId),
      m_remotePwdMd5(remotePwdMd5),
      m_connected(false),
      m_isOnlyFile(isOnlyFile),
      m_adaptiveResolution(adaptiveResolution)
{
    // 初始化ICE服务器配置
    m_host = ConfigUtil->ice_host.toStdString();
    m_port = (uint16_t)ConfigUtil->ice_port;
    m_username = ConfigUtil->ice_username.toStdString();
    m_password = ConfigUtil->ice_password.toStdString();

    // 初始化文件分包工具
    m_filePacketUtil = std::make_unique<FilePacketUtil>(this);

    // 连接文件分包工具的信号
    connect(m_filePacketUtil.get(), &FilePacketUtil::fileDownloadCompleted,
            this, &WebRtcCtl::recvDownloadFile);
    connect(m_filePacketUtil.get(), &FilePacketUtil::fileReceived,
            this, &WebRtcCtl::recvDownloadFile);

    // 重连定时器（单次触发）
    connect(&m_reconnectTimer, &QTimer::timeout, this, &WebRtcCtl::doReconnect);

    LOG_INFO("created for remote: {}", m_remoteId);
}

WebRtcCtl::~WebRtcCtl()
{
    LOG_DEBUG("destructor");
    m_reconnectTimer.stop();
    destroy();
}

void WebRtcCtl::init()
{
    LOG_INFO("Creating PeerConnection for control side");

    if (!m_isOnlyFile)
    {
        // 初始化H264解码器（启用硬件加速）
        m_h264Decoder = std::make_unique<H264Decoder>(this);
        if (!m_h264Decoder->initialize())
        {
            LOG_ERROR("Failed to initialize H264 decoder");
            m_h264Decoder = nullptr; // 确保解码器不可用
            return;
        }
    }
    // 初始化WebRTC
    initPeerConnection();
    if (!m_isOnlyFile)
    {
        // 创建接收轨道
        createTracks();
    }

    setupCallbacks();

    // 发送CONNECT消息给被控端
    JsonObjectBuilder connectMsgBuilder = JsonUtil::createObject()
                                              .add(Constant::KEY_ROLE, Constant::ROLE_CTL)
                                              .add(Constant::KEY_TYPE, Constant::TYPE_CONNECT)
                                              .add(Constant::KEY_RECEIVER, m_remoteId)
                                              .add(Constant::KEY_RECEIVER_PWD, m_remotePwdMd5)
                                              .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                              .add(Constant::KEY_IS_ONLY_FILE, m_isOnlyFile)
                                              .add(Constant::KEY_FPS, ConfigUtil->fps);

    // 如果启用了自适应分辨率，则包含控制端可显示的最大区域信息
    if (m_adaptiveResolution)
    {
        QScreen *screen = QApplication::primaryScreen();
        QRect screenGeometry = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

        // 计算控制端窗口能够显示的最大内容区域（减去标题栏等UI开销）
        int titleBarHeight = 30;                                         // 估算标题栏高度
        int maxContentWidth = screenGeometry.width() - 20;               // 减去边距
        int maxContentHeight = screenGeometry.height() - titleBarHeight; // 减去各种UI开销

        connectMsgBuilder = connectMsgBuilder.add("control_max_width", maxContentWidth)
                                .add("control_max_height", maxContentHeight);

        LOG_INFO("Sending CONNECT message with adaptive resolution - max display area: {}x{}",
                 maxContentWidth, maxContentHeight);
    }
    else
    {
        LOG_INFO("Sending CONNECT message without adaptive resolution - client will use original resolution");
    }

    QJsonObject connectMsg = connectMsgBuilder.build();
    QString message = JsonUtil::toCompactString(connectMsg);
    emit sendWsCliTextMsg(message);
}

void WebRtcCtl::initPeerConnection()
{
    try
    {
        // 配置ICE服务器
        rtc::Configuration config;

        // STUN服务器
        rtc::IceServer stunServer(m_host, m_port);
        config.iceServers.push_back(stunServer);

        rtc::IceServer turnUdpServer(m_host, m_port, m_username, m_password, rtc::IceServer::RelayType::TurnUdp);
        config.iceServers.push_back(turnUdpServer);

        rtc::IceServer turnTcpServer(m_host, m_port, m_username, m_password, rtc::IceServer::RelayType::TurnTcp);
        config.iceServers.push_back(turnTcpServer);

        // 创建PeerConnection
        m_peerConnection = std::make_shared<rtc::PeerConnection>(config);
        LOG_INFO("PeerConnection created successfully");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to initialize PeerConnection: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Failed to initialize PeerConnection: unknown error");
    }
}

void WebRtcCtl::createTracks()
{
    if (!m_peerConnection)
    {
        LOG_ERROR("PeerConnection not available for creating tracks");
        return;
    }

    try
    {
        // 创建视频接收轨道 - 设置RTP解包器
        LOG_INFO("Creating video receive track");
        std::string video_name = Constant::TYPE_VIDEO.toStdString();
        rtc::Description::Video videoDesc(video_name); // 使用固定流名称匹配发送端
        videoDesc.addH264Codec(96);                    // 确保payload type匹配
        uint32_t videoSSRC = 1;
        std::string msid = Constant::TYPE_VIDEO_MSID.toStdString();
        videoDesc.addSSRC(videoSSRC, video_name, msid, video_name);
        videoDesc.setDirection(rtc::Description::Direction::RecvOnly);
        m_videoTrack = m_peerConnection->addTrack(videoDesc);

        // 为视频轨道设置H264 RTP解包器 - 这是必需的！
        auto h264Depacketizer = std::make_shared<rtc::H264RtpDepacketizer>();
        m_videoTrack->setMediaHandler(h264Depacketizer);

        // 创建音频接收轨道
        LOG_INFO("Creating audio receive track");
        std::string audio_name = Constant::TYPE_AUDIO.toStdString();
        rtc::Description::Audio audioDesc(audio_name); // 使用固定流名称匹配发送端
        audioDesc.addOpusCodec(111);                   // 确保payload type匹配
        audioDesc.setDirection(rtc::Description::Direction::RecvOnly);
        m_audioTrack = m_peerConnection->addTrack(audioDesc);

        LOG_INFO("Control side tracks created successfully");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to create tracks: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Failed to create tracks: unknown error");
    }
}

void WebRtcCtl::setupCallbacks()
{
    if (!m_peerConnection)
        return;

    QPointer<WebRtcCtl> weakThis(this);

    // 连接状态回调
    m_peerConnection->onStateChange([weakThis](rtc::PeerConnection::State state)
                                    {
        auto self = weakThis;
        if (!self) return;
        self->m_connected = (state == rtc::PeerConnection::State::Connected);

        std::string stateStr;
        if(state == rtc::PeerConnection::State::Connected){
            stateStr = "Connected";
            rtc::Candidate local;
            rtc::Candidate remote;
            if(self->m_peerConnection && self->m_peerConnection->getSelectedCandidatePair(&local, &remote)){
                LOG_INFO("Selected candidate pair: local={}, remote={}", std::string(local), std::string(remote));
            }
            // 成功连接 -> 停止重连尝试
            self->stopReconnect();
        }else if(state == rtc::PeerConnection::State::Connecting){
            stateStr = "Checking";  
        }else if(state == rtc::PeerConnection::State::New){
            stateStr = "New";
        }else if(state == rtc::PeerConnection::State::Failed){
            stateStr = "Failed";
            // 连接失败 -> 计划重连
            self->scheduleReconnect();
        }else if(state == rtc::PeerConnection::State::Disconnected){
            stateStr = "Disconnected";
        }else if(state == rtc::PeerConnection::State::Closed){
            stateStr = "Closed";
            // 连接已关闭 -> 计划重连
            self->scheduleReconnect();
        }else{
            stateStr = "Unknown";
        }
        LOG_DEBUG("Control side connection state: {}",stateStr); });

    // ICE连接状态回调
    m_peerConnection->onIceStateChange([weakThis](rtc::PeerConnection::IceState state)
                                       {
        auto self = weakThis; if(!self) return;
        std::string stateStr;
        if(state == rtc::PeerConnection::IceState::Connected){
            stateStr = "Connected";
            // ICE 成功 -> 停止重连
            self->stopReconnect();
        }else if(state == rtc::PeerConnection::IceState::Checking){
            stateStr = "Checking";  
        }else if(state == rtc::PeerConnection::IceState::New){
            stateStr = "New";
        }else if(state == rtc::PeerConnection::IceState::Failed){
            stateStr = "Failed";
            // ICE 失败 -> 计划重连
            self->scheduleReconnect();
        }else if(state == rtc::PeerConnection::IceState::Disconnected){
            stateStr = "Disconnected";
        }else if(state == rtc::PeerConnection::IceState::Closed){
            stateStr = "Closed";
            // ICE 关闭 -> 计划重连
            self->scheduleReconnect();
        }else if(state == rtc::PeerConnection::IceState::Completed){
            stateStr = "Completed";
            // ICE 完成 -> 停止重连
            self->stopReconnect();
        }else{
            stateStr = "Unknown";
        }
        LOG_INFO("Control side ICE state: {}", stateStr); });

    // ICE候选者收集回调
    m_peerConnection->onGatheringStateChange([weakThis](rtc::PeerConnection::GatheringState state)
                                             {
        auto self = weakThis; if(!self) return;
        std::string stateStr;
        if(state == rtc::PeerConnection::GatheringState::InProgress){
            stateStr = "InProgress";
        }else if(state == rtc::PeerConnection::GatheringState::Complete){
            stateStr = "Complete";
        }else if(state == rtc::PeerConnection::GatheringState::New){
            stateStr = "New";
        }else{
            stateStr = "Unknown";
        }
        LOG_INFO("Control side ICE gathering state: {}", stateStr); });

    m_peerConnection->onLocalDescription([weakThis](rtc::Description description)
                                         {
        auto self = weakThis; if(!self) return;
        LOG_INFO("Control side local description set");
        try
        {
            QString sdp = QString::fromStdString(std::string(description));
            QString type = QString::fromStdString(description.typeString());
            if(type == Constant::TYPE_OFFER){
                return;
            }

            // 发送本地描述给被控端
            QJsonObject answerMsg = JsonUtil::createObject()
                                        .add(Constant::KEY_ROLE, Constant::ROLE_CTL)
                                        .add(Constant::KEY_TYPE, type)
                                        .add(Constant::KEY_RECEIVER, self->m_remoteId)
                                        .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                        .add(Constant::KEY_DATA, sdp)
                                        .build();

            QString message = JsonUtil::toCompactString(answerMsg);
            Q_EMIT self->sendWsCliTextMsg(message);
            LOG_INFO("Sent local description ({}) to cli", message);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Failed to send answer: {}", e.what());
        }catch (...)
        {
            LOG_ERROR("Failed to send answer: unknown error");
        } });

    // 本地候选者回调
    m_peerConnection->onLocalCandidate([weakThis](const rtc::Candidate &candidate)
                                       {
        auto self = weakThis; if(!self) return;
        QString candidateStr = QString::fromStdString(std::string(candidate));
        QString midStr = QString::fromStdString(candidate.mid());

        // 发送本地候选者给被控端
        QJsonObject candidateMsg = JsonUtil::createObject()
                                       .add(Constant::KEY_ROLE, Constant::ROLE_CTL)
                                       .add(Constant::KEY_TYPE, Constant::TYPE_CANDIDATE)
                                       .add(Constant::KEY_RECEIVER, self->m_remoteId)
                                       .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                       .add(Constant::KEY_DATA, candidateStr)
                                       .add(Constant::KEY_MID, midStr)
                                       .build();

        QString message = JsonUtil::toCompactString(candidateMsg);
        Q_EMIT self->sendWsCliTextMsg(message);
        LOG_DEBUG("Sent local candidate to cli: {}", message); });

    // 设置轨道回调 - 使用onMessage接收解包后的H264数据
    if (m_videoTrack)
    {
        LOG_INFO("Setting up video track message callback");
        QPointer<WebRtcCtl> weakThisTrack(this);
        m_videoTrack->onFrame([weakThisTrack](rtc::binary data, rtc::FrameInfo info)
                              {
            auto self = weakThisTrack; if(!self) return;
            LOG_TRACE("Video frame received: {}, timestamp: {}", ConvertUtil::formatFileSize(data.size()), info.timestamp);
            self->processVideoFrame(data, info); });
        LOG_INFO("Video track message callback set");
    }

    if (m_audioTrack)
    {
        LOG_INFO("Setting up audio track message callback");
        QPointer<WebRtcCtl> weakThisAudio(this);
        m_audioTrack->onFrame([weakThisAudio](rtc::binary data, rtc::FrameInfo info)
                              {
            auto self = weakThisAudio; if(!self) return;
            LOG_TRACE("Audio frame received: {}, ts: {}", ConvertUtil::formatFileSize(data.size()), info.timestamp);
            self->processAudioFrame(data, info); });
        LOG_INFO("Audio track message callback set");
    }

    // 接收到远程轨道回调（备用，以防某些情况下需要）
    m_peerConnection->onTrack([weakThis](std::shared_ptr<rtc::Track> track)
                              {
        auto self = weakThis; if(!self) return;
        QString trackMid = QString::fromStdString(track->mid());
        LOG_INFO("Control side received additional track: {}", trackMid); });

    // 接收到数据通道回调
    m_peerConnection->onDataChannel([weakThis](std::shared_ptr<rtc::DataChannel> channel)
                                    {
        auto self = weakThis; if(!self) return;
        QString channelLabel = QString::fromStdString(channel->label());
        LOG_INFO("Control side received data channel: {}", channelLabel);

        if (channelLabel == Constant::TYPE_FILE) {
            self->m_fileChannel = channel;
            self->setupFileChannelCallbacks();
        } else if (channelLabel == Constant::TYPE_FILE_TEXT) {
            self->m_fileTextChannel = channel;
            self->setupFileTextChannelCallbacks();
        } else if (channelLabel == Constant::TYPE_INPUT) {
            self->m_inputChannel = channel;
            self->setupInputChannelCallbacks();
        } });
}

void WebRtcCtl::setupFileChannelCallbacks()
{
    if (!m_fileChannel)
        return;

    QPointer<WebRtcCtl> weakThis(this);
    QString channelLabel = QString::fromStdString(m_fileChannel->label());

    m_fileChannel->onOpen([weakThis, channelLabel]()
                          { auto self = weakThis; if(!self) return; LOG_INFO("File channel opened: {}", channelLabel); });

    m_fileChannel->onClosed([weakThis, channelLabel]()
                            { auto self = weakThis; if(!self) return; LOG_INFO("File channel closed: {}", channelLabel); });

    m_fileChannel->onError([weakThis, channelLabel](const std::string &error)
                           { auto self = weakThis; if(!self) return; LOG_ERROR("File channel error: {}", error); });

    m_fileChannel->onMessage([weakThis, channelLabel](const rtc::message_variant &message)
                             {
        auto self = weakThis; if(!self) return;
        if (std::holds_alternative<rtc::binary>(message)) {
            auto binaryData = std::get<rtc::binary>(message);
            LOG_DEBUG("File channel received binary data: {}", ConvertUtil::formatFileSize(binaryData.size()));

            if (self->m_filePacketUtil) self->m_filePacketUtil->processReceivedFragment(binaryData, channelLabel);
        } else if (std::holds_alternative<std::string>(message)) {
            // 文件通道不再处理文本消息，记录警告
            LOG_WARN("File channel received text message, but should use file_text channel instead");
        } });
}

void WebRtcCtl::setupFileTextChannelCallbacks()
{
    if (!m_fileTextChannel)
        return;

    QPointer<WebRtcCtl> weakThis(this);
    QString channelLabel = QString::fromStdString(m_fileTextChannel->label());

    m_fileTextChannel->onOpen([weakThis, channelLabel]()
                              { auto self = weakThis; if(!self) return; LOG_INFO("File text channel opened: {}", channelLabel); });

    m_fileTextChannel->onClosed([weakThis, channelLabel]()
                                { auto self = weakThis; if(!self) return; LOG_INFO("File text channel closed: {}", channelLabel); });

    m_fileTextChannel->onError([weakThis, channelLabel](const std::string &error)
                               { auto self = weakThis; if(!self) return; LOG_ERROR("File text channel error: {}", error); });

    m_fileTextChannel->onMessage([weakThis, channelLabel](const rtc::message_variant &message)
                                 {
        auto self = weakThis; if(!self) return;
        if (std::holds_alternative<std::string>(message)) {
            // 处理文件相关的文本消息（文件列表、上传响应等）
            std::string data = std::get<std::string>(message);
            QByteArray dataArr = QByteArray::fromStdString(data);
            LOG_TRACE("File text channel received message: {}", QString::fromUtf8(dataArr));

            QJsonObject object = JsonUtil::safeParseObject(dataArr);
            if (JsonUtil::isValidObject(object)) {
                QString msgType = JsonUtil::getString(object, Constant::KEY_MSGTYPE);
                LOG_TRACE("Parsed message type: {}", msgType);

                if (msgType == Constant::TYPE_UPLOAD_FILE_RES) {
                    QString cliPath = JsonUtil::getString(object, Constant::KEY_PATH_CLI);
                    bool status = JsonUtil::getBool(object, "status");
                    LOG_INFO("Upload response: {} - {}", cliPath, status);
                    Q_EMIT self->recvUploadFileRes(status, cliPath);
                } else if (msgType == Constant::TYPE_FILE_LIST) {
                    Q_EMIT self->recvGetFileList(object);
                } else if (msgType == Constant::TYPE_FILE_DOWNLOAD) {
                    if(object.contains("directoryEnd")){
                        QString ctlPath = JsonUtil::getString(object, Constant::KEY_PATH_CTL);
                        Q_EMIT self->recvDownloadFile(true, ctlPath);
                    }
                } else {
                    Q_EMIT self->recvGetFileList(object);
                }
            } else {
                LOG_ERROR("Failed to parse JSON message: {}", QString::fromUtf8(dataArr));
            }
        } else {
            LOG_WARN("File text channel received binary data, ignoring");
        } });
}

void WebRtcCtl::setupInputChannelCallbacks()
{
    if (!m_inputChannel)
        return;

    QPointer<WebRtcCtl> weakThis(this);
    QString channelLabel = QString::fromStdString(m_inputChannel->label());

    m_inputChannel->onOpen([weakThis, channelLabel]()
                           { auto self = weakThis; if(!self) return; LOG_INFO("Input channel opened: {}", channelLabel); });

    m_inputChannel->onClosed([weakThis, channelLabel]()
                             {
                                 auto self = weakThis;
                                 if(!self) return;
                                 LOG_INFO("Input channel closed: {}", channelLabel);
                                 // 视频轨道仍可能可用，但输入通道已失效：触发重连恢复
                                 self->scheduleReconnect();
                             });

    m_inputChannel->onError([weakThis, channelLabel](const std::string &error)
                            {
                                auto self = weakThis;
                                if(!self) return;
                                LOG_ERROR("Input channel error: {}", error);
                                self->scheduleReconnect();
                            });

    m_inputChannel->onMessage([weakThis, channelLabel](const rtc::message_variant &message)
                              {
        auto self = weakThis; if(!self) return;
        if (std::holds_alternative<std::string>(message)) {
            // 输入通道现在只处理输入事件相关的消息
            LOG_DEBUG("Input channel message received (control side)");
        } else {
            LOG_DEBUG("Input channel binary message received (control side)"); 
        } });
}

void WebRtcCtl::parseWsMsg(const QJsonObject &object)
{
    // 忽略非信令消息（如心跳），避免无意义错误日志刷屏
    if (!object.contains(Constant::KEY_ROLE) || !object.contains(Constant::KEY_TYPE))
        return;

    QString role = JsonUtil::getString(object, Constant::KEY_ROLE);
    QString type = JsonUtil::getString(object, Constant::KEY_TYPE);

    // 只处理来自被控端的消息
    if (role != Constant::ROLE_CLI)
    {
        return;
    }

    // 处理SDP消息（Offer/Answer）
    if (type == Constant::TYPE_OFFER || type == Constant::TYPE_ANSWER)
    {
        QString data = JsonUtil::getString(object, Constant::KEY_DATA);
        if (!data.isEmpty())
        {
            try
            {
                LOG_INFO("Setting remote description: {}", type);
                rtc::Description desc(data.toStdString(), type.toStdString());
                m_peerConnection->setRemoteDescription(desc);
                m_peerConnection->createAnswer();
                LOG_INFO("Remote description set successfully");
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Failed to set remote description: {}", e.what());
            }
            catch (...)
            {
                LOG_ERROR("Failed to set remote description: unknown error");
            }
        }
    }
    // 处理ICE候选者
    else if (type == Constant::TYPE_CANDIDATE)
    {
        QString candidateStr = JsonUtil::getString(object, Constant::KEY_DATA);
        QString mid = JsonUtil::getString(object, Constant::KEY_MID);

        if (!candidateStr.isEmpty() && !mid.isEmpty())
        {
            try
            {
                m_peerConnection->addRemoteCandidate(rtc::Candidate(candidateStr.toStdString(), mid.toStdString()));
                LOG_DEBUG("Added remote candidate");
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Failed to add remote candidate: {}", e.what());
            }
            catch (...)
            {
                LOG_ERROR("Failed to add remote candidate: unknown error");
            }
        }
    }
}

void WebRtcCtl::onWsCliRecvBinaryMsg(const QByteArray &message)
{
    parseWsMsg(JsonUtil::safeParseObject(message));
}

void WebRtcCtl::onWsCliRecvTextMsg(const QString &message)
{
    parseWsMsg(JsonUtil::safeParseObject(message.toUtf8()));
}

void WebRtcCtl::inputChannelSendMsg(const rtc::message_variant &data)
{
    LOG_TRACE("inputChannelSendMsg called - connected: {}, inputChannel: {}, isOpen: {}",
              m_connected,
              (m_inputChannel != nullptr),
              (m_inputChannel && m_inputChannel->isOpen()));

    if (m_inputChannel && m_inputChannel->isOpen())
    {
        try
        {
            bool isMouseMove = false;
            if (std::holds_alternative<std::string>(data))
            {
                const std::string &msg = std::get<std::string>(data);
                isMouseMove = (msg.find("\"msgType\":\"mouse\"") != std::string::npos &&
                               msg.find("\"dwFlags\":\"move\"") != std::string::npos);
            }

            // 弱网保护：鼠标移动事件限流（约 60fps）
            if (isMouseMove)
            {
                qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                if ((nowMs - m_lastInputMoveSendMs) < 16)
                    return;
                m_lastInputMoveSendMs = nowMs;

                // 若发送缓冲区过高，直接丢弃旧 move 事件，避免输入“卡死感”
                size_t buffered = m_inputChannel->bufferedAmount();
                if (buffered > 64 * 1024)
                {
                    LOG_TRACE("Drop mouse move due to channel backlog: {} bytes", buffered);
                    return;
                }
            }

            if (!m_inputChannel->send(data))
            {
                LOG_TRACE("Input message buffered by SCTP");
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Failed to send input channel message: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("Failed to send input channel message: unknown error");
        }
    }
    else
    {
        // 若输入通道不可用，尝试触发恢复；重复调用由 scheduleReconnect 内部去重
        scheduleReconnect();

        // 2秒节流一次告警，避免鼠标移动导致日志风暴
        qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - m_lastInputNotReadyLogMs >= 2000)
        {
            m_lastInputNotReadyLogMs = nowMs;
            LOG_WARN("Input channel not ready for sending - channel exists: {}, channel open: {}",
                     (m_inputChannel != nullptr),
                     (m_inputChannel && m_inputChannel->isOpen()));
        }
    }
}

void WebRtcCtl::fileChannelSendMsg(const rtc::message_variant &data)
{
    LOG_TRACE("fileChannelSendMsg called - connected: {}, fileChannel: {}, isOpen: {}",
              m_connected,
              (m_fileChannel != nullptr),
              (m_fileChannel && m_fileChannel->isOpen()));

    if (m_connected && m_fileChannel && m_fileChannel->isOpen())
    {
        try
        {
            m_fileChannel->send(data);
            LOG_TRACE("Successfully sent file channel message");
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Failed to send file channel message: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("Failed to send file channel message: unknown error");
        }
    }
    else
    {
        LOG_WARN("File channel not ready for sending - connected: {}, channel exists: {}, channel open: {}",
                 m_connected,
                 (m_fileChannel != nullptr),
                 (m_fileChannel && m_fileChannel->isOpen()));
    }
}

void WebRtcCtl::fileTextChannelSendMsg(const rtc::message_variant &data)
{
    LOG_TRACE("fileTextChannelSendMsg called - connected: {}, fileTextChannel: {}, isOpen: {}",
              m_connected,
              (m_fileTextChannel != nullptr),
              (m_fileTextChannel && m_fileTextChannel->isOpen()));

    if (m_connected && m_fileTextChannel && m_fileTextChannel->isOpen())
    {
        try
        {
            m_fileTextChannel->send(data);
            LOG_TRACE("Successfully sent file text channel message");
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Failed to send file text channel message: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("Failed to send file text channel message: unknown error");
        }
    }
    else
    {
        LOG_WARN("File text channel not ready for sending - connected: {}, channel exists: {}, channel open: {}",
                 m_connected,
                 (m_fileTextChannel != nullptr),
                 (m_fileTextChannel && m_fileTextChannel->isOpen()));
    }
}

void WebRtcCtl::uploadFile2CLI(const QString &ctlPath, const QString &cliPath)
{
    LOG_WARN("uploadFile2CLI called: {} -> {}", ctlPath, cliPath);

    if (!m_fileChannel || !m_fileChannel->isOpen())
    {
        LOG_ERROR("File channel not available");
        emit recvUploadFileRes(false, ctlPath);
        return;
    }

    // 构造完整的本地文件路径
    QString fullLocalPath = ctlPath;

    QFileInfo fileInfo(ctlPath);
    if (!fileInfo.exists())
    {
        LOG_ERROR("File does not exist: {}", ctlPath);
        emit recvUploadFileRes(false, ctlPath);
        return;
    }

    if (fileInfo.isFile())
    {
        // 上传单个文件
        uploadSingleFile(ctlPath, cliPath);
    }
    else if (fileInfo.isDir())
    {
        // 上传目录
        uploadDirectory(ctlPath, cliPath);
    }
    else
    {
        LOG_ERROR("Unknown file type: {}", ctlPath);
        emit recvUploadFileRes(false, ctlPath);
    }
}

void WebRtcCtl::uploadSingleFile(const QString &ctlPath, const QString &cliPath)
{
    QFileInfo fileInfo(ctlPath);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        LOG_ERROR("File does not exist or is not a regular file: {}", ctlPath);
        emit recvUploadFileRes(false, cliPath);
        return;
    }

    // 创建包含文件信息的头部
    QJsonObject header = JsonUtil::createObject()
                             .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_UPLOAD)
                             .add(Constant::KEY_PATH_CTL, ctlPath)
                             .add(Constant::KEY_PATH_CLI, cliPath)
                             .add(Constant::KEY_FILE_SIZE, static_cast<double>(fileInfo.size()))
                             .add("isDirectory", false)
                             .build();

    // 使用流式发送方法，避免将大文件加载到内存
    if (m_fileChannel && m_fileChannel->isOpen())
    {
        try
        {
            if (FilePacketUtil::sendFileStream(ctlPath, header, m_fileChannel))
            {
                LOG_INFO("Sent file stream: {} -> {} ({})",
                         ctlPath, cliPath, ConvertUtil::formatFileSize(fileInfo.size()));
            }
            else
            {
                LOG_ERROR("Failed to send file stream: {}", ctlPath);
                emit recvUploadFileRes(false, cliPath);
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Exception during file stream send: {}", e.what());
            emit recvUploadFileRes(false, cliPath);
        }
        catch (...)
        {
            LOG_ERROR("Failed to send file stream: unknown error");
            emit recvUploadFileRes(false, cliPath);
        }
    }
    else
    {
        LOG_ERROR("File channel not available for uploading file");
        emit recvUploadFileRes(false, cliPath);
    }
}

void WebRtcCtl::uploadDirectory(const QString &ctlPath, const QString &cliPath)
{
    QDir dir(ctlPath);
    dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    dir.setSorting(QDir::Name | QDir::DirsFirst); // 按名称排序，文件夹优先
    // 遍历条目并分类
    QFileInfoList list = dir.entryInfoList();
    // 首先发送目录开始标记
    QJsonObject dirStartHeader = JsonUtil::createObject()
                                     .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_UPLOAD)
                                     .add(Constant::KEY_PATH_CTL, ctlPath)
                                     .add(Constant::KEY_PATH_CLI, cliPath)
                                     .add("isDirectory", true)
                                     .add("directoryStart", true)
                                     .build();

    QByteArray dirStartHeaderBytes = JsonUtil::toCompactBytes(dirStartHeader);
    rtc::binary dirStartData;
    dirStartData.resize(dirStartHeaderBytes.size());
    std::memcpy(dirStartData.data(), dirStartHeaderBytes.constData(), dirStartHeaderBytes.size());
    fileTextChannelSendMsg(dirStartData);

    int fileCount = 0;
    bool hasErrors = false;

    for (const QFileInfo &fileInfo : list)
    {
        if (fileInfo.isFile())
        {
            QString relativePath = dir.relativeFilePath(fileInfo.absoluteFilePath());

            // 在远程路径中包含目录名
            QString fullRemotePath = QDir::cleanPath(cliPath + "/" + relativePath);

            // 发送单个文件（使用原始文件名作为显示名称）
            uploadSingleFile(fileInfo.absoluteFilePath(), fullRemotePath);
            fileCount++;
        }
    }

    if (fileCount == 0)
    {
        LOG_WARN("No files found in directory: {}", ctlPath);
        emit recvUploadFileRes(false, cliPath);
    }
    else
    {
        LOG_INFO("Uploaded directory: {} -> {} ({} files)", ctlPath, cliPath, fileCount);
        // 对于目录上传，我们假设成功（单个文件的错误会单独处理）
        emit recvUploadFileRes(true, cliPath);
    }
    // 发送目录结束标记
    QJsonObject dirEndHeader = JsonUtil::createObject()
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_UPLOAD)
                                   .add(Constant::KEY_PATH_CTL, ctlPath)
                                   .add(Constant::KEY_PATH_CLI, cliPath)
                                   .add("isDirectory", true)
                                   .add("directoryEnd", true)
                                   .add("fileCount", fileCount)
                                   .build();

    QByteArray dirEndHeaderBytes = JsonUtil::toCompactBytes(dirEndHeader);
    rtc::binary dirEndData;
    dirEndData.resize(dirEndHeaderBytes.size());
    std::memcpy(dirEndData.data(), dirEndHeaderBytes.constData(), dirEndHeaderBytes.size());
    fileTextChannelSendMsg(dirEndData);

    LOG_INFO("Sent directory: {} -> {} ({} files)", ctlPath, cliPath, fileCount);
}

void WebRtcCtl::destroy()
{
    LOG_DEBUG("WebRtcCtl destroy started");
    m_connected = false;

    // 清理文件分包工具
    if (m_filePacketUtil)
    {
        m_filePacketUtil = nullptr;
    }

    // 清理数据通道（按顺序清理）
    if (m_inputChannel)
    {
        LOG_DEBUG("Cleaning up input channel");
        m_inputChannel->resetCallbacks();
        m_inputChannel->close();
        m_inputChannel = nullptr;
    }

    if (m_fileChannel)
    {
        LOG_DEBUG("Cleaning up file channel");
        m_fileChannel->resetCallbacks();
        m_fileChannel->close();
        m_fileChannel = nullptr;
    }

    if (m_fileTextChannel)
    {
        LOG_DEBUG("Cleaning up file text channel");
        m_fileTextChannel->resetCallbacks();
        m_fileTextChannel->close();
        m_fileTextChannel = nullptr;
    }

    // 清理轨道
    if (m_audioTrack)
    {
        LOG_DEBUG("Cleaning up audio track");
        m_audioTrack->resetCallbacks();
        m_audioTrack->close();
        m_audioTrack = nullptr;
    }

    if (m_videoTrack)
    {
        LOG_DEBUG("Cleaning up video track");
        m_videoTrack->resetCallbacks();
        m_videoTrack->close();
        m_videoTrack = nullptr;
    }

    // 最后清理PeerConnection
    if (m_peerConnection)
    {
        LOG_DEBUG("Cleaning up peer connection");
        m_peerConnection->resetCallbacks();
        m_peerConnection->close();
        m_peerConnection = nullptr;
    }

    LOG_INFO("WebRtcCtl destroyed");
}

// 计划一次重连（指数退避）
void WebRtcCtl::scheduleReconnect()
{
    if (!m_allowReconnect)
        return;

    // 多线程回调只允许首个调用者真正进入调度，避免一次断线瞬间排队多次
    bool expected = false;
    if (!m_reconnectPending.compare_exchange_strong(expected, true))
    {
        LOG_DEBUG("Reconnect already pending");
        return;
    }

    if (m_reconnectTimer.isActive())
    {
        LOG_DEBUG("Reconnect already scheduled");
        return;
    }

    m_reconnectAttempts++;

    // 首次重连更快，后续指数退避；上限缩短到 15 秒，避免长时间无法控制
    if (m_reconnectAttempts == 1)
    {
        m_reconnectBackoffMs = 3000;
    }
    else
    {
        m_reconnectBackoffMs = std::min(m_reconnectBackoffMs * 2, 15000);
    }

    LOG_INFO("Scheduling reconnect attempt {}, backoff {} ms", m_reconnectAttempts, m_reconnectBackoffMs);
    // 必须设为单次触发，避免连接成功后定时器仍循环触发重连
    m_reconnectTimer.setSingleShot(true);
    m_reconnectTimer.setInterval(m_reconnectBackoffMs);
    QMetaObject::invokeMethod(&m_reconnectTimer, "start", Qt::QueuedConnection);
}

void WebRtcCtl::stopReconnect()
{
    // QTimer 只能在其所属（Qt 主）线程操作。
    // libdatachannel 的回调运行在内部 IO 线程，直接调用 stop() 会导致崩溃。
    // 使用 Qt::QueuedConnection 将 stop() 投递回主线程执行。
    QMetaObject::invokeMethod(&m_reconnectTimer, "stop", Qt::QueuedConnection);
    m_reconnectAttempts = 0;
    m_reconnectBackoffMs = 3000; // 重置退避时间，下次重连从头开始
    m_reconnectPending.store(false);
}

// 重连执行函数（由定时器触发）
void WebRtcCtl::doReconnect()
{
    // 定时器已触发，允许后续再次调度
    m_reconnectPending.store(false);

    LOG_INFO("Reconnect attempt {} starting", m_reconnectAttempts);

    // 清理当前连接（但不影响类级工具对象，若被清理则重建）
    m_connected = false;
    destroy();

    // 确保文件分包工具在需要时被重建并重新连接信号
    if (!m_filePacketUtil)
    {
        m_filePacketUtil = std::make_unique<FilePacketUtil>(this);
        connect(m_filePacketUtil.get(), &FilePacketUtil::fileDownloadCompleted,
                this, &WebRtcCtl::recvDownloadFile);
        connect(m_filePacketUtil.get(), &FilePacketUtil::fileReceived,
                this, &WebRtcCtl::recvDownloadFile);
    }

    // 重新初始化（会发起新的 CONNECT/SDP 流程）
    try
    {
        init();
        LOG_INFO("Reconnect attempt {}: init() invoked", m_reconnectAttempts);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception during reconnect init: {}", e.what());
        scheduleReconnect();
    }
    catch (...)
    {
        LOG_ERROR("Unknown exception during reconnect init");
        scheduleReconnect();
    }
}

// 处理接收到的音频数据
void WebRtcCtl::processAudioFrame(const rtc::binary &audioData, const rtc::FrameInfo &frameInfo)
{
    LOG_TRACE("Received audio frame: {}", ConvertUtil::formatFileSize(audioData.size()));

    if (audioData.empty())
    {
        LOG_WARN("Received empty audio frame");
        return;
    }
}

// 处理接收到的视频数据
void WebRtcCtl::processVideoFrame(const rtc::binary &data, const rtc::FrameInfo &frameInfo)
{
    LOG_TRACE("Received video frame: {}", ConvertUtil::formatFileSize(data.size()));

    if (data.empty())
    {
        return;
    }

    try
    {
        // 解码H264数据为QImage
        if (m_h264Decoder)
        {
            QImage decodedFrame = m_h264Decoder->decodeFrame(data);
            if (!decodedFrame.isNull())
            {
                emit videoFrameDecoded(decodedFrame);
                LOG_TRACE("Successfully decoded video frame: {}x{}", decodedFrame.width(), decodedFrame.height());
            }
        }
        else
        {
            LOG_WARN("H264 decoder not initialized");
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error processing video frame: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Unknown error processing video frame");
    }
}
