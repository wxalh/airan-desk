#include "webrtc/ctl/webrtc_ctl.h"
#include "webrtc/codec/video_codec_capability_signaling.h"
#include "util/file/file_packet_util.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

#include <QGuiApplication>
#include <QScreen>
#include <QThread>
#include <QTimer>

namespace
{
/*
 * Constrains automatic remote desktop resolution to the control side's visible area.
 */
QSize defaultRemoteVideoConstraint()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    QSize size(qMax(640, available.width()), qMax(360, available.height()));
    size.scale(QSize(2560, 1440), Qt::KeepAspectRatio);
    size.setWidth(qMax(2, size.width() & ~1));
    size.setHeight(qMax(2, size.height() & ~1));
    return size;
}
} // namespace

WebRtcCtl::WebRtcCtl(const QString &remoteId, const QString &remotePwdMd5,
                     bool isOnlyFile, QObject *parent)
    : QObject(parent),
      m_remoteId(remoteId),
      m_remotePwdMd5(remotePwdMd5),
      m_sessionId(QUuid::createUuid().toString().remove('{').remove('}')),
      m_connected(false),
      m_isOnlyFile(isOnlyFile)
{
    m_host = ConfigUtil->ice_host.toStdString();
    m_port = (uint16_t)ConfigUtil->ice_port;
    m_username = ConfigUtil->ice_username.toStdString();
    m_password = ConfigUtil->ice_password.toStdString();
    m_networkPath = ConfigUtil->remote_network_path;
    m_mediaTopology = ConfigUtil->remote_media_topology;
    m_qualityProfile = ConfigUtil->remote_quality_profile;
    m_requestedWidth = ConfigUtil->remote_width;
    m_requestedHeight = ConfigUtil->remote_height;
    if (!m_isOnlyFile && (m_requestedWidth <= 0 || m_requestedHeight <= 0))
    {
        const QSize automaticSize = defaultRemoteVideoConstraint();
        m_requestedWidth = automaticSize.width();
        m_requestedHeight = automaticSize.height();
        LOG_INFO("Automatic remote desktop resolution constraint selected: {}x{}",
                 m_requestedWidth, m_requestedHeight);
    }

    m_filePacketUtil = std::make_unique<FilePacketUtil>(this);
    m_callbackDispatcher = new QtCallbackDispatcher(this);
    connectFilePacketUtilSignals();

    m_reconnectTimer = new QTimer(this);
    m_controlHeartbeatTimer = new QTimer(this);
    m_sessionHeartbeatTimer = new QTimer(this);
    m_inputMoveFlushTimer = new QTimer(this);
    m_inputMoveTailTimer = new QTimer(this);
    m_inputMoveFlushTimer->setSingleShot(true);
    m_inputMoveTailTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &WebRtcCtl::doReconnect);
    connect(m_controlHeartbeatTimer, &QTimer::timeout, this, &WebRtcCtl::sendControlHeartbeat);
    connect(m_sessionHeartbeatTimer, &QTimer::timeout, this, &WebRtcCtl::pollSessionHeartbeat);
    connect(m_inputMoveFlushTimer, &QTimer::timeout, this, &WebRtcCtl::flushPendingInputMove);
    connect(m_inputMoveTailTimer, &QTimer::timeout, this, &WebRtcCtl::finishInputMoveBurst);

    LOG_INFO("created for remote: {}, sessionId={}", m_remoteId, m_sessionId);
}

void WebRtcCtl::setSessionLabel(const QString &label)
{
    m_sessionLabel = label;
}

void WebRtcCtl::connectFilePacketUtilSignals()
{
    if (!m_filePacketUtil)
        return;

    connect(m_filePacketUtil.get(), &FilePacketUtil::fileDownloadCompleted,
            this, &WebRtcCtl::recvDownloadFile);
    connect(m_filePacketUtil.get(), &FilePacketUtil::fileDownloadCompleted,
            this, [this](bool status, const QString &path) {
                noteClipboardDownloadResult(path, status);
            });
    connect(m_filePacketUtil.get(), &FilePacketUtil::fileReceived,
            this, &WebRtcCtl::recvDownloadFile);
}

WebRtcCtl::~WebRtcCtl()
{
    LOG_DEBUG("destructor");
    if (!m_shutdownDone && QThread::currentThread() == thread())
        performShutdown();
}

void WebRtcCtl::performShutdown()
{
    if (m_shutdownDone)
        return;

    LOG_DEBUG("WebRtcCtl shutdown");
    m_shutdownDone = true;
    disableReconnect();
    if (m_controlHeartbeatTimer)
        m_controlHeartbeatTimer->stop();
    if (m_sessionHeartbeatTimer)
        m_sessionHeartbeatTimer->stop();
    destroy();
}

void WebRtcCtl::shutdown()
{
    performShutdown();
    emit shutdownFinished();
}

void WebRtcCtl::shutdownAndMoveToOwnerThread(QObject *owner)
{
    notifyLocalDisconnect();
    performShutdown();
    if (owner)
        moveToThread(owner->thread());
    emit shutdownFinished();
}

void WebRtcCtl::init()
{
    if (m_shutdownDone)
        return;
    m_shutdownStarted.store(false);
    m_callbackLifetime = std::make_shared<CallbackLifetime>();
    m_heartbeatNegotiated.store(false);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastSessionInboundMs.store(now);
    m_lastSessionOutboundMs.store(now);
    m_lastSessionProgressMs.store(now);
    m_lastBufferedAmount = 0;
    LOG_INFO("Creating PeerConnection for control side, sessionId={}, label={}", m_sessionId, m_sessionLabel);
    emit connectionStatusChanged(tr("Control WebRTC connection created"));
    m_availableNetworkPaths = QStringList() << QStringLiteral("auto");
    m_selectedNetworkPath.clear();
    publishNetworkPathState();

    initPeerConnection();
    if (!m_peerConnection)
    {
        LOG_ERROR("Control init aborted: PeerConnection is not available");
        scheduleReconnect();
        return;
    }
    emit connectionStatusChanged(tr("Controller codec initialization complete: %1")
                                     .arg(summarizeVideoCodecCapabilities(buildLocalVideoCodecCapabilitiesJson(), this)));
    if (!m_isOnlyFile)
    {
        createTracks();
        if (!m_videoTrack)
        {
            LOG_ERROR("Control init aborted: video track was not created");
            scheduleReconnect();
            return;
        }
    }

    setupCallbacks();

    const int requestedFps = ConfigUtil->fps;

    auto connectBuilder = JsonUtil::createObject()
                              .add(Constant::KEY_ROLE, Constant::ROLE_CTL)
                              .add(Constant::KEY_TYPE, Constant::TYPE_CONNECT)
                              .add(Constant::KEY_RECEIVER, m_remoteId)
                              .add(Constant::KEY_RECEIVER_PWD, m_remotePwdMd5)
                              .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                              .add(Constant::KEY_SESSION_ID, m_sessionId)
                              .add(Constant::KEY_LABEL_NAME, m_sessionLabel)
                              .add(Constant::KEY_IS_ONLY_FILE, m_isOnlyFile)
                              .add(Constant::KEY_WIDTH, m_requestedWidth)
                              .add(Constant::KEY_HEIGHT, m_requestedHeight)
                              .add(Constant::KEY_NETWORK_PATH, m_networkPath)
                              .add(Constant::KEY_MEDIA_TOPOLOGY, m_mediaTopology)
                              .add(Constant::KEY_QUALITY_PROFILE, m_qualityProfile)
                              .add(Constant::KEY_AUDIO_MODE, m_audioMode)
                              .add(Constant::KEY_FPS, requestedFps)
                              .add(Constant::KEY_ENABLE_WGC_CAPTURE, ConfigUtil->enable_wgc_capture)
                              .add(Constant::KEY_ENABLE_DXGI_CAPTURE, ConfigUtil->enable_dxgi_capture)
                              .add(Constant::KEY_ENABLE_DXGI_NATIVE_GPU_CAPTURE, ConfigUtil->enable_dxgi_native_gpu_capture);

    QJsonObject connectMsg = connectBuilder.build();
    LOG_INFO("Sending CONNECT message with initial stream constraints: networkPath={}, mediaTopology={}, qualityProfile={}, maxFps={}, wgc={}, dxgi={}, dxgiNativeGpu={}",
             m_networkPath,
             m_mediaTopology,
             m_qualityProfile,
             requestedFps,
             ConfigUtil->enable_wgc_capture,
             ConfigUtil->enable_dxgi_capture,
             ConfigUtil->enable_dxgi_native_gpu_capture);
    emit connectionStatusChanged(tr("Connection request sent to remote"));
    QString message = JsonUtil::toCompactString(connectMsg);
    emit sendWsCliTextMsg(message);
}
