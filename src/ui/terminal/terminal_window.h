#ifndef TERMINAL_WINDOW_H
#define TERMINAL_WINDOW_H

#include <atomic>
#include <QByteArray>
#include <QThread>
#include <QWidget>
#include <QPointer>
#include "webrtc/ctl/webrtc_ctl.h"
#include "websocket/ws_cli.h"

class NativeTerminalWidget;
class TerminalFilePanel;
class QSplitter;
class QCheckBox;
class QLabel;
class QCloseEvent;
class QTimer;
class TerminalLogWriter;

namespace Ui
{
    class TerminalWindow;
}

class TerminalWindow : public QWidget
{
    Q_OBJECT
public:
    explicit TerminalWindow(QString remoteId, QString remotePwdMd5, WsCli *wsCli, QWidget *parent = nullptr);
    ~TerminalWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    bool isClosing() const;
    void beginAsyncShutdown();
    void finalizeCloseWhenStopped();
    void initUI();
    void initCLI();
    void tryStartTerminal();
    void sendTerminalStart(bool requestFallback);
    void updateTerminalTitle(const QString &title);
    void sendTerminalResize(const QSize &gridSize);
    void sendTerminalInput(const QByteArray &data);
    void requestFileList(const QString &path);
    void requestDownload(const QString &remotePath, const QString &localPath, bool isDirectory, const QString &transferId);
    void requestUpload(const QString &localPath, const QString &remotePath, bool isDirectory, const QString &transferId);
    void injectPathTracking();
    void setTerminalAutoSave(bool enabled);
    bool openTerminalLogFile();
    void closeTerminalLogFile(bool finishThread = false);
    void appendTerminalLogMarker(const QString &marker);
    void updateTerminalLogUi();

private slots:
    void onFileTextChannelOpened();
    void onTerminalSessionHealthChanged(int state, const QString &message);
    void onFilePanelSessionHealthChanged(int state, const QString &message);
    void onRemoteDisconnectRequested(const QString &reason, bool peerWide);
    void onTerminalInfo(const QString &osName, const QString &shellPath, const QString &mode,
                        bool pathTracking, bool pathTrackingReady, const QString &requestId);
    void onTerminalClosed(int exitCode, const QString &requestId);
    void onTerminalError(const QString &message, const QString &requestId);
    void onTerminalStartTimeout();
    void onAutoSaveLogToggled(bool checked);
    void onTerminalLogError(const QString &message);
    void onTerminalLogOpenFinished(bool ok, const QString &errorMessage);

private:
    QString m_remoteId;
    QString m_remotePwdMd5;
    QString m_instanceId;
    WebRtcCtl m_rtcCtl;
    QPointer<WsCli> m_ws;
    QThread m_rtcThread;
    NativeTerminalWidget *m_terminal = nullptr;
    TerminalFilePanel *m_filePanel = nullptr;
    QCheckBox *m_autoSaveLogCheck = nullptr;
    QLabel *m_autoSaveLogPathLabel = nullptr;
    QThread m_terminalLogThread;
    TerminalLogWriter *m_terminalLogWriter = nullptr;
    QString m_terminalLogPath;
    bool m_channelReady = false;
    bool m_started = false;
    bool m_terminalFallbackRequested = false;
    bool m_terminalLegacyResponseMode = false;
    quint64 m_terminalStartGeneration = 0;
    QString m_terminalStartRequestId;
    QString m_pendingFileListRequestId;
    QTimer *m_terminalStartTimer = nullptr;
    bool m_terminalLogEnabled = false;
    QString m_remoteOs;
    QString m_remoteShell;
    QString m_remoteTerminalMode;
    bool m_remotePathTracking = true;
    std::atomic_bool m_closing{false};
    bool m_shutdownComplete{false};
    Ui::TerminalWindow *ui{nullptr};

signals:
    void initRtcCtl();
    void fileTextChannelSendMsg(const rtc::message_variant &data);
    void filePanelTextChannelSendMsg(const rtc::message_variant &data);
    void uploadFile2CLI(const QString &ctlPath, const QString &cliPath, const QString &transferId);
    void terminalConsumerBacklogChanged(qint64 bytes);
};

#endif /* TERMINAL_WINDOW_H */
