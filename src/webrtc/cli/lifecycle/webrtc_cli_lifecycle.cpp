#include "webrtc/cli/webrtc_cli.h"
#include "common/constant.h"
#include "util/file/file_packet_util.h"
#include "util/qt/qt_callback_util.h"
#include "security/audit_session.h"
#include "security/terminal_command_audit_parser.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QJsonObject>
#include <QMessageBox>
#include <QScreen>
#include <QTimer>
#include <QUuid>

namespace
{

QString normalizeInitialAudioMode(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized == QStringLiteral("listen") || normalized == QStringLiteral("call"))
        return normalized;
    return QStringLiteral("off");
}

QSize physicalScreenSize(QScreen *screen)
{
    if (!screen)
        return QSize(1920, 1080);
    const QRect geometry = screen->geometry();
    const qreal dpr = screen->devicePixelRatio();
    return QSize(qMax(1, qRound(geometry.width() * dpr)),
                 qMax(1, qRound(geometry.height() * dpr)));
}
}


WebRtcCli::WebRtcCli(const QString &remoteId, int fps, bool isOnlyFile, const QString &sessionId, QObject *parent)
    : WebRtcCli(remoteId, fps, isOnlyFile, -1, -1, QStringLiteral("auto"), QStringLiteral("off"), sessionId, parent)
{
}


WebRtcCli::WebRtcCli(const QString &remoteId, int fps, bool isOnlyFile, int requestedWidth, int requestedHeight,
                     const QString &networkPath, const QString &initialAudioMode, const QString &sessionId, QObject *parent)
    : WebRtcCli(remoteId, fps, isOnlyFile, requestedWidth, requestedHeight, networkPath, QStringLiteral("p2p"), QStringLiteral("auto"), initialAudioMode, sessionId, std::shared_ptr<AuditSession>(), parent)
{
}

WebRtcCli::WebRtcCli(const QString &remoteId, int fps, bool isOnlyFile, int requestedWidth, int requestedHeight,
                     const QString &networkPath, const QString &mediaTopology,
                     const QString &qualityProfile, const QString &initialAudioMode, const QString &sessionId,
                     std::shared_ptr<AuditSession> auditSession, QObject *parent)
    : QObject(parent),
      m_remoteId(remoteId),
      m_subscriberId(QUuid::createUuid().toString().remove("{").remove("}")),
      m_sessionId(sessionId),
      m_auditSession(std::move(auditSession)),
      m_isOnlyFile(isOnlyFile), 
      m_currentDir(QDir::home()),
      m_connected(false),
      m_channelsReady(false),
      m_destroying(false),
      m_fps(fps),
      m_subscribed(false)
{
    m_controlledSessionMode = m_isOnlyFile ? QStringLiteral("file")
                                           : QStringLiteral("desktop");
    if (!m_isOnlyFile && m_fps > 30)
        m_adaptiveVideoFpsCap = 30;

    QGuiApplication *guiApp = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    QScreen *screen = guiApp ? guiApp->primaryScreen() : nullptr;
    m_screenIndex = 0;
    m_currentDesktopSourceIndex = desktopSourceIndexForScreenIndex(m_screenIndex);
    m_screenId = currentScreenId();
    m_currentDesktopSourceRect = desktopSourceRectForScreenIndex(m_screenIndex);
    const QSize screenSize = m_currentDesktopSourceRect.isValid()
                                 ? m_currentDesktopSourceRect.size()
                                 : physicalScreenSize(screen);
    m_screen_width = screenSize.width();
    m_screen_height = screenSize.height();
    LOG_INFO("Initial desktop capture screen selected: primary index={}, size={}x{}",
             m_screenIndex, m_screen_width, m_screen_height);
    m_requestedEncodeWidth = requestedWidth;
    m_requestedEncodeHeight = requestedHeight;
    m_baseRequestedEncodeWidth = requestedWidth;
    m_baseRequestedEncodeHeight = requestedHeight;
    calculateEncodeResolution(m_requestedEncodeWidth, m_requestedEncodeHeight);
    m_networkPath = networkPath.isEmpty() ? QStringLiteral("auto") : networkPath;
    m_mediaTopology = mediaTopology.trimmed().toLower() == QStringLiteral("sfu")
                          ? QStringLiteral("sfu")
                          : QStringLiteral("p2p");
    const QString normalizedQualityProfile = qualityProfile.trimmed().toLower();
    m_autoQualityProfile = normalizedQualityProfile.isEmpty() ||
                           normalizedQualityProfile == QStringLiteral("auto");
    if (m_autoQualityProfile)
        m_qualityProfile = QStringLiteral("weak_clear");
    else if (normalizedQualityProfile == QStringLiteral("balanced"))
        m_qualityProfile = QStringLiteral("balanced");
    else if (normalizedQualityProfile == QStringLiteral("weak") ||
             normalizedQualityProfile == QStringLiteral("weak_clear") ||
             normalizedQualityProfile == QStringLiteral("lowbandwidth") ||
             normalizedQualityProfile == QStringLiteral("clear"))
        m_qualityProfile = QStringLiteral("weak_clear");
    else
        m_qualityProfile = QStringLiteral("lan_hd");

    const QString normalizedInitialAudioMode = normalizeInitialAudioMode(initialAudioMode);
    if (!m_isOnlyFile && normalizedInitialAudioMode != QStringLiteral("off") &&
        (!RuntimeEnvironment::uiAvailable() || !QApplication::instance()))
    {
        m_audioMode = normalizedInitialAudioMode;
        LOG_INFO("Initial audio mode accepted without prompt: {}", m_audioMode);
    }
    else if (!m_isOnlyFile && normalizedInitialAudioMode != QStringLiteral("off"))
    {
        LOG_INFO("Initial audio mode {} requires UI consent; starting without audio track", normalizedInitialAudioMode);
    }

    
    m_host = ConfigUtil->ice_host.toStdString();
    m_port = (uint16_t)ConfigUtil->ice_port;
    m_username = ConfigUtil->ice_username.toStdString();
    m_password = ConfigUtil->ice_password.toStdString();

    
    m_filePacketUtil = new FilePacketUtil(this);
    m_callbackDispatcher = new QtCallbackDispatcher(this);

    
    connect(m_filePacketUtil, &FilePacketUtil::fileDownloadCompleted, this,
            [this](bool status, const QString &path) {
                handleFileReceived(status, path, QString());
            });
    connect(m_filePacketUtil, &FilePacketUtil::fileReceived, this, &WebRtcCli::handleFileReceived);

    
    m_inputChannelRecoverTimer = new QTimer(this);
    m_inputChannelRecoverTimer->setSingleShot(true);
    connect(m_inputChannelRecoverTimer, &QTimer::timeout, this, &WebRtcCli::recoverInputChannel);

    m_streamConfigApplyTimer = new QTimer(this);
    m_streamConfigApplyTimer->setSingleShot(true);
    connect(m_streamConfigApplyTimer, &QTimer::timeout, this, &WebRtcCli::applyPendingStreamConfig);

    m_streamConfigNotifyTimer = new QTimer(this);
    m_streamConfigNotifyTimer->setSingleShot(true);
    connect(m_streamConfigNotifyTimer, &QTimer::timeout, this, &WebRtcCli::notifyCurrentStreamConfig);

    m_controlWatchdogTimer = new QTimer(this);
    connect(m_controlWatchdogTimer, &QTimer::timeout, this, &WebRtcCli::checkControlAlive);
    m_sessionHeartbeatTimer = new QTimer(this);
    connect(m_sessionHeartbeatTimer, &QTimer::timeout, this, &WebRtcCli::pollSessionHeartbeat);
    m_disconnectGraceTimer = new QTimer(this);
    m_disconnectGraceTimer->setSingleShot(true);
    connect(m_disconnectGraceTimer, &QTimer::timeout, this, &WebRtcCli::stopMediaCapture);
    m_mediaStatsTimer = new QTimer(this);
    connect(m_mediaStatsTimer, &QTimer::timeout, this, &WebRtcCli::queryMediaStats);
    m_clipboardSnapshotTimer = new QTimer(this);
    m_clipboardSnapshotTimer->setSingleShot(true);
    connect(m_clipboardSnapshotTimer, &QTimer::timeout, this, [this]() {
        sendLocalClipboardSnapshot();
    });
    connect(this, &WebRtcCli::audioCapturePromptRequested, QCoreApplication::instance(),
            [this](const QString &requestId, const QString &requestedMode, const QString &modeLabel) {
                if (!RuntimeEnvironment::uiAvailable() || !QApplication::instance())
                {
                    emit audioCaptureDecisionRequested(requestId,
                                                       requestedMode,
                                                       true,
                                                       QCoreApplication::translate("WebRtcCli", "Accepted without prompt."));
                    return;
                }

                const QString title = QCoreApplication::translate("WebRtcCli", "Remote audio request");
                const QString text = QCoreApplication::translate("WebRtcCli", "The controller requests the '%1' audio mode. Allow it?").arg(modeLabel);

                QMessageBox box(QMessageBox::Question,
                                title,
                                text,
                                QMessageBox::Yes | QMessageBox::No,
                                nullptr,
                                Qt::Dialog | Qt::WindowStaysOnTopHint);
                box.setDefaultButton(QMessageBox::No);
                box.setWindowModality(Qt::ApplicationModal);
                box.show();
                box.raise();
                box.activateWindow();

                const QMessageBox::StandardButton result =
                    static_cast<QMessageBox::StandardButton>(box.exec());
                const bool accepted = result == QMessageBox::Yes;
                const QString message = accepted
                                            ? QCoreApplication::translate("WebRtcCli", "The remote device accepted audio.")
                                            : QCoreApplication::translate("WebRtcCli", "The remote device rejected audio.");
                emit audioCaptureDecisionRequested(requestId, requestedMode, accepted, message);
            });
    connect(this, &WebRtcCli::audioCaptureDecisionRequested, this, &WebRtcCli::applyAudioCaptureDecision);

    LOG_INFO("created for remote: {}, subscriber: {}, mediaTopology={}, qualityProfile={}, autoQuality={}",
             m_remoteId, m_subscriberId, m_mediaTopology, m_qualityProfile, m_autoQualityProfile);
}

void WebRtcCli::setControlledSessionMode(const QString &mode)
{
    if (!mode.isEmpty())
        m_controlledSessionMode = mode;
}


WebRtcCli::~WebRtcCli()
{
    LOG_DEBUG("WebRtcCli destructor");
    performDestroy();
}


void WebRtcCli::init()
{
    LOG_INFO("Creating PeerConnection and tracks for client side");
    m_heartbeatNegotiated.store(false);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastSessionInboundMs.store(now);
    m_lastSessionOutboundMs.store(now);
    m_lastSessionProgressMs.store(now);
    m_lastBufferedAmount = 0;
    
    initPeerConnection();
    if (!m_peerConnection)
    {
        LOG_ERROR("Client init aborted: PeerConnection is not available");
        sendSignalingError(tr("Remote initialization aborted: PeerConnection unavailable"));
        emit destroyCli();
        return;
    }

    setupCallbacks();
    
    createTracksAndChannels();
    if (!m_isOnlyFile && (!m_videoTrack || !m_inputChannel || (m_audioMode != QStringLiteral("off") && !m_audioTrack)))
    {
        LOG_ERROR("Client init aborted: media tracks or input channel were not created");
        QStringList missing;
        if (!m_videoTrack)
            missing.append(tr("video track"));
        if (m_audioMode != QStringLiteral("off") && !m_audioTrack)
            missing.append(tr("audio track"));
        if (!m_inputChannel)
            missing.append(tr("input channel"));
        sendSignalingError(tr("Remote initialization aborted: %1 not created").arg(missing.join(QStringLiteral(", "))));
        emit destroyCli();
        return;
    }
    if (!m_fileChannel || !m_fileTextChannel)
    {
        LOG_ERROR("Client init aborted: file data channels were not created");
        QStringList missing;
        if (!m_fileChannel)
            missing.append(tr("file channel"));
        if (!m_fileTextChannel)
            missing.append(tr("file text channel"));
        sendSignalingError(tr("Remote initialization aborted: %1 not created").arg(missing.join(QStringLiteral(", "))));
        emit destroyCli();
        return;
    }
    m_peerConnection->createOffer();
}
