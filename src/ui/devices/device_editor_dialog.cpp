#include "device_manager_dialog.h"
#include "device_dialog_layout.h"
#include "ui/widgets/password/password_line_edit_util.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

bool DeviceManagerDialog::editEntry(DeviceEntry &entry, int editingIndex)
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("deviceEditorDialog"));
    dialog.setWindowTitle(tr("Device details"));
    auto *layout = DeviceDialogLayout::createWindowLayout(&dialog);
    layout->setContentsMargins(UiAdaptive::scaled(16), UiAdaptive::scaled(16),
                               UiAdaptive::scaled(16), UiAdaptive::scaled(16));
    auto *content = DeviceDialogLayout::addScrollableContent(
        &dialog, layout, QStringLiteral("deviceEditorScrollArea"));
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->setHorizontalSpacing(UiAdaptive::scaled(12));
    form->setVerticalSpacing(UiAdaptive::scaled(8));
    auto *id = new QLineEdit(entry.id, &dialog);
    id->setObjectName(QStringLiteral("deviceId"));
    id->setMaxLength(128);
    auto *password = new QLineEdit(entry.password, &dialog);
    password->setObjectName(QStringLiteral("devicePassword"));
    password->setMaxLength(1024);
    installPasswordRevealButton(password, true);
    auto *note = new QLineEdit(entry.note, &dialog);
    note->setObjectName(QStringLiteral("deviceNote"));
    note->setMaxLength(1024);
    auto *group = new QComboBox(&dialog);
    group->setObjectName(QStringLiteral("deviceGroup"));
    group->setEditable(true);
    group->addItems(groups());
    group->setCurrentText(entry.group);
    group->lineEdit()->setMaxLength(128);
    auto *os = new QComboBox(&dialog);
    os->setObjectName(QStringLiteral("deviceOs"));
    os->setEditable(true);
    os->addItems({QString(), QStringLiteral("Windows"), QStringLiteral("macOS"), QStringLiteral("Linux")});
    os->setCurrentText(entry.os);
    os->lineEdit()->setMaxLength(256);
    os->setToolTip(tr("Detected automatically for supported devices; can also be entered manually."));
    form->addRow(tr("Device ID"), id);
    form->addRow(tr("Connection password"), password);
    form->addRow(tr("Note"), note);
    form->addRow(tr("Group"), group);
    form->addRow(tr("Operating system"), os);
    content->addLayout(form);
    auto *error = new QLabel(&dialog);
    error->setWordWrap(true);
    content->addWidget(error);
    content->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Save"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    layout->addWidget(buttons);
    DeviceDialogLayout::constrainToScreen(&dialog, QSize(520, 390), QSize(300, 260));
    // Five stacked label/field rows need more height than the old fixed size,
    // especially after adding the shared title bar and scaling the stylesheet.
    // Measure the polished form at its real viewport width, then leave scrolling
    // available when the screen cannot fit that complete preferred height.
    dialog.ensurePolished();
    for (QWidget *widget : dialog.findChildren<QWidget *>())
        widget->ensurePolished();
    dialog.layout()->activate();
    layout->activate();
    auto *scroll = dialog.findChild<QScrollArea *>(QStringLiteral("deviceEditorScrollArea"));
    content->activate();
    const QMargins margins = layout->contentsMargins();
    const int viewportWidth = qMax(1, dialog.width() - margins.left() - margins.right());
    const int contentHeight = qMax(content->minimumSize().height(), content->totalHeightForWidth(viewportWidth));
    const int windowChromeHeight = dialog.findChild<AppTitleBar *>()->height() +
                                  margins.top() + margins.bottom() + layout->spacing() +
                                  buttons->sizeHint().height() + 2 * scroll->frameWidth();
    const int preferredHeight = contentHeight + windowChromeHeight + UiAdaptive::scaled(8);
    dialog.resize(dialog.width(), qMin(dialog.maximumHeight(), qMax(dialog.height(), preferredHeight)));
    id->setFocus(Qt::OtherFocusReason);
    scroll->verticalScrollBar()->setValue(0);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString deviceId = id->text().trimmed();
        if (deviceId.isEmpty() || password->text().trimmed().isEmpty())
        {
            error->setText(tr("Device ID and connection password are required."));
            return;
        }
        for (int index = 0; index < m_vault.entries().size(); ++index)
        {
            if (index != editingIndex && m_vault.entries().at(index).id.compare(deviceId, Qt::CaseInsensitive) == 0)
            {
                error->setText(tr("This device ID is already saved."));
                return;
            }
        }
        entry = {deviceId, password->text().trimmed(), note->text().trimmed(),
                 group->currentText().trimmed(), os->currentText().trimmed()};
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
