#ifndef CONTROL_WINDOW_H
#define CONTROL_WINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QMutex>
#include <QPointer>
#include <atomic>
#include <QWheelEvent>
#include <QScrollArea>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QFrame>
#include <QApplication>
#include <QClipboard>
#include <QButtonGroup>
#include <QElapsedTimer>
#include <QPixmap>
#include <QActionGroup>
#include <QMenu>
#include <QList>
#include <QHash>
#include <QByteArray>
#include <QStringList>
#include <QJsonArray>
#include <QDateTime>
#include "common/constant.h"
#include "webrtc/ctl/webrtc_ctl.h"
#include "websocket/ws_cli.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
    class ControlWindow;
}
class QKeyEvent;
class QMouseEvent;
class QResizeEvent;
class QCloseEvent;
class QEvent;
class QBoxLayout;
class QAction;
class QToolButton;
class QVBoxLayout;
class QDialog;
class QTableWidget;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
class D3D11VideoWidget;
#endif


class ControlWindow : public QMainWindow
{
    Q_OBJECT
public:
    
    ControlWindow(QString remoteId, QString remotePwdMd5, WsCli *_ws_cli, QWidget *parent = nullptr);

    
    ~ControlWindow();

    
    void initUI();

    
    void initCLI();

    
    void keyPressEvent(QKeyEvent *event) override;

    
    void keyReleaseEvent(QKeyEvent *event) override;

    
    void mouseDoubleClickEvent(QMouseEvent *event) override;

    
    void wheelEvent(QWheelEvent *event) override;

    
    void resizeEvent(QResizeEvent *event) override;

    
    QPointF getNormPoint(const QPoint &pos);

    
    bool isValidNormPoint(const QPointF &pos) const;

    
    void createFloatingToolbar();

    
    void createAndroidNavigationPanel();

    
    void updateToolbarPosition();

protected:
    void closeEvent(QCloseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

public:
    struct TransferRecord
    {
        QString transferId;
        QString sourcePath;
        QString targetPath;
        QString operation;
        qint64 transferredBytes{0};
        qint64 totalBytes{0};
        int transferredFiles{0};
        int totalFiles{0};
        QString status;
        bool finished{false};
        QDateTime updatedAt;
    };

private:
    bool isClosing() const;
    void beginAsyncShutdown();
    void finalizeCloseWhenStopped();
    bool handleToolbarDragEvent(QObject *watched, QEvent *event);
    bool handleToolbarAutoHideEvent(QObject *watched, QEvent *event);
    void setToolbarAutoHidden(bool hidden);
    void applyToolbarAutoHiddenPosition();
    bool handleAndroidNavigationDragEvent(QObject *watched, QEvent *event);
    bool handleRemoteMouseMoveEvent(QObject *watched, QEvent *event);
    void sendRemoteMouseMoveAt(const QPoint &windowPos);
    bool shouldCaptureRemoteKeyboard() const;
    bool handleRemoteKeyboardEvent(QKeyEvent *event, bool pressed);
    void sendRemoteKeyboardEvent(int winKey, bool pressed);
    void releaseRemotePressedKeys();
    void connectLocalClipboardMonitor();
    void noteLocalClipboardChanged();
    void syncLocalClipboardToRemoteIfNeeded();
    void markLocalClipboardSynced(const QByteArray &signature);
    void enqueueVideoFrame(const QImage &frame);
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    void enqueueD3D11VideoFrame(const rtc::D3D11VideoFrame &frame);
#endif

    bool isReceivedImg;      
    bool windowSizeAdjusted; 
    QScrollArea scrollArea;

    QLabel label;
    QPixmap m_sourcePixmap;
    QPixmap m_scaledPixmap;
    QRect m_videoDisplayRect;
    QSize m_cachedScaledTargetSize;
    QSize m_cachedScaledPixmapSize;
    QMutex m_videoFrameMutex;
    QImage m_pendingVideoFrame;
    bool m_videoFrameDrainScheduled{false};
    int m_pendingVideoFrameKind{0};
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    rtc::D3D11VideoFrame m_pendingD3D11VideoFrame;
    D3D11VideoWidget *m_d3d11VideoWidget{nullptr};
    bool m_useD3D11Video{true};
#endif

    QString remote_id;
    QString remote_pwd_md5;
    QString m_instanceId;
    WebRtcCtl m_rtc_ctl;
    QPointer<WsCli> m_ws;
    QThread m_rtc_ctl_thread;

    
    QFrame *m_floatingToolbar;
    QPushButton *m_screenshotBtn;
    QToolButton *m_switchScreenBtn;
    QToolButton *m_remoteOperationBtn;
    QPushButton *m_fileTransferBtn;
    QPushButton *m_transferRecordBtn{nullptr};
    QToolButton *m_audioCaptureBtn{nullptr};
    QPushButton *m_androidBackBtn{nullptr};
    QPushButton *m_androidHomeBtn{nullptr};
    QPushButton *m_androidRecentsBtn{nullptr};
    QWidget *m_centralHost{nullptr};
    QWidget *m_androidNavHost{nullptr};
    QFrame *m_androidNavPanel{nullptr};
    QWidget *m_toolbarButtonRow{nullptr};
    QBoxLayout *m_toolbarButtonLayout{nullptr};
    QList<QToolButton *> m_sideMenuButtons;
    QLabel *m_statsLabel;
    QDialog *m_transferRecordDialog{nullptr};
    QTableWidget *m_transferRecordTable{nullptr};
    QActionGroup *m_networkActionGroup{nullptr};
    QActionGroup *m_displayActionGroup{nullptr};
    QActionGroup *m_audioActionGroup{nullptr};
    bool m_fitToWindow;
    QElapsedTimer m_fpsTimer;
    int m_fpsFrameCount;
    double m_currentFps;
    double m_currentKbps;
    QSize m_remoteResolution;
    QString m_remoteEncoderName;
    QString m_remoteEncoderType;
    QString m_remoteVideoCodec;
    QString m_remoteCaptureMethod;
    QString m_networkPath{QStringLiteral("auto")};
    bool m_remoteDesktopLocked{false};
    bool m_androidNavigationVisible{false};
    bool m_audioCaptureEnabled{false};
    bool m_suppressClipboardCopyRelease{false};
    bool m_suppressClipboardPasteRelease{false};
    QList<int> m_remotePressedKeys;
    bool m_localClipboardDirty{false};
    bool m_clipboardMonitorConnected{false};
    QTimer *m_transferStatusTimer{nullptr};
    QString m_transferStatusText;
    QString m_audioMode{QStringLiteral("off")};
    QString m_pendingAudioMode;
    QString m_pendingAudioRequestId;
    QString m_remoteOsName;
    QJsonArray m_remoteScreens;
    QString m_remoteScreenId;
    QMenu *m_screenMenu{nullptr};
    QList<int> m_connectionStepStates;
    QString m_connectionFailureReason;
    QHash<QString, TransferRecord> m_transferRecords;
    QHash<QString, QString> m_transferPathToId;
    QByteArray m_lastObservedClipboardSignature;
    QByteArray m_lastSyncedClipboardSignature;
    QByteArray m_pendingRemoteClipboardApplySignature;

    
    bool m_draggingToolbar;
    QPoint m_dragStartPosition;
    QPoint m_toolbarOffset;
    bool m_toolbarInSidePanel{false};
    bool m_toolbarUserMoved{false};
    QTimer *m_toolbarAutoHideTimer{nullptr};
    bool m_toolbarAutoHidden{false};
    QPoint m_toolbarShownPosition;
    bool m_draggingAndroidNav{false};
    QPoint m_androidNavDragStart;
    QPoint m_androidNavStartPos;
    QSize m_windowSize; 
    std::atomic_bool m_closing{false};
    bool m_shutdownComplete{false};
    Ui::ControlWindow *ui{nullptr};
signals:
    void sendMsg2InputChannel(const rtc::message_variant &data);
    void pasteClipboardPayloadToRemote(const QJsonObject &payload);
    void syncClipboardPayloadToRemote(const QJsonObject &payload);
    void requestRemoteClipboardSnapshot();
    void initRtcCtl();
public slots:
    void updateImg(const QImage &img);
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    void updateD3D11Frame(const rtc::D3D11VideoFrame &frame);
#endif
    void updateVideoStats(double kbps, const QSize &resolution);

    
    void onScreenshotClicked();
    void onRemoteOperationTriggered();
    void onFileTransferClicked();
    void onTransferRecordClicked();
    void onAudioCaptureClicked();
    void onAudioModeActionTriggered(QAction *action);
    void onNetworkPathSelected(QAction *action);
    void onDisplayModeSelected(QAction *action);
    void onNetworkPathStateChanged(const QStringList &availablePaths, const QString &selectedPath, const QString &requestedPath);
    void onRemoteEncoderChanged(const QString &encoderName, const QString &encoderType);
    void onRemoteMediaStateChanged(const QString &codec, const QString &captureMethod);
    void onRemoteScreensChanged(const QJsonArray &screens, const QString &currentScreenId);
    void onAudioModeRequestFinished(const QString &requestId, const QString &mode, bool accepted, const QString &message);
    void onRemoteDesktopStateChanged(bool locked, const QString &message);
    void onRemoteOsChanged(const QString &osName);
    void onConnectionStatusChanged(const QString &status);
    void onSessionHealthChanged(int state, const QString &message);
    void onRemoteDisconnectRequested(const QString &reason, bool peerWide);
    void onDownloadFinished(bool status, const QString &filePath);
    void applyLocalClipboardPayload(const QJsonObject &payload);
    void onTransferStarted(const QString &transferId, const QString &sourcePath, const QString &targetPath, const QString &operation);
    void onTransferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes, int transferredFiles, int totalFiles);
    void onTransferFinished(bool status, const QString &filePath, const QString &errorMessage);

private slots:
    void drainPendingVideoFrame();
    void appendConnectionProgress(const QString &status);
    void resetConnectionProgress();
    void renderConnectionProgress();
    void markConnectionWaitingFrameDone();
    void refreshStatsLabel();
    void clearTransferStatus();
    void updateAudioButtonState(const QString &mode, bool pending);
    void sendRemoteKeyTap(int winKey);
    void sendRemoteShortcut(const QList<int> &winKeys);
    void sendSwitchScreenRequest(const QString &screenId);
    void rebuildScreenMenu();
    void restoreScreenshotButtonText();
    void adjustWindowSizeToVideo(const QSize &videoSize); 
    void updateScaledPixmap();
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    bool ensureD3D11VideoWidget();
#endif
    void switchToCpuVideoWidget();
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    void switchToD3D11VideoWidget();
    void updateD3D11VideoGeometry();
#endif
    QAction *addToolbarCheckedAction(QMenu *menu, QActionGroup *group, const QString &text, const QString &data, bool checked = false);
    void setupFloatingToolbarFrame();
    void createToolbarStatsLabel(QVBoxLayout *mainLayout);
    void createToolbarButtonRow();
    QMenu *createQuickActionMenu();
    void createToolbarCoreButtons();
    void createToolbarModeMenus();
    void addToolbarMenuButton(const QString &title, QMenu *menu, QActionGroup *group);
    void sendAndroidNavigation(const QString &action);
    void setAndroidNavigationVisible(bool visible);
    void constrainAndroidNavigationPanel();
    bool isRemotePortrait() const;
    bool shouldPlaceToolbarInSidePanel() const;
    void applyToolbarLayoutMode(bool sidePanelMode);
    void updateAndroidSidePanelWidth();
    void refreshTransferRecordDialog();
};
#endif /* CONTROL_WINDOW_H */
