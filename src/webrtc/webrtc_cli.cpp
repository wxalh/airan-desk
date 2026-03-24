#include "webrtc_cli.h"
#include "../common/constant.h"
#include "../util/json_util.h"
#include "../util/file_packet_util.h"
#include "../desktop/desktop_grab.h"
#include "../desktop/desktop_capture_manager.h"
#include <QStorageInfo>
#include <QDir>
#include <QUuid>
#include <QDataStream>
#include <QGuiApplication>
#include <QScreen>
#include <QThread>
#include <iostream>
#include <QPointer>

/**
 * WebRtcCli类实现
 * 该类负责处理WebRTC客户端的所有功能，包括连接、媒体处理、数据
 * init pc -> setup tracks -> create data channels -> on gathering complete ->send local sdp
 * -> on remote sdp -> set remote description -> send ice candidates
 * -> on ice candidate -> add ice candidate
 */
WebRtcCli::WebRtcCli(const QString &remoteId, int fps, bool isOnlyFile,
                     int controlMaxWidth, int controlMaxHeight, QObject *parent)
    : QObject(parent),
      m_remoteId(remoteId),
      m_isOnlyFile(isOnlyFile), // 默认不是仅文件传输
      m_currentDir(QDir::home()),
      m_connected(false),
      m_channelsReady(false),
      m_destroying(false),
      m_fps(fps),
      m_subscribed(false)
{

    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
    m_screen_width = screenGeometry.width();
    m_screen_height = screenGeometry.height();

    // 根据控制端最大显示区域和被控端实际分辨率计算合适的编码分辨率
    calculateOptimalResolution(controlMaxWidth, controlMaxHeight);

    // 初始化ICE服务器配置
    m_host = ConfigUtil->ice_host.toStdString();
    m_port = (uint16_t)ConfigUtil->ice_port;
    m_username = ConfigUtil->ice_username.toStdString();
    m_password = ConfigUtil->ice_password.toStdString();

    // 初始化文件分包工具类（由 QObject 父对象管理生命周期）
    m_filePacketUtil = new FilePacketUtil(this);

    // 连接文件接收信号
    connect(m_filePacketUtil, &FilePacketUtil::fileDownloadCompleted, this, &WebRtcCli::handleFileReceived);
    connect(m_filePacketUtil, &FilePacketUtil::fileReceived, this, &WebRtcCli::handleFileReceived);

    // 输入通道恢复定时器（防抖，避免弱网抖动时重协商风暴）
    m_inputChannelRecoverTimer.setSingleShot(true);
    connect(&m_inputChannelRecoverTimer, &QTimer::timeout, this, &WebRtcCli::recoverInputChannel);

    LOG_INFO("created for remote: {}", m_remoteId);
}

WebRtcCli::~WebRtcCli()
{
    LOG_DEBUG("WebRtcCli destructor");

    // 先调用destroy停止所有活动
    destroy();
}

void WebRtcCli::init()
{
    LOG_INFO("Creating PeerConnection and tracks for client side");
    // 初始化WebRTC
    initPeerConnection();

    setupCallbacks();
    // 创建轨道和数据通道
    createTracksAndChannels();
    m_peerConnection->createOffer();
}

void WebRtcCli::populateLocalFiles()
{
    // 获取已挂载的驱动器路径
    QJsonArray mountedPaths;
    QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &volume : volumes)
    {
        if (volume.isValid() && volume.isReady())
        {
            mountedPaths.append(volume.rootPath());
        }
    }

    m_currentDir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    m_currentDir.setSorting(QDir::Name | QDir::DirsFirst);

    QFileInfoList list = m_currentDir.entryInfoList();

    QJsonArray fileArray;
    for (const QFileInfo &entry : list)
    {
        QJsonObject fileObj = JsonUtil::createObject()
                                  .add(Constant::KEY_NAME, entry.fileName())
                                  .add(Constant::KEY_IS_DIR, entry.isDir())
                                  .add(Constant::KEY_FILE_SIZE, static_cast<double>(entry.size()))
                                  .add(Constant::KEY_FILE_LAST_MOD_TIME, entry.lastModified().toString(Qt::ISODate))
                                  .build();
        fileArray.append(fileObj);
    }

    QJsonObject responseMsg = JsonUtil::createObject()
                                  .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                  .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_LIST)
                                  .add(Constant::KEY_PATH, m_currentDir.absolutePath())
                                  .add(Constant::KEY_FOLDER_FILES, fileArray)
                                  .add(Constant::KEY_FOLDER_MOUNTED, mountedPaths)
                                  .build();

    sendFileTextChannelMessage(responseMsg);
}

// WebRTC核心功能
void WebRtcCli::initPeerConnection()
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
        LOG_ERROR("Unknown error during PeerConnection initialization");
    }
}

void WebRtcCli::createTracksAndChannels()
{
    if (!m_peerConnection)
    {
        LOG_ERROR("PeerConnection not available for creating tracks");
        return;
    }

    try
    {
        if (!m_isOnlyFile)
        {
            // 创建视频轨道 - 严格按照官方示例配置
            LOG_INFO("Creating video track");
            std::string video_name = Constant::TYPE_VIDEO.toStdString();
            rtc::Description::Video videoDesc(video_name); // 使用固定流名称匹配接收端
            videoDesc.addH264Codec(96);                    // H264 payload type

            // 设置SSRC和媒体流标识 - 关键配置
            uint32_t videoSSRC = 1;
            std::string msid = Constant::TYPE_VIDEO_MSID.toStdString();
            videoDesc.addSSRC(videoSSRC, video_name, msid, video_name);
            videoDesc.setDirection(rtc::Description::Direction::SendOnly);
            m_videoTrack = m_peerConnection->addTrack(videoDesc);

            // 为视频轨道设置RTP打包器链
            auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(videoSSRC, video_name, 96, rtc::H264RtpPacketizer::ClockRate);
            // 使用StartSequence分隔符，因为FFMPEG输出的是Annex-B格式（带有0x00000001起始码）
            auto h264Packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, rtpConfig);

            // 添加RTCP SR报告器
            auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
            h264Packetizer->addToChain(srReporter);

            // 添加RTCP NACK响应器
            auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
            h264Packetizer->addToChain(nackResponder);

            m_videoTrack->setMediaHandler(h264Packetizer);

            // 创建音频轨道
            LOG_INFO("Creating audio track");
            rtc::Description::Audio audioDesc(Constant::TYPE_AUDIO.toStdString()); // 使用固定流名称匹配接收端
            audioDesc.addOpusCodec(111);                                           // Opus payload type

            // 设置SSRC和媒体流标识
            uint32_t audioSSRC = 2;
            audioDesc.addSSRC(audioSSRC, Constant::TYPE_AUDIO.toStdString(), msid, Constant::TYPE_AUDIO.toStdString());
            audioDesc.setDirection(rtc::Description::Direction::SendOnly);
            m_audioTrack = m_peerConnection->addTrack(audioDesc);

            // 创建输入数据通道（低延迟优先：无序 + 最多0次重传）
            LOG_INFO("Creating input data channel");
            rtc::Reliability inputReliability;
            inputReliability.unordered = true;
            inputReliability.maxRetransmits = 0;
            m_inputChannel = m_peerConnection->createDataChannel(Constant::TYPE_INPUT.toStdString(), {inputReliability});
            setupInputChannelCallbacks();
        }
        // 创建文件数据通道（用于二进制文件传输）
        LOG_INFO("Creating file data channel");
        m_fileChannel = m_peerConnection->createDataChannel(Constant::TYPE_FILE.toStdString());
        setupFileChannelCallbacks();

        // 创建文件文本数据通道（用于文件列表、目录切换等文本消息）
        LOG_INFO("Creating file text data channel");
        m_fileTextChannel = m_peerConnection->createDataChannel(Constant::TYPE_FILE_TEXT.toStdString());
        setupFileTextChannelCallbacks();

        m_channelsReady = true;
        LOG_INFO("All tracks and channels created successfully");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to create tracks and channels: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Unknown error during tracks and channels creation");
    }
}

void WebRtcCli::setupCallbacks()
{
    if (!m_peerConnection)
        return;

    // 使用 QPointer 避免 lambda 捕获 this 导致强引用
    QPointer<WebRtcCli> weakThis(this);

    // 连接状态回调
    m_peerConnection->onStateChange([weakThis](rtc::PeerConnection::State state)
                                    {
        auto self = weakThis;
        if (!self) return;
        // 如果正在销毁，不处理回调
        if (self->m_destroying) {
            LOG_DEBUG("Ignoring state change callback during destruction");
            return;
        }
        self->m_connected = (state == rtc::PeerConnection::State::Connected);

        std::string stateStr;
        if(self->m_connected){
            stateStr = "Connected";
            rtc::Candidate local;
            rtc::Candidate remote;
            if(self->m_peerConnection && self->m_peerConnection->getSelectedCandidatePair(&local, &remote)){
                LOG_INFO("Selected candidate pair: local={}, remote={}", std::string(local), std::string(remote));
            }
        }else if(state == rtc::PeerConnection::State::Connecting){
            stateStr = "Checking";  
        }else if(state == rtc::PeerConnection::State::New){
            stateStr = "New";
        }else if(state == rtc::PeerConnection::State::Failed){
            stateStr = "Failed";
        }else if(state == rtc::PeerConnection::State::Disconnected){
            stateStr = "Disconnected";
        }else if(state == rtc::PeerConnection::State::Closed){
            stateStr = "Closed";
        }else{
            stateStr = "Unknown";
        }
        LOG_INFO("Client side connection state: {}", stateStr);
        if(self->m_isOnlyFile){
            return; // 仅文件传输模式不处理连接状态
        }
        if (self->m_connected) {
            LOG_INFO("WebRTC connection established, starting media capture");
            self->startMediaCapture();
        }else if(state == rtc::PeerConnection::State::Disconnected || state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed) {
            LOG_INFO("WebRTC connection lost, stopping media capture");
            self->stopMediaCapture();
        } });

    // ICE连接状态回调
    m_peerConnection->onIceStateChange([weakThis](rtc::PeerConnection::IceState state)
                                       {
        auto self = weakThis;
        if (!self) return;
        std::string stateStr;
        if (state == rtc::PeerConnection::IceState::Connected)
        {
            stateStr = "Connected";
        }
        else if (state == rtc::PeerConnection::IceState::Checking)
        {
            stateStr = "Checking";
        }
        else if (state == rtc::PeerConnection::IceState::New)
        {
            stateStr = "New";
        }
        else if (state == rtc::PeerConnection::IceState::Failed)
        {
            stateStr = "Failed";
        }
        else if (state == rtc::PeerConnection::IceState::Disconnected)
        {
            stateStr = "Disconnected";
        }
        else if (state == rtc::PeerConnection::IceState::Closed)
        {
            stateStr = "Closed";
        }
        else if (state == rtc::PeerConnection::IceState::Completed)
        {
            stateStr = "Completed";
        }
        else
        {
            stateStr = "Unknown";
        }
        LOG_INFO("Client side ICE state: {}", stateStr); });

    // ICE候选者收集回调
    m_peerConnection->onGatheringStateChange([weakThis](rtc::PeerConnection::GatheringState state)
                                             { 
                                                auto self = weakThis;
                                                if (!self) return;
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
                                                LOG_DEBUG("Client side gathering state: {}", stateStr); });

    m_peerConnection->onLocalDescription([weakThis](rtc::Description description)
                                         {
        auto self = weakThis;
        if (!self) return;
        try
        {
            QString sdp = QString::fromStdString(std::string(description));
            QString type = QString::fromStdString(description.typeString());
            if (type == Constant::TYPE_ANSWER)
            {
                return;
            }
            // 发送本地描述给控制端
            QJsonObject offerMsg = JsonUtil::createObject()
                                       .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                       .add(Constant::KEY_TYPE, type)
                                       .add(Constant::KEY_RECEIVER, self->m_remoteId)
                                       .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                       .add(Constant::KEY_DATA, sdp)
                                       .build();

            QString message = JsonUtil::toCompactString(offerMsg);
            Q_EMIT self->sendWsCliTextMsg(message);
            LOG_INFO("Sent local description ({}) to ctl", message);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Failed to send local description: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("Unknown error during local description handling");
        } });
    // 本地候选者回调
    m_peerConnection->onLocalCandidate([weakThis](const rtc::Candidate &candidate)
                                       {
        auto self = weakThis;
        if (!self) return;
        QString candidateStr = QString::fromStdString(std::string(candidate));
        QString midStr = QString::fromStdString(candidate.mid());
        
        // 发送本地候选者给控制端
        QJsonObject candidateMsg = JsonUtil::createObject()
            .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
            .add(Constant::KEY_TYPE, Constant::TYPE_CANDIDATE)
            .add(Constant::KEY_RECEIVER, self->m_remoteId)
            .add(Constant::KEY_SENDER, ConfigUtil->local_id)
            .add(Constant::KEY_DATA, candidateStr)
            .add(Constant::KEY_MID, midStr)
            .build();
        
        QString message = JsonUtil::toCompactString(candidateMsg);
        
            Q_EMIT self->sendWsCliTextMsg(message);
            LOG_DEBUG("Sent local candidate to cli: {}", message); });
}

void WebRtcCli::setupFileChannelCallbacks()
{
    if (!m_fileChannel)
        return;

    QPointer<WebRtcCli> weakThis(this);

    m_fileChannel->onOpen([weakThis]()
                          { 
                              auto self = weakThis; if(!self) return; 
                              LOG_INFO("File channel opened"); });

    m_fileChannel->onMessage([weakThis](auto data)
                             {
        auto self = weakThis; if(!self) return;
        if (std::holds_alternative<rtc::binary>(data)) {
            auto binaryData = std::get<rtc::binary>(data);
            LOG_TRACE("File channel received binary data: {}", ConvertUtil::formatFileSize(binaryData.size()));
            // 所有数据都按分包格式处理
            if (self->m_filePacketUtil) self->m_filePacketUtil->processReceivedFragment(binaryData, "file");
        } else if (std::holds_alternative<std::string>(data)) {
            // 文件通道不再处理文本消息，记录警告
            LOG_WARN("File channel received text message, but should use file_text channel instead");
        } });

    m_fileChannel->onError([weakThis](std::string error)
                           { 
                               auto self = weakThis; if(!self) return; 
                               LOG_ERROR("File channel error: {}", error); });

    m_fileChannel->onClosed([weakThis]()
                            { 
                                auto self = weakThis; if(!self) return; 
                                LOG_INFO("File channel closed"); 
                                if(self->m_isOnlyFile) {
                                    // 仅文件传输模式下，销毁客户端
                                    Q_EMIT self->destroyCli();
                                } });
}

void WebRtcCli::setupFileTextChannelCallbacks()
{
    if (!m_fileTextChannel)
        return;

    QPointer<WebRtcCli> weakThis(this);

    m_fileTextChannel->onOpen([weakThis]()
                              {
                                  auto self = weakThis;
                                  if (!self)
                                      return;
                                  LOG_INFO("File text channel opened");
                                  self->populateLocalFiles(); // 在文本通道开启时发送初始文件列表
                              });

    m_fileTextChannel->onMessage([weakThis](auto data)
                                 {
        auto self = weakThis; if(!self) return;
        if (std::holds_alternative<std::string>(data)) {
            // 处理来自控制端的文件文本消息
            std::string message = std::get<std::string>(data);
            QString msgStr = QString::fromUtf8(message.c_str(), message.length());
            LOG_TRACE("File text channel received message: {}", msgStr);
            
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(msgStr.toUtf8(), &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                LOG_ERROR("File text channel message parse error: {}", parseError.errorString());
                return;
            }
            
            QJsonObject object = doc.object();
            self->parseFileMsg(object);
        } else {
            LOG_WARN("File text channel received binary data, ignoring");
        } });

    m_fileTextChannel->onError([weakThis](std::string error)
                               { auto self = weakThis; if(!self) return; LOG_ERROR("File text channel error: {}", error); });

    m_fileTextChannel->onClosed([weakThis]()
                                { auto self = weakThis; if(!self) return; LOG_INFO("File text channel closed"); });
}

void WebRtcCli::setupInputChannelCallbacks()
{
    if (!m_inputChannel)
        return;

    QPointer<WebRtcCli> weakThis(this);

    m_inputChannel->onOpen([weakThis]()
                           {
                               auto self = weakThis;
                               if(!self) return;
                               if (self->m_inputChannelRecoverTimer.isActive())
                                   self->m_inputChannelRecoverTimer.stop();
                               LOG_INFO("Input channel opened");
                           });

    m_inputChannel->onMessage([weakThis](auto data)
                              {
        auto self = weakThis; if(!self) return;
        if (std::holds_alternative<std::string>(data)) {
            // 处理来自控制端的输入消息
            std::string message = std::get<std::string>(data);
            QString msgStr = QString::fromUtf8(message.c_str(), message.length());
            
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(msgStr.toUtf8(), &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                LOG_ERROR("Input channel message parse error: {}", parseError.errorString());
                return;
            }
            
            QJsonObject object = doc.object();
            self->parseInputMsg(object);
        } });

    m_inputChannel->onError([weakThis](std::string error)
                            {
                                auto self = weakThis;
                                if(!self) return;
                                LOG_ERROR("Input channel error: {}", error);
                                self->scheduleInputChannelRecovery(QString::fromStdString(error));
                            });

    m_inputChannel->onClosed([weakThis]()
                             {
                                 auto self = weakThis;
                                 if(!self) return;
                                 LOG_INFO("Input channel closed");
                                 self->scheduleInputChannelRecovery("closed");
                             });
}

void WebRtcCli::scheduleInputChannelRecovery(const QString &reason)
{
    if (m_destroying || m_isOnlyFile || !m_peerConnection)
        return;

    if (m_inputChannelRecoverTimer.isActive())
        return;

    LOG_WARN("Input channel unavailable (reason: {}), schedule channel-level renegotiation", reason);
    m_inputChannelRecoverTimer.start(1200);
}

void WebRtcCli::recoverInputChannel()
{
    if (m_destroying || m_isOnlyFile || !m_peerConnection)
        return;

    // 若当前通道已经恢复，则跳过
    if (m_inputChannel && m_inputChannel->isOpen())
    {
        LOG_INFO("Input channel already open, skip recovery");
        return;
    }

    try
    {
        if (m_inputChannel)
        {
            try { m_inputChannel->resetCallbacks(); } catch (...) {}
            try { m_inputChannel->close(); } catch (...) {}
            m_inputChannel.reset();
        }

        rtc::Reliability inputReliability;
        inputReliability.unordered = true;
        inputReliability.maxRetransmits = 0;
        m_inputChannel = m_peerConnection->createDataChannel(Constant::TYPE_INPUT.toStdString(), {inputReliability});
        setupInputChannelCallbacks();

        // 通道级恢复需要重新协商
        m_peerConnection->createOffer();
        LOG_INFO("Input channel recreated, renegotiation offer sent");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("recoverInputChannel failed: {}", e.what());
        m_inputChannelRecoverTimer.start(2000);
    }
    catch (...)
    {
        LOG_ERROR("recoverInputChannel failed: unknown error");
        m_inputChannelRecoverTimer.start(2000);
    }
}

void WebRtcCli::destroy()
{
    disconnect();
    // 设置销毁标志防止回调执行
    m_destroying = true;
    m_connected = false;
    m_channelsReady = false;

    // 在释放前先关闭并重置数据通道回调，避免回调在释放期间被触发或持有strong ref
    if (m_inputChannel)
    {
        try
        {
            m_inputChannel->resetCallbacks();
        }
        catch (...)
        {
        }
        try
        {
            m_inputChannel->close();
        }
        catch (...)
        {
        }
        m_inputChannel.reset();
    }

    if (m_fileChannel)
    {
        try
        {
            m_fileChannel->resetCallbacks();
        }
        catch (...)
        {
        }
        try
        {
            m_fileChannel->close();
        }
        catch (...)
        {
        }
        m_fileChannel.reset();
    }

    if (m_fileTextChannel)
    {
        try
        {
            m_fileTextChannel->resetCallbacks();
        }
        catch (...)
        {
        }
        try
        {
            m_fileTextChannel->close();
        }
        catch (...)
        {
        }
        m_fileTextChannel.reset();
    }

    // 清理轨道，先重置回调并解除 media handler，避免可能的 shared_ptr 循环引用导致对象无法释放
    if (m_videoTrack)
    {
        try
        {
            m_videoTrack->resetCallbacks();
        }
        catch (...)
        {
        }
        try
        {
            m_videoTrack->setMediaHandler(nullptr);
        }
        catch (...)
        {
        }
        try
        {
            m_videoTrack->close();
        }
        catch (...)
        {
        }
        m_videoTrack.reset();
    }

    if (m_audioTrack)
    {
        try
        {
            m_audioTrack->resetCallbacks();
        }
        catch (...)
        {
        }
        try
        {
            m_audioTrack->setMediaHandler(nullptr);
        }
        catch (...)
        {
        }
        try
        {
            m_audioTrack->close();
        }
        catch (...)
        {
        }
        m_audioTrack.reset();
    }

    // 关闭PeerConnection（在通道和轨道都已关闭/解除后关闭）
    if (m_peerConnection)
    {
        try
        {
            m_peerConnection->resetCallbacks();
        }
        catch (...)
        {
        }
        try
        {
            m_peerConnection->close();
        }
        catch (...)
        {
        }
        m_peerConnection.reset();
    }

    if (m_filePacketUtil)
    {
        // QObject 父子会在父对象析构时处理，但显式断开并 deleteLater 可更快释放
        m_filePacketUtil->disconnect();
        m_filePacketUtil->deleteLater();
        m_filePacketUtil = nullptr;
    }
    // 清理分包数据
    m_uploadFragments.clear();

    LOG_INFO("WebRtcCli destroyed");
}

// WebSocket消息处理
void WebRtcCli::onWsCliRecvBinaryMsg(const QByteArray &message)
{
    parseWsMsg(JsonUtil::safeParseObject(message));
}

void WebRtcCli::onWsCliRecvTextMsg(const QString &message)
{
    parseWsMsg(JsonUtil::safeParseObject(message.toUtf8()));
}

// 简化实现的存根方法
void WebRtcCli::parseWsMsg(const QJsonObject &object)
{
    QString type = JsonUtil::getString(object, Constant::KEY_TYPE);
    if (type.isEmpty())
    {
        LOG_ERROR("parseWsMsg: Missing or empty message type");
        return;
    }

    // 处理来自控制端的信令消息
    if (type == Constant::TYPE_OFFER || type == Constant::TYPE_ANSWER)
    {
        QString data = JsonUtil::getString(object, Constant::KEY_DATA);
        if (!data.isEmpty())
        {
            setRemoteDescription(data, type);
            LOG_TRACE("parseWsMsg: Processed {} message", type);
        }
        else
        {
            LOG_ERROR("parseWsMsg: Empty data for {} message", type);
        }
    }
    else if (type == Constant::TYPE_CANDIDATE)
    {
        QString data = JsonUtil::getString(object, Constant::KEY_DATA);
        QString mid = JsonUtil::getString(object, Constant::KEY_MID);
        if (!data.isEmpty())
        {
            addIceCandidate(data, mid);
            LOG_TRACE("parseWsMsg: Processed candidate message");
        }
        else
        {
            LOG_ERROR("parseWsMsg: Empty data for candidate message");
        }
    }
}
void WebRtcCli::parseFileMsg(const QJsonObject &object)
{
    QString msgType = JsonUtil::getString(object, Constant::KEY_MSGTYPE);
    if (msgType.isEmpty())
    {
        LOG_ERROR("parseFileMsg: Missing msgType");
        return;
    }

    if (msgType == Constant::TYPE_FILE_LIST)
    {
        QString path = JsonUtil::getString(object, Constant::KEY_PATH);
        LOG_INFO("Processing file list request for path: {}", path);
        if (path.isEmpty())
        {
            LOG_ERROR("parseFileMsg: Missing path for file list request");
            return;
        }
        if (path == Constant::FOLDER_HOME)
        {
            m_currentDir = QDir::home();
        }
        else
        {
            m_currentDir.setPath(path);
        }

        populateLocalFiles();
    }
    else if (msgType == Constant::TYPE_FILE_DOWNLOAD)
    {
        QString cliPath = JsonUtil::getString(object, Constant::KEY_PATH_CLI);
        QString ctlPath = JsonUtil::getString(object, Constant::KEY_PATH_CTL);
        if (cliPath.isEmpty() || ctlPath.isEmpty())
        {
            LOG_ERROR("parseFileMsg: Missing file paths for download request");
            return;
        }
        sendFile(cliPath, ctlPath);
    }
    else if (msgType == Constant::TYPE_FILE_UPLOAD)
    {
        // 上传文件现在通过文件通道的二进制数据处理，不再需要输入通道处理
        LOG_INFO("File upload request received, waiting for binary data on file channel");
    }
    else
    {
        LOG_WARNING("parseFileMsg: Unknown message type: {}", msgType);
    }
}
void WebRtcCli::parseInputMsg(const QJsonObject &object)
{
    QString msgType = JsonUtil::getString(object, Constant::KEY_MSGTYPE);
    if (msgType.isEmpty())
    {
        LOG_ERROR("parseInputMsg: Missing msgType");
        return;
    }
    QString senderId = JsonUtil::getString(object, Constant::KEY_SENDER);
    if (senderId.isEmpty() || senderId != m_remoteId)
    {
        LOG_WARNING("parseInputMsg: Ignoring message from unknown sender: {}", senderId);
        return;
    }
    QString remoteId = JsonUtil::getString(object, Constant::KEY_RECEIVER);
    QString remotePwd = JsonUtil::getString(object, Constant::KEY_RECEIVER_PWD);
    if (remoteId.isEmpty() || remoteId != ConfigUtil->local_id || remotePwd != ConfigUtil->local_pwd_md5)
    {
        LOG_WARNING("parseInputMsg: Ignoring message for unknown receiver: {}, expected: {}, pwd: {}, expected: {}",
                    remoteId, ConfigUtil->local_id, remotePwd, ConfigUtil->local_pwd_md5);
        return;
    }
    if (msgType == Constant::TYPE_MOUSE)
    {
        // 处理鼠标事件
        handleMouseEvent(object);
    }
    else if (msgType == Constant::TYPE_KEYBOARD)
    {
        // 处理键盘事件
        handleKeyboardEvent(object);
    }
    else
    {
        LOG_WARNING("parseInputMsg: Unknown input message type: {}", msgType);
    }
}
void WebRtcCli::setRemoteDescription(const QString &data, const QString &type)
{
    if (!m_peerConnection)
        return;

    try
    {
        rtc::Description::Type descType;
        if (type == Constant::TYPE_OFFER)
        {
            descType = rtc::Description::Type::Offer;
        }
        else if (type == Constant::TYPE_ANSWER)
        {
            descType = rtc::Description::Type::Answer;
        }
        else
        {
            LOG_ERROR("Unknown description type: {}", type);
            return;
        }

        rtc::Description description(data.toStdString(), descType);
        m_peerConnection->setRemoteDescription(description);

        LOG_INFO("Set remote description: {}", type);
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
void WebRtcCli::addIceCandidate(const QString &candidate, const QString &mid)
{
    if (!m_peerConnection)
        return;

    try
    {
        rtc::Candidate rtcCandidate(candidate.toStdString(), mid.toStdString());
        m_peerConnection->addRemoteCandidate(rtcCandidate);
        LOG_TRACE("Added ICE candidate");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to add ICE candidate: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Failed to add ICE candidate: unknown error");
    }
}
void WebRtcCli::startMediaCapture()
{
    if (!m_isOnlyFile && !m_subscribed)
    {
        // 订阅桌面捕获管理器
        if (DesktopCaptureManager::instance()->subscribe(m_remoteId, 0, m_encode_width, m_encode_height, m_fps))
        {
            m_subscribed = true;
            // 连接管理器的信号（使用 QueuedConnection 以确保跨线程安全）
            connect(DesktopCaptureManager::instance(), &DesktopCaptureManager::frameEncoded,
                    this, &WebRtcCli::onVideoFrameReady, Qt::QueuedConnection);
            LOG_INFO("Subscribed to capture manager with resolution {}x{} and fps {}", m_encode_width, m_encode_height, m_fps);
        }
        else
        {
            LOG_ERROR("Failed to subscribe to capture manager");
        }
    }
}
void WebRtcCli::stopMediaCapture()
{
    LOG_INFO("Stopping media capture");
    // 取消订阅桌面捕获管理器
    if (m_subscribed)
    {
        DesktopCaptureManager::instance()->unsubscribe(m_remoteId, 0);
        m_subscribed = false;
        LOG_INFO("Unsubscribed from capture manager");
    }

    // 如果这是最后一个订阅者，管理器会自动停止捕获
    m_destroying = true;
    emit destroyCli(); // 通知销毁客户端
}
void WebRtcCli::onVideoFrameReady(const QString &subscriberId, std::shared_ptr<rtc::binary> encodedData, quint64 timestamp_us)
{
    int sz = encodedData ? (int)encodedData->size() : -1;
    LOG_TRACE("onVideoFrameReady called for subscriber {} (encodedData size={})", subscriberId, sz);

    if (subscriberId != m_remoteId || !m_videoTrack || !m_connected)
        return;

    // 验证H264数据有效性
    if (!encodedData || encodedData->empty())
    {
        LOG_WARN("Received empty video frame data");
        return;
    }
    m_lastTimestamp = timestamp_us;
    try
    {
        // 发送视频帧 - 使用官方示例的方式
        if (m_videoTrack->isOpen())
        {
            // 使用chrono duration发送帧
            m_videoTrack->sendFrame(*encodedData, std::chrono::duration<double, std::micro>(timestamp_us));
            LOG_TRACE("Sent video frame: {}, timestamp: {} us", ConvertUtil::formatFileSize(encodedData->size()), timestamp_us);
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to send video frame: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Failed to send video frame: unknown error");
    }
}

void WebRtcCli::sendFile(const QString &cliPath, const QString &ctlPath)
{
    QFileInfo info(cliPath);
    if (!info.exists())
    {
        LOG_ERROR("File or directory does not exist: {}", cliPath);
        sendFileErrorResponse(cliPath, "File or directory does not exist");
        return;
    }

    if (info.isFile())
    {
        // 发送单个文件
        sendSingleFile(cliPath, ctlPath);
    }
    else if (info.isDir())
    {
        // 发送文件夹中的所有文件
        sendDirectory(cliPath, ctlPath);
    }
    else
    {
        LOG_ERROR("Unknown file type: {}", cliPath);
        sendFileErrorResponse(cliPath, "Unknown file type");
    }
}

void WebRtcCli::sendSingleFile(const QString &cliPath, const QString &ctlPath)
{
    QFileInfo fileInfo(cliPath);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        LOG_ERROR("File does not exist or is not a regular file: {}", cliPath);
        sendFileErrorResponse(cliPath, "File does not exist or is not a regular file");
        return;
    }

    QString absCtlPath = ctlPath;
    if (!absCtlPath.endsWith(fileInfo.fileName()))
    {
        absCtlPath = QDir::cleanPath(absCtlPath + "/" + fileInfo.fileName());
    }

    // 创建包含文件信息的头部
    QJsonObject header = JsonUtil::createObject()
                             .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DOWNLOAD)
                             .add(Constant::KEY_PATH_CLI, cliPath)
                             .add(Constant::KEY_PATH_CTL, absCtlPath)
                             .add(Constant::KEY_FILE_SIZE, static_cast<double>(fileInfo.size()))
                             .add("isDirectory", false)
                             .build();

    // 使用流式发送方法，避免将大文件加载到内存
    if (m_fileChannel && m_fileChannel->isOpen())
    {
        try
        {
            if (FilePacketUtil::sendFileStream(cliPath, header, m_fileChannel))
            {
                LOG_INFO("Sent file stream: {} -> {} ({})",
                         cliPath, absCtlPath, ConvertUtil::formatFileSize(fileInfo.size()));
            }
            else
            {
                LOG_ERROR("Failed to send file stream: {}", cliPath);
                sendFileErrorResponse(cliPath, "Failed to send file stream");
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Exception during file stream send: {}", e.what());
            sendFileErrorResponse(cliPath, "Exception during file stream send");
        }
        catch (...)
        {
            LOG_ERROR("Failed to send file stream: unknown error");
        }
    }
    else
    {
        LOG_ERROR("File channel not available for sending file");
        sendFileErrorResponse(cliPath, "File channel not available");
    }
}

void WebRtcCli::sendDirectory(const QString &cliPath, const QString &ctlPath)
{
    QDir dir(cliPath);
    dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    dir.setSorting(QDir::Name | QDir::DirsFirst); // 按名称排序，文件夹优先
    // 遍历条目并分类
    QFileInfoList list = dir.entryInfoList();

    // 首先发送目录开始标记
    QJsonObject dirStartHeader = JsonUtil::createObject()
                                     .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DOWNLOAD)
                                     .add(Constant::KEY_PATH_CLI, cliPath)
                                     .add(Constant::KEY_PATH_CTL, ctlPath)
                                     .add("isDirectory", true)
                                     .add("directoryStart", true)
                                     .build();

    sendFileTextChannelMessage(dirStartHeader);

    int fileCount = 0;
    for (const QFileInfo &fileInfo : list)
    {
        if (fileInfo.isFile())
        {
            QString relativePath = dir.relativeFilePath(fileInfo.absoluteFilePath());

            // 在远程路径中包含目录名
            QString fullRemotePath = QDir::cleanPath(ctlPath + "/" + relativePath);

            // 发送单个文件（使用原始文件名作为显示名称）
            sendSingleFile(fileInfo.absoluteFilePath(), fullRemotePath);
            fileCount++;
        }
    }

    // 发送目录结束标记
    QJsonObject dirEndHeader = JsonUtil::createObject()
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DOWNLOAD)
                                   .add(Constant::KEY_PATH_CLI, cliPath)
                                   .add(Constant::KEY_PATH_CTL, ctlPath)
                                   .add("isDirectory", true)
                                   .add("directoryEnd", true)
                                   .add("fileCount", fileCount)
                                   .build();

    sendFileTextChannelMessage(dirEndHeader);

    LOG_INFO("Sent directory: {} -> {} ({} files)", cliPath, ctlPath, fileCount);
}

void WebRtcCli::sendFileErrorResponse(const QString &filePath, const QString &error)
{
    QJsonObject errorMsg = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DOWNLOAD)
                               .add(Constant::KEY_PATH, filePath)
                               .add("error", error)
                               .build();

    sendFileTextChannelMessage(errorMsg);
}

void WebRtcCli::sendUploadResponse(const QString &fileName, bool success, const QString &message)
{
    QJsonObject responseMsg = JsonUtil::createObject()
                                  .add(Constant::KEY_MSGTYPE, Constant::TYPE_UPLOAD_FILE_RES)
                                  .add(Constant::KEY_PATH_CLI, fileName)
                                  .add("status", success)
                                  .add("message", message)
                                  .build();

    sendFileTextChannelMessage(responseMsg);
}

void WebRtcCli::handleFileReceived(bool status, const QString &tempPath)
{
    LOG_INFO("Received complete file from FilePacketUtil, status: {}, tempPath: {}", status, tempPath);

    sendUploadResponse(tempPath, status, status ? "Upload successful" : "Upload failed");
}

void WebRtcCli::saveUploadedFile(const QString &filePath, const QByteArray &data)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        LOG_ERROR("Failed to open file for writing: {}", filePath);

        // 发送错误响应
        QJsonObject errorMsg = JsonUtil::createObject()
                                   .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_UPLOAD)
                                   .add(Constant::KEY_PATH, filePath)
                                   .add("error", "Failed to save file")
                                   .build();

        sendFileTextChannelMessage(errorMsg);
        return;
    }

    file.write(data);
    file.close();

    // 发送成功响应
    QJsonObject successMsg = JsonUtil::createObject()
                                 .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                 .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_UPLOAD)
                                 .add(Constant::KEY_PATH, filePath)
                                 .add("success", true)
                                 .add("size", data.size())
                                 .build();

    sendFileTextChannelMessage(successMsg);
    LOG_INFO("Saved uploaded file: {} ({})", filePath, ConvertUtil::formatFileSize(data.size()));
}
void WebRtcCli::handleMouseEvent(const QJsonObject &object)
{
    int button = JsonUtil::getInt(object, Constant::KEY_BUTTON, -1);
    qreal x = JsonUtil::getDouble(object, Constant::KEY_X, -1);
    qreal y = JsonUtil::getDouble(object, Constant::KEY_Y, -1);
    int mouseData = JsonUtil::getInt(object, Constant::KEY_MOUSEDATA, -1);
    QString flags = JsonUtil::getString(object, Constant::KEY_DWFLAGS, "");
    if (x < 0 || y < 0)
    {
        LOG_ERROR("handleMouseEvent: Invalid mouse event data");
        return;
    }

    // 使用InputUtil处理鼠标事件
    InputUtil::execMouseEvent(button, x, y, mouseData, flags);
    LOG_TRACE("Handled mouse event: {} at ({}, {})", flags, x, y);
}
void WebRtcCli::handleKeyboardEvent(const QJsonObject &object)
{
    int key = JsonUtil::getInt(object, Constant::KEY_KEY, -1);
    QString flags = JsonUtil::getString(object, Constant::KEY_DWFLAGS, "");

    if (key == -1 || flags.isEmpty())
    {
        LOG_ERROR("handleKeyboardEvent: Invalid keyboard event data");
        return;
    }

    // 使用InputUtil处理键盘事件
    InputUtil::execKeyboardEvent(key, flags);
    LOG_TRACE("Handled keyboard event: {} {}", flags, key);
}
void WebRtcCli::sendFileChannelMessage(const QJsonObject &message)
{
    if (!m_connected || !m_fileChannel || !m_fileChannel->isOpen())
    {
        LOG_ERROR("File channel not available");
        return;
    }

    QString jsonStr = JsonUtil::toCompactString(message);
    std::string stdStr = jsonStr.toStdString();

    try
    {
        m_fileChannel->send(stdStr);
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

void WebRtcCli::sendFileTextChannelMessage(const QJsonObject &message)
{
    if (!m_connected || !m_fileTextChannel || !m_fileTextChannel->isOpen())
    {
        LOG_ERROR("File text channel not available");
        return;
    }

    QString jsonStr = JsonUtil::toCompactString(message);
    std::string stdStr = jsonStr.toStdString();

    try
    {
        m_fileTextChannel->send(stdStr);
        LOG_TRACE("Sent file text channel message: {}", jsonStr);
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

void WebRtcCli::sendInputChannelMessage(const QJsonObject &message)
{
    if (!m_inputChannel || !m_inputChannel->isOpen())
    {
        LOG_ERROR("Input channel not available");
        return;
    }

    QString jsonStr = JsonUtil::toCompactString(message);
    std::string stdStr = jsonStr.toStdString();

    try
    {
        m_inputChannel->send(stdStr);
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

void WebRtcCli::calculateOptimalResolution(int controlMaxWidth, int controlMaxHeight)
{
    LOG_INFO("Calculating optimal encoding resolution - Control max display area: {}x{}, Local screen: {}x{}",
             controlMaxWidth, controlMaxHeight, m_screen_width, m_screen_height);

    // 如果控制端没有发送最大显示区域信息（-1），则使用被控端原始分辨率
    if (controlMaxWidth == -1 || controlMaxHeight == -1)
    {
        m_encode_width = m_screen_width;
        m_encode_height = m_screen_height;
        LOG_INFO("Using original local screen resolution: {}x{} (adaptive resolution disabled)",
                 m_encode_width, m_encode_height);
    }
    // 比较被控端实际分辨率和控制端最大显示区域，选择较小的
    else if (m_screen_width <= controlMaxWidth && m_screen_height <= controlMaxHeight)
    {
        // 被控端分辨率小于等于控制端显示区域，使用被控端实际分辨率
        m_encode_width = m_screen_width;
        m_encode_height = m_screen_height;
        LOG_INFO("Using local screen resolution: {}x{} (fits within control display area)",
                 m_encode_width, m_encode_height);
    }
    else
    {
        // 被控端分辨率大于控制端显示区域，需要按比例缩放保持宽高比
        double localAspectRatio = (double)m_screen_width / m_screen_height;
        double controlAspectRatio = (double)controlMaxWidth / controlMaxHeight;

        if (localAspectRatio > controlAspectRatio)
        {
            // 被控端更宽，以控制端宽度为准，按比例计算高度
            m_encode_width = controlMaxWidth;
            m_encode_height = (int)(controlMaxWidth / localAspectRatio);
        }
        else
        {
            // 被控端更高，以控制端高度为准，按比例计算宽度
            m_encode_height = controlMaxHeight;
            m_encode_width = (int)(controlMaxHeight * localAspectRatio);
        }

        LOG_INFO("Scaled to maintain aspect ratio: {}x{} (local aspect: {:.3f}, control aspect: {:.3f})",
                 m_encode_width, m_encode_height, localAspectRatio, controlAspectRatio);
    }

    // 确保编码分辨率按 16 对齐（MF/QSV/NVENC 等硬编更容易接受；同时仍满足 H264 偶数要求）
    // 向上取整到 16 的倍数： (x + 15) & ~15
    // 向下取整到 16 的倍数： x & ~15
    m_encode_width = m_encode_width & ~15;
    m_encode_height = m_encode_height & ~15;

    LOG_INFO("Final encoding resolution (16-aligned): {}x{}", m_encode_width, m_encode_height);
}
