#include "ui/settings/window/settings_window.h"
#include "ui/settings/audio/settings_audio_backend.h"
#include "util/config/config_util.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QMetaObject>
#include <QMutexLocker>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVariant>

namespace
{

void setComboByData(QComboBox *combo, const QString &data)
{
    const int index = combo ? combo->findData(data) : -1;
    if (index >= 0)
        combo->setCurrentIndex(index);
}
}


QString SettingsWindow::selectedAudioDeviceValue(QComboBox *combo) const
{
    if (!combo)
        return QString();

    const int idx = combo->currentIndex();
    if (idx >= 0 && idx < combo->count())
    {
        const QVariant data = combo->itemData(idx);
        if (data.isValid())
            return data.toString().trimmed();
    }
    return combo->currentText().trimmed();
}


void SettingsWindow::refreshAudioDevices()
{
    if (m_audioRefreshRunning.exchange(true))
        return;
    if (m_refreshAudioDevicesBtn)
        m_refreshAudioDevicesBtn->setEnabled(false);

    QString preferredMic = selectedAudioDeviceValue(m_audioMicDeviceCombo);
    QString preferredLoopback = selectedAudioDeviceValue(m_audioLoopbackDeviceCombo);
    if (m_audioMicDeviceCombo && m_audioMicDeviceCombo->count() == 0)
        preferredMic = ConfigUtil->audio_mic_device;
    if (m_audioLoopbackDeviceCombo && m_audioLoopbackDeviceCombo->count() == 0)
        preferredLoopback = ConfigUtil->audio_loopback_device;

    const std::shared_ptr<std::atomic_bool> callbackState = m_asyncCallbacksAlive;
    SettingsWindow *const receiver = this;
    std::thread([callbackState, receiver, preferredMic, preferredLoopback]() {
        const QList<SettingsAudioBackend::AudioDeviceItem> devices = SettingsAudioBackend::enumerateAudioDevices();
        QCoreApplication *application = QCoreApplication::instance();
        if (!application)
            return;
        QTimer::singleShot(0, application, [callbackState, receiver, devices, preferredMic, preferredLoopback]() {
            if (!callbackState->load())
                return;
            receiver->applyAudioDevices(devices, preferredMic, preferredLoopback);
            receiver->m_audioRefreshRunning.store(false);
        });
    }).detach();
}


void SettingsWindow::applyAudioDevices(const QList<SettingsAudioBackend::AudioDeviceItem> &devices,
                                       const QString &preferredMic,
                                       const QString &preferredLoopback)
{
    m_audioMicDeviceCombo->clear();
    m_audioLoopbackDeviceCombo->clear();
    m_audioMicDeviceCombo->addItem(QCoreApplication::translate("SettingsWindow", "System default"), QString());
    m_audioLoopbackDeviceCombo->addItem(QCoreApplication::translate("SettingsWindow", "System default"), QString());

    for (const SettingsAudioBackend::AudioDeviceItem &item : devices)
    {
        if (item.loopback)
            m_audioLoopbackDeviceCombo->addItem(item.displayName, item.id);
        else
            m_audioMicDeviceCombo->addItem(item.displayName, item.id);
    }

    m_audioMicDeviceCombo->addItem(QCoreApplication::translate("SettingsWindow", "None"), SettingsAudioBackend::noneDeviceValue());
    m_audioLoopbackDeviceCombo->addItem(QCoreApplication::translate("SettingsWindow", "None"), SettingsAudioBackend::noneDeviceValue());

#if defined(Q_OS_LINUX)
    if (m_audioLoopbackDeviceCombo->findData(QStringLiteral("@DEFAULT_MONITOR@")) < 0)
        m_audioLoopbackDeviceCombo->insertItem(1, QStringLiteral("@DEFAULT_MONITOR@"), QStringLiteral("@DEFAULT_MONITOR@"));
#endif

    m_audioMicDeviceCombo->setInsertPolicy(QComboBox::NoInsert);
    m_audioLoopbackDeviceCombo->setInsertPolicy(QComboBox::NoInsert);
    m_audioMicDeviceCombo->setEditable(false);
    m_audioLoopbackDeviceCombo->setEditable(false);

    setComboByData(m_audioMicDeviceCombo, preferredMic);
    setComboByData(m_audioLoopbackDeviceCombo, preferredLoopback);
    if (m_refreshAudioDevicesBtn)
        m_refreshAudioDevicesBtn->setEnabled(true);
}


void SettingsWindow::testSpeaker()
{
    if (selectedAudioDeviceValue(m_audioLoopbackDeviceCombo).compare(SettingsAudioBackend::noneDeviceValue(), Qt::CaseInsensitive) == 0)
        return;

    const QString preferredOutput = selectedAudioDeviceValue(m_audioLoopbackDeviceCombo);
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32) || defined(Q_OS_LINUX)
    if (m_speakerTestRunning.exchange(true))
        return;
    if (m_testSpeakerBtn)
        m_testSpeakerBtn->setEnabled(false);

    const std::shared_ptr<std::atomic_bool> callbackState = m_asyncCallbacksAlive;
    SettingsWindow *const receiver = this;
    std::thread([callbackState, receiver, preferredOutput]() {
        SettingsAudioBackend::playToneOnOutput(preferredOutput);
        QCoreApplication *application = QCoreApplication::instance();
        if (!application)
            return;
        QTimer::singleShot(0, application, [callbackState, receiver]() {
            if (!callbackState->load())
                return;
            receiver->m_speakerTestRunning.store(false);
            if (receiver->m_testSpeakerBtn)
                receiver->m_testSpeakerBtn->setEnabled(true);
        });
    }).detach();
#else
    SettingsAudioBackend::playToneOnOutput(preferredOutput);
#endif
}


void SettingsWindow::toggleMicTest()
{
    if (m_micTestRunning.load())
    {
        stopMicTest();
        return;
    }

    if (m_micTestThread.joinable())
        return;

    if (selectedAudioDeviceValue(m_audioMicDeviceCombo).compare(SettingsAudioBackend::noneDeviceValue(), Qt::CaseInsensitive) == 0)
        return;

    stopMicTest();
    m_micTestRunning.store(true);
    if (m_micLevelBar)
        m_micLevelBar->setValue(0);
    if (m_testMicBtn)
        m_testMicBtn->setText(QCoreApplication::translate("SettingsWindow", "Stop"));

    const QString preferredInput = selectedAudioDeviceValue(m_audioMicDeviceCombo);
    m_micTestThread = std::thread([this, preferredInput]() {
        SettingsAudioBackend::runMicLevelTest(preferredInput, &m_micTestRunning, this);
    });
}


void SettingsWindow::stopMicTest()
{
    m_micTestRunning.store(false);
    if (m_micTestThread.joinable() && m_testMicBtn)
        m_testMicBtn->setEnabled(false);
    QMutexLocker locker(&m_micLevelMutex);
    m_pendingMicTestLevel = 0.0f;
}


void SettingsWindow::enqueueMicTestLevel(float normalizedLevel)
{
    bool scheduleDrain = false;
    {
        QMutexLocker locker(&m_micLevelMutex);
        m_pendingMicTestLevel = normalizedLevel;
        if (!m_micLevelDrainScheduled)
        {
            m_micLevelDrainScheduled = true;
            scheduleDrain = true;
        }
    }
    if (scheduleDrain)
        QMetaObject::invokeMethod(this, "drainPendingMicTestLevel", Qt::QueuedConnection);
}


void SettingsWindow::drainPendingMicTestLevel()
{
    float normalizedLevel = 0.0f;
    {
        QMutexLocker locker(&m_micLevelMutex);
        normalizedLevel = m_pendingMicTestLevel;
        m_pendingMicTestLevel = 0.0f;
        m_micLevelDrainScheduled = false;
    }
    if (m_micTestRunning.load())
        onMicTestLevel(normalizedLevel);
}


void SettingsWindow::onMicTestLevel(float normalizedLevel)
{
    const int percent = qBound(0, static_cast<int>(normalizedLevel * 100.0f), 100);
    if (m_micLevelBar)
        m_micLevelBar->setValue(percent);
}


void SettingsWindow::onMicTestStopped()
{
    m_micTestRunning.store(false);
    if (m_micTestThread.joinable())
        m_micTestThread.join();
    if (m_testMicBtn)
    {
        m_testMicBtn->setEnabled(true);
        m_testMicBtn->setText(QCoreApplication::translate("SettingsWindow", "Test"));
    }
    QTimer::singleShot(150, this, [this]() {
        if (m_micLevelBar)
            m_micLevelBar->setValue(0);
    });
    if (m_closeAfterMicTest)
    {
        m_closeAfterMicTest = false;
        close();
    }
}
