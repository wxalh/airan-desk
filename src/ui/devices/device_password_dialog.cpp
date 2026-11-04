#include "device_manager_dialog.h"
#include "device_dialog_layout.h"
#include "ui/widgets/password/password_line_edit_util.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

bool DeviceManagerDialog::authenticate(bool creating)
{
    QDialog dialog(parentWidget());
    dialog.setObjectName(QStringLiteral("deviceVaultPasswordDialog"));
    dialog.setWindowTitle(creating ? tr("Set management password") : tr("Unlock my devices"));
    auto *layout = DeviceDialogLayout::createWindowLayout(&dialog);
    layout->setContentsMargins(UiAdaptive::scaled(16), UiAdaptive::scaled(16),
                               UiAdaptive::scaled(16), UiAdaptive::scaled(16));
    auto *content = DeviceDialogLayout::addScrollableContent(
        &dialog, layout, QStringLiteral("devicePasswordScrollArea"));
    auto *intro = new QLabel(creating
        ? tr("Set a separate management password to encrypt your saved devices. Use at least 8 characters. A forgotten password cannot be recovered.")
        : tr("Enter your management password to unlock your saved devices."), &dialog);
    intro->setWordWrap(true);
    content->addWidget(intro);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->setHorizontalSpacing(UiAdaptive::scaled(12));
    form->setVerticalSpacing(UiAdaptive::scaled(8));
    auto *password = new QLineEdit(&dialog);
    password->setObjectName(QStringLiteral("managementPassword"));
    password->setMaxLength(1024);
    installPasswordRevealButton(password, true);
    form->addRow(tr("Management password"), password);
    QLineEdit *confirm = nullptr;
    if (creating)
    {
        confirm = new QLineEdit(&dialog);
        confirm->setObjectName(QStringLiteral("confirmManagementPassword"));
        confirm->setMaxLength(1024);
        installPasswordRevealButton(confirm, true);
        form->addRow(tr("Confirm password"), confirm);
    }
    content->addLayout(form);
    auto *remember = new QCheckBox(tr("Remember management password permanently"), &dialog);
    remember->setObjectName(QStringLiteral("rememberManagementPassword"));
    content->addWidget(remember);
    auto *error = new QLabel(&dialog);
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    content->addWidget(error);
    content->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    DeviceDialogLayout::constrainToScreen(&dialog, QSize(520, creating ? 380 : 330), QSize(300, 250));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (creating && (password->text().size() < 8 || password->text() != confirm->text()))
        {
            error->setText(tr("Use at least 8 characters and enter the same password twice."));
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool unlocked = creating ? m_vault.create(password->text(), false) : m_vault.unlock(password->text());
        QApplication::restoreOverrideCursor();
        if (!unlocked)
        {
            error->setText(m_vault.errorString());
            return;
        }
        if (!m_vault.setRemembered(remember->isChecked()))
            QMessageBox::warning(&dialog, tr("My devices"), m_vault.errorString());
        dialog.accept();
    });
    password->setFocus();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    password->clear();
    if (confirm)
        confirm->clear();
    return accepted;
}

void DeviceManagerDialog::changeMasterPassword()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Change management password"));
    auto *layout = DeviceDialogLayout::createWindowLayout(&dialog);
    layout->setContentsMargins(UiAdaptive::scaled(16), UiAdaptive::scaled(16),
                               UiAdaptive::scaled(16), UiAdaptive::scaled(16));
    auto *content = DeviceDialogLayout::addScrollableContent(
        &dialog, layout, QStringLiteral("devicePasswordChangeScrollArea"));
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->setHorizontalSpacing(UiAdaptive::scaled(12));
    form->setVerticalSpacing(UiAdaptive::scaled(8));
    auto *oldPassword = new QLineEdit(&dialog);
    auto *newPassword = new QLineEdit(&dialog);
    auto *confirm = new QLineEdit(&dialog);
    for (auto *edit : {oldPassword, newPassword, confirm})
    {
        edit->setMaxLength(1024);
        installPasswordRevealButton(edit, true);
    }
    form->addRow(tr("Current management password"), oldPassword);
    form->addRow(tr("New management password"), newPassword);
    form->addRow(tr("Confirm password"), confirm);
    content->addLayout(form);
    auto *error = new QLabel(&dialog);
    error->setWordWrap(true);
    content->addWidget(error);
    content->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    DeviceDialogLayout::constrainToScreen(&dialog, QSize(520, 370), QSize(300, 250));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (newPassword->text().size() < 8 || newPassword->text() != confirm->text())
        {
            error->setText(tr("Use at least 8 characters and enter the same password twice."));
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool changed = m_vault.changePassword(oldPassword->text(), newPassword->text());
        QApplication::restoreOverrideCursor();
        if (!changed)
        {
            error->setText(m_vault.errorString());
            return;
        }
        const QSignalBlocker blocker(m_remember);
        m_remember->setChecked(m_vault.isRemembered());
        dialog.accept();
    });
    dialog.exec();
    for (auto *edit : {oldPassword, newPassword, confirm})
        edit->clear();
}
