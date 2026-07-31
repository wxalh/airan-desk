#include "settings_window.h"

#include "ui/widgets/password/password_line_edit_util.h"
#include "ui_settings_window.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QRect>
#include <QSize>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTabBar>

namespace
{

class ComboPopupDelegate : public QStyledItemDelegate
{
public:
    explicit ComboPopupDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 34));
        return size;
    }
};


class SettingsTabHitFilter : public QObject
{
public:
    explicit SettingsTabHitFilter(QTabBar *tabBar)
        : QObject(tabBar),
          m_tabBar(tabBar)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != m_tabBar || !m_tabBar || event->type() != QEvent::MouseButtonPress)
            return QObject::eventFilter(watched, event);

        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton || m_tabBar->tabAt(mouseEvent->pos()) >= 0)
            return QObject::eventFilter(watched, event);

        for (int i = 0; i < m_tabBar->count(); ++i)
        {
            QRect hitRect = m_tabBar->tabRect(i);
            hitRect.setTop(0);
            hitRect.setBottom(m_tabBar->height() - 1);
            if (hitRect.contains(mouseEvent->pos()))
            {
                m_tabBar->setCurrentIndex(i);
                mouseEvent->accept();
                return true;
            }
        }

        return QObject::eventFilter(watched, event);
    }

private:
    QTabBar *m_tabBar{nullptr};
};


void polishSettingsField(QWidget *field)
{
    if (!field)
        return;

    field->setMinimumHeight(34);
    field->setMinimumWidth(260);
    field->setSizePolicy(QSizePolicy::Expanding, field->sizePolicy().verticalPolicy());
}


void polishSettingsCombo(QComboBox *combo)
{
    polishSettingsField(combo);
    combo->setItemDelegate(new ComboPopupDelegate(combo));
    if (auto *listView = qobject_cast<QListView *>(combo->view()))
        listView->setSpacing(2);
    combo->view()->setTextElideMode(Qt::ElideRight);
}


void polishDialogButtons(QDialogButtonBox *buttonBox)
{
    if (!buttonBox)
        return;

    if (QAbstractButton *saveBtn = buttonBox->button(QDialogButtonBox::Save))
        saveBtn->setText(QCoreApplication::translate("SettingsWindow", "Save"));
    if (QAbstractButton *closeBtn = buttonBox->button(QDialogButtonBox::Close))
        closeBtn->setText(QCoreApplication::translate("SettingsWindow", "Close"));
}
} /* namespace */


void SettingsWindow::bindUiObjects()
{
    m_allowRemoteCheck = ui->allowRemoteCheck;
    m_autoStartCheck = ui->autoStartCheck;
    m_logLevelCombo = ui->logLevelCombo;
    m_languageCombo = ui->languageCombo;
    m_notificationScriptEdit = ui->notificationScriptEdit;
    m_notificationScriptBrowseButton = ui->notificationScriptBrowseButton;
    m_notificationScriptTestButton = ui->notificationScriptTestButton;
    m_thirdPartyLicensesButton = ui->thirdPartyLicensesButton;
    m_aboutVersionLabel = ui->aboutVersionLabel;
    m_aboutLatestReleaseButton = ui->aboutLatestReleaseButton;
    m_aboutProjectButton = ui->aboutProjectButton;
    m_aboutStarButton = ui->aboutStarButton;
    m_aboutServerButton = ui->aboutServerButton;
    m_aboutNoticeLabel = ui->aboutNoticeLabel;
    m_aboutReadmeBrowser = ui->aboutReadmeBrowser;
    m_wsUrlEdit = ui->wsUrlEdit;
    m_iceHostEdit = ui->iceHostEdit;
    m_icePortSpin = ui->icePortSpin;
    m_iceUserEdit = ui->iceUserEdit;
    m_icePasswordEdit = ui->icePasswordEdit;
    m_audioMicDeviceCombo = ui->audioMicDeviceCombo;
    m_audioLoopbackDeviceCombo = ui->audioLoopbackDeviceCombo;
    m_refreshAudioDevicesBtn = ui->refreshAudioDevicesBtn;
    m_testSpeakerBtn = ui->testSpeakerBtn;
    m_testMicBtn = ui->testMicBtn;
    m_micLevelBar = ui->micLevelBar;
    m_openh264EnableCheck = ui->openh264EnableCheck;
    m_openh264StatusLabel = ui->openh264StatusLabel;
    m_openh264ImportButton = ui->openh264ImportButton;
    m_openh264DownloadButton = ui->openh264DownloadButton;
    m_openh264LicenseButton = ui->openh264LicenseButton;
    m_fpsSpin = ui->fpsSpin;
    m_networkCombo = ui->networkCombo;
    m_enableWgcCheck = ui->enableWgcCheck;
    m_enableDxgiCheck = ui->enableDxgiCheck;
    m_enableDxgiNativeGpuCheck = ui->enableDxgiNativeGpuCheck;
    m_serviceRepairButton = ui->serviceRepairButton;
    m_serviceStatusLabel = ui->serviceStatusLabel;
}


void SettingsWindow::populateSettingControls()
{
    populateGeneralSettings();
    populateConnectionSettings();
    populateAudioSettings();
    populateRemoteSettings();
    populateCodecSettings();
    populateAboutSettings();
    polishSettingControls();
}


void SettingsWindow::polishSettingControls()
{
    ui->settingsTabs->tabBar()->installEventFilter(new SettingsTabHitFilter(ui->settingsTabs->tabBar()));
    polishDialogButtons(ui->buttonBox);

    const QList<QWidget *> fields{m_logLevelCombo, m_languageCombo, m_notificationScriptEdit, m_wsUrlEdit, m_iceHostEdit,
                                  m_icePortSpin, m_iceUserEdit, m_icePasswordEdit,
                                  m_audioMicDeviceCombo, m_audioLoopbackDeviceCombo, m_fpsSpin,
                                  m_networkCombo};
    for (QWidget *field : fields)
        polishSettingsField(field);

    for (QComboBox *combo : {m_logLevelCombo, m_languageCombo, m_audioMicDeviceCombo,
                             m_audioLoopbackDeviceCombo, m_networkCombo})
        polishSettingsCombo(combo);

    setPasswordRevealFonts(m_icePasswordEdit, m_icePasswordEdit->font(), m_iceUserEdit->font());
}
