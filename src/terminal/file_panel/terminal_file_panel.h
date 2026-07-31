#ifndef TERMINAL_FILE_PANEL_H
#define TERMINAL_FILE_PANEL_H

#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QUrl>
#include <QStringList>
#include <QWidget>

class QObject;
class QCheckBox;
class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QProgressBar;
class QTableWidget;
class QTableWidgetItem;
class QToolButton;
class QVBoxLayout;

class TerminalFilePanel : public QWidget
{
    Q_OBJECT
public:
    explicit TerminalFilePanel(QWidget *parent = nullptr);

    QString currentRemotePath() const;

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
    void setConnected(bool connected);
    void setRemotePath(const QString &path);
    void followTerminalPath(const QString &path);
    void recvGetFileList(const QJsonObject &object);
    void recvDownloadFile(bool status, const QString &filePath);
    void recvUploadFile(bool status, const QString &filePath, const QString &errorMessage);
    void recvRenameFile(bool status, const QString &filePath, const QString &errorMessage);
    void recvDeleteFile(bool status, const QString &filePath, const QString &errorMessage);
    void recvCreateFile(bool status, const QString &filePath, bool isDirectory, const QString &errorMessage);
    void onTransferStarted(const QString &transferId, const QString &sourcePath, const QString &targetPath, const QString &operation);
    void onTransferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes, int transferredFiles, int totalFiles);
    void abortTransfers(const QString &reason);

signals:
    void requestFileList(const QString &path);
    void requestDownload(const QString &remotePath, const QString &localPath, bool isDirectory, const QString &transferId);
    void requestUpload(const QString &localPath, const QString &remotePath, bool isDirectory, const QString &transferId);
    void requestRemoteOperation(const QJsonObject &message);
    bool requestRemoteDrag(const QJsonArray &files, const QString &requestId, QString *errorMessage);

private slots:
    void onPathEditingFinished();
    void onParentClicked();
    void onRefreshClicked();
    void onDownloadClicked();
    void onUploadFileClicked();
    void onUploadDirectoryClicked();
    void onDriveChanged(int index);
    void onRemoteCellDoubleClicked(int row, int column);
    void onRemoteTableContextMenuRequested(const QPoint &pos);
    void onFilesDropped(const QList<QUrl> &urls);

private:
    struct TransferBatchItem
    {
        QString sourcePath;
        QString targetPath;
        bool directory{false};
        qint64 totalBytes{0};
        int totalFiles{0};
    };

    struct TransferTask
    {
        QString operation;
        QString sourcePath;
        QString targetPath;
        qint64 transferredBytes{0};
        qint64 totalBytes{0};
        int transferredFiles{0};
        int totalFiles{0};
        bool completed{false};
        bool status{false};
        QString batchParentId;
        int batchItemIndex{0};
        int batchExpectedItems{0};
        int batchCompletedItems{0};
        bool batchFailed{false};
        bool directory{false};
        int currentFile{0};
    };

    void setupUi();
    void applyPanelStyle();
    void createToolbar(QVBoxLayout *layout);
    void createPathEdit(QVBoxLayout *layout);
    void createRemoteTable(QVBoxLayout *layout);
    void createTransferStatus(QVBoxLayout *layout);
    void connectUiSignals();
    void requestRemoteDelete(const QString &path);
    void requestRemoteRename(const QString &path, const QString &newName);
    void requestRemoteCreate(const QString &path, bool isDirectory);
    void requestRemoteRunFile(const QString &path);
    void deleteSelectedRemoteFiles();
    void renameSelectedRemoteFile();
    void createRemoteItem(bool isDirectory);
    void runSelectedRemoteFile();
    void downloadSelectedRemoteFiles();
    void uploadFiles(const QStringList &paths);
    void startRemoteDragFromSelection();
    QStringList selectedRemotePaths() const;
    int scaled(int value) const;
    void refreshDpiMetrics();
    void populateRemoteFiles();
    void updatePathEdit();
    void updateMountedPaths(const QJsonArray &paths);
    void updateDriveCombo();
    QString joinRemotePath(const QString &basePath, const QString &name) const;
    QString parentRemotePath(const QString &path) const;
    QString createTransferId() const;
    qint64 collectDirectoryStats(const QString &path, int *fileCount) const;
    void beginTransfer(const QString &transferId, const QString &operation, const TransferBatchItem &item);
    QString beginTransferBatch(const QString &operation, const QList<TransferBatchItem> &items, QStringList *childIds);
    QString findTransferIdByPath(const QString &path);
    void registerTransferPath(const QString &path, const QString &transferId);
    void unregisterTransferPaths(const QString &transferId, const TransferTask &task);
    void updateBatchTransfer(const QString &batchId, TransferTask &task);
    void finishTransfer(const QString &transferId, bool status, const QString &path);
    void updateTransferStatus();

    QCheckBox *m_followPathCheck = nullptr;
    QComboBox *m_driveCombo = nullptr;
    QLineEdit *m_remotePathEdit = nullptr;
    QLabel *m_transferStatusLabel = nullptr;
    QProgressBar *m_transferProgressBar = nullptr;
    QTableWidget *m_remoteTable = nullptr;
    QToolButton *m_parentButton = nullptr;
    QToolButton *m_refreshButton = nullptr;
    QToolButton *m_uploadFileButton = nullptr;
    QToolButton *m_uploadDirectoryButton = nullptr;
    QToolButton *m_downloadButton = nullptr;

    bool m_connected = false;
    bool m_updatingDriveCombo = false;
    QPoint m_remoteDragStartPos;
    QString m_currentRemotePath;
    QStringList m_mountedPaths;
    QJsonArray m_remoteFiles;
    QIcon m_dirIcon;
    QIcon m_fileIcon;
    QString m_currentTransferId;

    QHash<QString, TransferTask> m_transferTasks;
    QHash<QString, QStringList> m_transferPathIds;
};

#endif /* TERMINAL_FILE_PANEL_H */
