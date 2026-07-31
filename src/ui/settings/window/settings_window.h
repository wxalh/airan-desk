#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H

#include "ui/settings/audio/settings_audio_backend.h"

#include <QDialog>
#include <QMutex>
#include <atomic>
#include <memory>
#include <thread>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;
class QCloseEvent;
class QShowEvent;
class QProgressBar;
class QPushButton;
class QUrl;
class QNetworkAccessManager;
class QTextBrowser;

namespace Ui
{
    class SettingsWindow;
}

class SettingsWindow : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget *parent = nullptr);
    ~SettingsWindow() override;

signals:
    void controlledAccessChanged(bool allowed);
    void signalingServerChanged();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void saveSettings();
    void refreshServiceState();
    void repairWindowsService();
    void onServiceOperationFinished(bool status, const QString &errorMessage);
    void refreshAudioDevices();
    void applyAudioDevices(const QList<SettingsAudioBackend::AudioDeviceItem> &devices,
                           const QString &preferredMic,
                           const QString &preferredLoopback);
    void testSpeaker();
    void toggleMicTest();
    void onMicTestLevel(float normalizedLevel);
    void enqueueMicTestLevel(float normalizedLevel);
    void drainPendingMicTestLevel();
    void onMicTestStopped();
    void chooseNotificationScript();
    void testNotificationScript();
    void chooseOpenH264Binary();
    void openOpenH264ReleasePage();
    void showOpenH264License();
    void showThirdPartyLicenses();
    void openLatestReleasePage();
    void openProjectPage();
    void openStarPage();
    void openServerPage();

private:
    void centerOnParent();
    void bindUiObjects();
    void populateSettingControls();
    void populateGeneralSettings();
    void populateConnectionSettings();
    void populateAudioSettings();
    void populateRemoteSettings();
    void populateCodecSettings();
    void populateAboutSettings();
    void refreshOpenH264Status();
    void polishSettingControls();
    bool openAboutUrl(const QUrl &url, const QString &actionName);
    bool isServiceInstalled() const;
    void startServiceOperation();
    QString selectedAudioDeviceValue(QComboBox *combo) const;
    void stopMicTest();

    QCheckBox *m_allowRemoteCheck{nullptr};
    QCheckBox *m_autoStartCheck{nullptr};
    QComboBox *m_logLevelCombo{nullptr};
    QComboBox *m_languageCombo{nullptr};
    QLineEdit *m_notificationScriptEdit{nullptr};
    QPushButton *m_notificationScriptBrowseButton{nullptr};
    QPushButton *m_notificationScriptTestButton{nullptr};
    QPushButton *m_thirdPartyLicensesButton{nullptr};
    QLineEdit *m_wsUrlEdit{nullptr};
    QLineEdit *m_iceHostEdit{nullptr};
    QSpinBox *m_icePortSpin{nullptr};
    QLineEdit *m_iceUserEdit{nullptr};
    QLineEdit *m_icePasswordEdit{nullptr};
    QComboBox *m_audioMicDeviceCombo{nullptr};
    QComboBox *m_audioLoopbackDeviceCombo{nullptr};
    QPushButton *m_refreshAudioDevicesBtn{nullptr};
    QPushButton *m_testSpeakerBtn{nullptr};
    QPushButton *m_testMicBtn{nullptr};
    QProgressBar *m_micLevelBar{nullptr};

    QLabel *m_aboutVersionLabel{nullptr};
    QPushButton *m_aboutLatestReleaseButton{nullptr};
    QPushButton *m_aboutProjectButton{nullptr};
    QPushButton *m_aboutStarButton{nullptr};
    QPushButton *m_aboutServerButton{nullptr};
    QLabel *m_aboutNoticeLabel{nullptr};
    QTextBrowser *m_aboutReadmeBrowser{nullptr};
    QNetworkAccessManager *m_aboutNetworkManager{nullptr};

    QCheckBox *m_openh264EnableCheck{nullptr};
    QLabel *m_openh264StatusLabel{nullptr};
    QPushButton *m_openh264ImportButton{nullptr};
    QPushButton *m_openh264DownloadButton{nullptr};
    QPushButton *m_openh264LicenseButton{nullptr};
    bool m_pendingOpenH264Enabled{false};
    QString m_pendingOpenH264Path;

    QSpinBox *m_fpsSpin{nullptr};
    QComboBox *m_networkCombo{nullptr};
    QCheckBox *m_enableWgcCheck{nullptr};
    QCheckBox *m_enableDxgiCheck{nullptr};
    QCheckBox *m_enableDxgiNativeGpuCheck{nullptr};
    QPushButton *m_serviceRepairButton{nullptr};
    QLabel *m_serviceStatusLabel{nullptr};
    std::atomic_bool m_micTestRunning{false};
    std::atomic_bool m_audioRefreshRunning{false};
    std::shared_ptr<std::atomic_bool> m_asyncCallbacksAlive{std::make_shared<std::atomic_bool>(true)};
    std::thread m_micTestThread;
    QMutex m_micLevelMutex;
    float m_pendingMicTestLevel{0.0f};
    bool m_micLevelDrainScheduled{false};
    bool m_serviceOperationRunning{false};
    bool m_closeAfterServiceOperation{false};
    bool m_closeAfterMicTest{false};
    std::thread m_serviceOperationThread;
    bool m_centeredOnFirstShow{false};
    Ui::SettingsWindow *ui{nullptr};
};

#endif /* SETTINGS_WINDOW_H */
