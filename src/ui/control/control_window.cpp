#include "ui/control/control_window.h"

#include "common/logger_manager.h"
#include "ui/video/d3d11_video_widget.h"
#include "ui_control_window.h"
#include "util/config/config_util.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QUuid>
#include <QToolButton>


ControlWindow::ControlWindow(QString remoteId, QString remotePwdMd5, WsCli *_ws_cli, QWidget *parent)
    : QMainWindow(parent), isReceivedImg(false), windowSizeAdjusted(false),
      remote_id(remoteId), remote_pwd_md5(remotePwdMd5),
      m_instanceId(QUuid::createUuid().toString().remove(QLatin1Char('{')).remove(QLatin1Char('}'))),
      m_rtc_ctl(remoteId, remotePwdMd5, false), m_ws(_ws_cli),
      m_floatingToolbar(nullptr), m_screenshotBtn(nullptr), m_switchScreenBtn(nullptr),
      m_remoteOperationBtn(nullptr), m_fileTransferBtn(nullptr), m_transferRecordBtn(nullptr), m_audioCaptureBtn(nullptr),
      m_statsLabel(nullptr), m_transferRecordDialog(nullptr), m_transferRecordTable(nullptr),
      m_fitToWindow(false), m_fpsFrameCount(0), m_currentFps(0.0), m_currentKbps(0.0),
      m_cachedScaledTargetSize(), m_cachedScaledPixmapSize(),
      m_networkPath(ConfigUtil->remote_network_path),
      m_audioMode(QStringLiteral("off")),
      m_draggingToolbar(false)
{
    m_rtc_ctl.setSessionLabel(QStringLiteral("control-%1").arg(m_instanceId));
    m_transferStatusTimer = new QTimer(this);
    m_transferStatusTimer->setSingleShot(true);
    connect(m_transferStatusTimer, &QTimer::timeout, this, &ControlWindow::clearTransferStatus);

    initUI();
    connectLocalClipboardMonitor();
    resetConnectionProgress();
    initCLI();
    createFloatingToolbar();
    emit initRtcCtl();
}


ControlWindow::~ControlWindow()
{
    LOG_DEBUG("ControlWindow destructor started, instanceId={}", m_instanceId);
    m_closing.store(true);
    Q_ASSERT(!m_rtc_ctl_thread.isRunning());

    qApp->removeEventFilter(this);

    if (m_floatingToolbar)
    {
        m_floatingToolbar->hide();
        delete m_floatingToolbar;
        m_floatingToolbar = nullptr;
    }

    if (scrollArea.widget() == &label)
        scrollArea.takeWidget();
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (m_d3d11VideoWidget && scrollArea.widget() == static_cast<QWidget *>(m_d3d11VideoWidget))
        scrollArea.takeWidget();
#endif
    label.setParent(nullptr);
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    delete m_d3d11VideoWidget;
    m_d3d11VideoWidget = nullptr;
#endif

    LOG_DEBUG("ControlWindow destructor finished, instanceId={}", m_instanceId);
    delete ui;
}

void ControlWindow::beginAsyncShutdown()
{
    if (m_closing.exchange(true))
        return;

    releaseRemotePressedKeys();
    qApp->removeEventFilter(this);
    hide();
    if (m_ws)
        disconnect(m_ws, nullptr, &m_rtc_ctl, nullptr);
    disconnect(this, nullptr, &m_rtc_ctl, nullptr);
    disconnect(&m_rtc_ctl, nullptr, this, nullptr);
    {
        QMutexLocker locker(&m_videoFrameMutex);
        m_pendingVideoFrame = QImage();
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
        m_pendingD3D11VideoFrame = rtc::D3D11VideoFrame{};
#endif
        m_pendingVideoFrameKind = 0;
        m_videoFrameDrainScheduled = false;
    }

    if (m_rtc_ctl_thread.isRunning())
    {
        QMetaObject::invokeMethod(&m_rtc_ctl,
                                  "shutdownAndMoveToOwnerThread",
                                  Qt::QueuedConnection,
                                  Q_ARG(QObject *, this));
    }
    finalizeCloseWhenStopped();
}

void ControlWindow::finalizeCloseWhenStopped()
{
    if (!m_closing.load() || m_rtc_ctl_thread.isRunning())
        return;
    m_shutdownComplete = true;
    QTimer::singleShot(0, this, [this]() { close(); });
}

bool ControlWindow::isClosing() const
{
    return m_closing.load();
}
