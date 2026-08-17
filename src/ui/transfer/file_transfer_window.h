#ifndef FILETRANSFERTOOL_H
#define FILETRANSFERTOOL_H

#include <atomic>
#include <QWidget>
#include <QPointer>
#include <QUrl>
#include <QList>
#include <QLabel>
#include <QStyle>
#include <QStringList>
#include <QDir>
#include <QIcon>
#include <QTableWidgetItem>
#include <QPoint>
#include <QMouseEvent>
#include <QHash>
#include "common/constant.h"
#include "webrtc/ctl/webrtc_ctl.h"
#include "websocket/ws_cli.h"

class QFileInfo;
class QComboBox;
class QGroupBox;
class QEvent;
class QKeyEvent;
class QEventLoop;
class QPushButton;
class QTableWidget;
class QObject;

namespace Ui
{
    class FileTransferWindow;
}

class QCloseEvent;

class FileTransferWindow : public QWidget
{
    Q_OBJECT
public:
    
    explicit FileTransferWindow(QString remoteId, QString remotePwdMd5, WsCli *_ws_cli,
                                QWidget *parent = nullptr);
    ~FileTransferWindow();
    
    void initUI();
    
    void initCLI();
    
    void keyPressEvent(QKeyEvent *event) override;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
signals:
    
    void inputChannelSendMsg(const rtc::message_variant &data);
    
    void fileChannelSendMsg(const rtc::message_variant &data);
    
    void fileTextChannelSendMsg(const rtc::message_variant &data);
    
    void initRtcCtl();
    
    void uploadFile2CLI(const QString &ctlPath, const QString &cliPath, const QString &transferId);
    void cancelFileTransfer(const QString &transferId);
protected:
    void closeEvent(QCloseEvent *event) override;

public slots:
    
    void onUploadButtonClicked();
    
    void onDownloadButtonClicked();

    void onLocalDeleteButtonClicked();

    void onRemoteDeleteButtonClicked();
    
    void recvGetFileList(const QJsonObject &object);
    
    void recvDownloadFile(bool status, const QString &filePath);
    void recvUploadFileRes(bool status, const QString &filePath, const QString &errorMessage);
    void recvDeleteFileRes(bool status, const QString &filePath, const QString &errorMessage);
    void recvRenameFileRes(bool status, const QString &filePath, const QString &errorMessage);
    void recvCreateFileRes(bool status, const QString &filePath, bool isDirectory, const QString &errorMessage);
    void onTransferStarted(const QString &transferId, const QString &sourcePath, const QString &targetPath, const QString &operation);
    void onTransferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes, int transferredFiles, int totalFiles);
private slots:
    
    void on_localPathCombo_textActivated(const QString &path);
    
    void on_localTable_cellDoubleClicked(int row, int column);

    void on_remotePathCombo_textActivated(const QString &path);
    
    void on_remoteTable_cellDoubleClicked(int row, int column);
    void onLocalTableContextMenuRequested(const QPoint &pos);
    void onRemoteTableContextMenuRequested(const QPoint &pos);
    void onLocalFilesDropped(const QList<QUrl> &urls);
    void onRemoteFilesDropped(const QList<QUrl> &urls);
    void onTransferCancelClicked();
    void onFileTextChannelOpened();
    void onSessionHealthChanged(int state, const QString &message);
    void onRemoteDisconnectRequested(const QString &reason, bool peerWide);

private:
    bool isClosing() const;
    struct TransferBatchItem
    {
        QString sourcePath;
        QString destinationPath;
        bool isDirectory{false};
        qint64 totalBytes{0};
        int totalFiles{0};
    };

    struct TransferTask
    {
        QString transferId;
        QString sourcePath;
        QString destinationPath;
        QString operation;
        qint64 transferredBytes{0};
        qint64 totalBytes{0};
        int transferredFiles{0};
        int totalFiles{0};
        int row{-1};
        bool canceled{false};
        bool completed{false};
        QString batchParentId;
        int batchItemIndex{0};
        int batchExpectedItems{0};
        int batchCompletedItems{0};
        bool batchFailed{false};
        bool directory{false};
        int currentFile{0};
        QLabel *progressLabel{nullptr};
        QPushButton *cancelButton{nullptr};
    };

    
    void populateLocalFiles();
    
    void populateRemoteFiles();
    
    void setupFileTables();
    
    void setupLogTable();
    bool isExecutableFileName(const QString &fileName) const;
    bool isLocalExecutableFile(const QFileInfo &fileInfo) const;
    bool isRemoteExecutableFile(const QTableWidgetItem *item) const;
    void requestRunFile(bool remoteSide, const QString &filePath);
    void deleteSelectedLocalFiles();
    void deleteSelectedRemoteFiles();
    void renameSelectedLocalFile();
    void renameSelectedRemoteFile();
    bool removeLocalPath(const QString &path, QString *errorMessage) const;
    void requestRemoteDelete(const QString &path);
    void requestRemoteRename(const QString &path, const QString &newName);
    void requestRemoteCreate(const QString &path, bool isDirectory);
    void createRemoteItem(bool isDirectory);
    void requestRemoteDownload(const QString &remotePath, const QString &localPath, bool isDirectory);
    void requestRemoteUpload(const QString &localPath, const QString &remotePath);
    void requestRemoteDownloadBatch(const QList<TransferBatchItem> &items);
    void requestRemoteUploadBatch(const QList<TransferBatchItem> &items);
    QString localDropTargetPath(const QList<QUrl> &urls) const;
    QStringList urlsToLocalPaths(const QList<QUrl> &urls) const;
    void startDownloadFromRemoteSelection(const QString &localBaseDir);
    void startUploadFromLocalSelection();
    void startRemoteDragFromSelection();
    QString createRemoteDragTempDir() const;
    
    void updateLocalPathCombo();
    
    void updateRemotePathCombo();
    void requestRemoteFileList(const QString &path);
    QString createTransferId() const;
    qint64 collectDirectoryStats(const QString &path, int *fileCount) const;
    TransferTask *findTransferTaskById(const QString &transferId);
    TransferTask *findTransferTaskByPath(const QString &path);
    void registerTransferPath(const QString &path, const QString &transferId);
    void unregisterTransferPaths(const TransferTask &task);
    int ensureTransferTaskRow(const TransferTask &task);
    void updateTransferTaskUi(TransferTask &task);
    void finishTransferTask(const QString &transferId, bool status, const QString &filePath);
    void cancelTransferTask(const QString &transferId);
    void updateProgressCell(TransferTask &task);
    void updateBatchTask(TransferTask &task);
    void failActiveTransfers(const QString &reason);

private:
    void beginAsyncShutdown();
    void finalizeCloseWhenStopped();

    Ui::FileTransferWindow *ui;
    bool connected;
    QLabel label;
    QString remote_id;
    QString remote_pwd_md5;
    QString m_instanceId;
    WebRtcCtl m_rtc_ctl;
    QPointer<WsCli> m_ws;
    QThread m_rtc_ctl_thread;

    QDir currentLocalDir;
    QString currentRemotePath; 
    QString m_pendingFileListRequestId;
    QString m_pendingFileListRequestPath;
    QIcon dirIcon;
    QIcon fileIcon;

    QJsonArray remoteFiles;
    QHash<QString, TransferTask> m_transferTasks;
    QHash<QString, QStringList> m_transferPathIds;
    QPoint m_localDragStartPos;
    QPoint m_remoteDragStartPos;
    std::atomic_bool m_closing{false};
    bool m_shutdownComplete{false};
};

#endif /* FILETRANSFERTOOL_H */
