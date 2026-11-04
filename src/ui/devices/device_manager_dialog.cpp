#include "device_manager_dialog.h"
#include "device_dialog_layout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QBoxLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QResizeEvent>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

DeviceManagerDialog::DeviceManagerDialog(QWidget *parent, const QString &vaultPath)
    : QDialog(parent), m_vault(vaultPath)
{
    setObjectName(QStringLiteral("myDevicesDialog"));
    setWindowTitle(tr("My devices"));
    setWindowIcon(parent ? parent->windowIcon() : QIcon());
    DeviceDialogLayout::constrainToScreen(this, QSize(900, 580), QSize(340, 280));
    auto *layout = DeviceDialogLayout::createWindowLayout(this, true);
    layout->setContentsMargins(UiAdaptive::scaled(20), UiAdaptive::scaled(20),
                               UiAdaptive::scaled(20), UiAdaptive::scaled(20));
    layout->setSpacing(UiAdaptive::scaled(12));
    auto *content = DeviceDialogLayout::addScrollableContent(
        this, layout, QStringLiteral("deviceManagerScrollArea"));
    auto *intro = new QLabel(tr("Devices are encrypted locally. Double-click a device to fill the home page."), this);
    intro->setWordWrap(true);
    content->addWidget(intro);

    m_filterLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_filterLayout->setObjectName(QStringLiteral("deviceFilterLayout"));
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("deviceSearch"));
    m_search->setPlaceholderText(tr("Search device ID or note"));
    m_search->setClearButtonEnabled(true);
    m_group = new QComboBox(this);
    m_group->setObjectName(QStringLiteral("deviceGroupFilter"));
    m_group->setMinimumWidth(0);
    m_filterLayout->addWidget(m_search, 1);
    m_filterLayout->addWidget(m_group);
    content->addLayout(m_filterLayout);

    m_table = new QTableWidget(0, 5, this);
    m_table->setObjectName(QStringLiteral("deviceTable"));
    m_table->setHorizontalHeaderLabels({tr("Device ID"), tr("Note"), tr("Group"), tr("Status"), tr("Operating system")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setMinimumSectionSize(40);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setColumnWidth(0, UiAdaptive::scaled(180));
    m_table->setColumnWidth(1, UiAdaptive::scaled(170));
    m_table->setColumnWidth(2, UiAdaptive::scaled(110));
    m_table->setColumnWidth(3, UiAdaptive::scaled(90));
    m_table->setMinimumHeight(UiAdaptive::scaled(160));
    content->addWidget(m_table, 1);

    m_actionLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_actionLayout->setObjectName(QStringLiteral("deviceActionLayout"));
    auto *add = new QPushButton(tr("Add device"), this);
    add->setObjectName(QStringLiteral("addDeviceButton"));
    m_edit = new QPushButton(tr("Edit"), this);
    m_remove = new QPushButton(tr("Remove"), this);
    auto *refresh = new QPushButton(tr("Refresh status"), this);
    m_load = new QPushButton(tr("Load to home"), this);
    m_load->setObjectName(QStringLiteral("loadDeviceButton"));
    for (auto *button : {add, m_edit, m_remove, refresh, m_load})
        button->setAutoDefault(false);
    m_editActionsLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_editActionsLayout->setObjectName(QStringLiteral("deviceEditActionsLayout"));
    m_editActionsLayout->addWidget(add);
    m_editActionsLayout->addWidget(m_edit);
    m_editActionsLayout->addWidget(m_remove);
    m_actionLayout->addLayout(m_editActionsLayout);
    m_actionLayout->addStretch();
    m_loadActionsLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_loadActionsLayout->setObjectName(QStringLiteral("deviceLoadActionsLayout"));
    m_loadActionsLayout->addWidget(refresh);
    m_loadActionsLayout->addWidget(m_load);
    m_actionLayout->addLayout(m_loadActionsLayout);
    content->addLayout(m_actionLayout);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    content->addWidget(m_status);
    m_securityLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_securityLayout->setObjectName(QStringLiteral("deviceSecurityLayout"));
    m_remember = new QCheckBox(tr("Remember management password permanently"), this);
    m_remember->setObjectName(QStringLiteral("rememberDevicePassword"));
    m_remember->setToolTip(tr("Allow this system account to unlock my devices automatically."));
    auto *changePassword = new QPushButton(tr("Change management password"), this);
    auto *lock = new QPushButton(tr("Lock and close"), this);
    changePassword->setAutoDefault(false);
    lock->setAutoDefault(false);
    m_securityLayout->addWidget(m_remember);
    m_securityLayout->addStretch();
    m_passwordActionsLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_passwordActionsLayout->setObjectName(QStringLiteral("devicePasswordActionsLayout"));
    m_passwordActionsLayout->addWidget(changePassword);
    m_passwordActionsLayout->addWidget(lock);
    m_securityLayout->addLayout(m_passwordActionsLayout);
    content->addLayout(m_securityLayout);

    connect(add, &QPushButton::clicked, this, &DeviceManagerDialog::addDevice);
    connect(m_edit, &QPushButton::clicked, this, &DeviceManagerDialog::editDevice);
    connect(m_remove, &QPushButton::clicked, this, &DeviceManagerDialog::removeDevice);
    connect(m_load, &QPushButton::clicked, this, &DeviceManagerDialog::loadDevice);
    connect(refresh, &QPushButton::clicked, this, &DeviceManagerDialog::refreshPresence);
    connect(changePassword, &QPushButton::clicked, this, &DeviceManagerDialog::changeMasterPassword);
    connect(lock, &QPushButton::clicked, this, [this]() {
        if (m_vault.setRemembered(false))
            reject();
        else
            showError();
    });
    connect(m_search, &QLineEdit::textChanged, this, &DeviceManagerDialog::applyFilter);
    connect(m_group, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DeviceManagerDialog::applyFilter);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &DeviceManagerDialog::updateActions);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { loadDevice(); });
    connect(m_remember, &QCheckBox::toggled, this, [this](bool remember) {
        if (!m_vault.setRemembered(remember))
        {
            showError();
            const QSignalBlocker blocker(m_remember);
            m_remember->setChecked(m_vault.isRemembered());
        }
    });
    connect(this, &QDialog::finished, this, [this]() {
        m_refreshTimer->stop();
        m_saveOsTimer->stop();
        saveDetectedOperatingSystems();
        m_detectedOs.clear();
        m_table->clearContents();
        m_table->setRowCount(0);
        m_vault.lock();
        m_initialDevice.password.fill(QChar());
        m_initialDevice.password.clear();
    });
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(30000);
    connect(m_refreshTimer, &QTimer::timeout, this, &DeviceManagerDialog::refreshPresence);
    m_saveOsTimer = new QTimer(this);
    m_saveOsTimer->setSingleShot(true);
    m_saveOsTimer->setInterval(750);
    connect(m_saveOsTimer, &QTimer::timeout, this, &DeviceManagerDialog::saveDetectedOperatingSystems);
    updateActions();
    updateResponsiveLayout();
    UiAdaptive::applyPreferredWindowSize(this, QSize(960, 720), QSize(420, 320));
}

void DeviceManagerDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateResponsiveLayout();
}

void DeviceManagerDialog::updateResponsiveLayout()
{
    if (m_filterLayout)
        m_filterLayout->setDirection(width() < UiAdaptive::scaled(520) ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    if (m_actionLayout)
        m_actionLayout->setDirection(width() < UiAdaptive::scaled(760) ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    if (m_securityLayout)
        m_securityLayout->setDirection(width() < UiAdaptive::scaled(760) ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    const bool compact = width() < UiAdaptive::scaled(520);
    for (QBoxLayout *group : {m_editActionsLayout, m_loadActionsLayout, m_passwordActionsLayout})
        if (group)
            group->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
}

bool DeviceManagerDialog::openVault()
{
    const bool creating = !m_vault.exists();
    if (!creating && m_vault.unlockRemembered())
    {
        // The OS protected credential unlocks only this local vault.
    }
    else if (!authenticate(creating))
        return false;

    const QSignalBlocker blocker(m_remember);
    m_remember->setChecked(m_vault.isRemembered());
    rebuildTable();
    m_refreshTimer->start();
    QTimer::singleShot(0, this, &DeviceManagerDialog::refreshPresence);
    return true;
}

void DeviceManagerDialog::setInitialDevice(const QString &id, const QString &password)
{
    m_initialDevice.id = id.trimmed();
    m_initialDevice.password = password;
}

QStringList DeviceManagerDialog::groups() const
{
    QSet<QString> unique;
    for (const auto &entry : m_vault.entries())
        if (!entry.group.isEmpty())
            unique.insert(entry.group);
    QStringList result = unique.values();
    result.sort(Qt::CaseInsensitive);
    return result;
}

void DeviceManagerDialog::rebuildTable()
{
    const QString selectedGroup = m_group->currentData().toString();
    const QSignalBlocker blocker(m_group);
    m_group->clear();
    m_group->addItem(tr("All groups"), QString());
    for (const QString &group : groups())
        m_group->addItem(group, group);
    const int groupIndex = m_group->findData(selectedGroup);
    m_group->setCurrentIndex(qMax(0, groupIndex));
    const auto &entries = m_vault.entries();
    m_table->setRowCount(entries.size());
    for (int row = 0; row < entries.size(); ++row)
    {
        const DeviceEntry &entry = entries.at(row);
        const QString state = m_presence.value(entry.id);
        const QString status = state == QStringLiteral("online") ? tr("Online")
                             : state == QStringLiteral("offline") ? tr("Offline") : tr("Unknown");
        const QString os = m_detectedOs.value(entry.id, entry.os);
        const QStringList cells{entry.id, entry.note, entry.group, status,
                                os.isEmpty() ? tr("Unknown") : os};
        for (int column = 0; column < cells.size(); ++column)
        {
            auto *item = new QTableWidgetItem(cells.at(column));
            item->setToolTip(cells.at(column));
            m_table->setItem(row, column, item);
        }
    }
    applyFilter();
    updateActions();
}

void DeviceManagerDialog::applyFilter()
{
    const QString search = m_search->text().trimmed();
    const QString group = m_group->currentData().toString();
    for (int row = 0; row < m_vault.entries().size(); ++row)
    {
        const DeviceEntry &entry = m_vault.entries().at(row);
        m_table->setRowHidden(row, (!group.isEmpty() && entry.group != group)
            || (!search.isEmpty() && !entry.id.contains(search, Qt::CaseInsensitive)
                && !entry.note.contains(search, Qt::CaseInsensitive)));
    }
    updateActions();
}

int DeviceManagerDialog::selectedIndex() const
{
    const int row = m_table->currentRow();
    return row >= 0 && row < m_vault.entries().size() && !m_table->isRowHidden(row) ? row : -1;
}

void DeviceManagerDialog::updateActions()
{
    const bool selected = selectedIndex() >= 0;
    m_edit->setEnabled(selected);
    m_remove->setEnabled(selected);
    m_load->setEnabled(selected);
}

void DeviceManagerDialog::showError()
{
    QMessageBox::warning(this, tr("My devices"), m_vault.errorString());
}

void DeviceManagerDialog::addDevice()
{
    DeviceEntry entry = m_initialDevice;
    if (!editEntry(entry))
        return;
    auto entries = m_vault.entries();
    entries.append(entry);
    if (!m_vault.save(entries))
    {
        showError();
        return;
    }
    rebuildTable();
    refreshPresence();
}

void DeviceManagerDialog::editDevice()
{
    const int index = selectedIndex();
    if (index < 0)
        return;
    DeviceEntry entry = m_vault.entries().at(index);
    if (!editEntry(entry, index))
        return;
    auto entries = m_vault.entries();
    entries[index] = entry;
    if (!m_vault.save(entries))
    {
        showError();
        return;
    }
    m_detectedOs.remove(entry.id);
    rebuildTable();
    refreshPresence();
}

void DeviceManagerDialog::removeDevice()
{
    const int index = selectedIndex();
    if (index < 0)
        return;
    if (QMessageBox::question(this, tr("Remove device"),
            tr("Remove device %1 from my devices?").arg(m_vault.entries().at(index).id),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    auto entries = m_vault.entries();
    m_presence.remove(entries.at(index).id);
    m_detectedOs.remove(entries.at(index).id);
    entries.removeAt(index);
    if (!m_vault.save(entries))
        showError();
    rebuildTable();
}

void DeviceManagerDialog::loadDevice()
{
    const int index = selectedIndex();
    if (index < 0)
        return;
    const DeviceEntry entry = m_vault.entries().at(index);
    emit deviceSelected(entry.id, entry.password);
    accept();
}

void DeviceManagerDialog::refreshPresence()
{
    if (!m_vault.isUnlocked())
        return;
    QStringList ids;
    for (const auto &entry : m_vault.entries())
    {
        ids.append(entry.id);
        m_presence.insert(entry.id, QStringLiteral("unknown"));
    }
    m_status->setText(ids.isEmpty() ? tr("No saved devices. Add a device to get started.")
        : tr("Status refreshes every 30 seconds. Unknown means the server is unavailable or the device does not support status queries."));
    rebuildTable();
    emit queryPresence(ids);
}

void DeviceManagerDialog::updatePresence(const QString &id, const QString &state, const QString &os)
{
    if (!m_vault.isUnlocked())
        return;
    const auto &entries = m_vault.entries();
    for (int row = 0; row < entries.size(); ++row)
    {
        if (entries.at(row).id != id)
            continue;
        m_presence.insert(id, state);
        if (!os.isEmpty() && entries.at(row).os != os)
        {
            m_detectedOs.insert(id, os);
            if (!m_saveOsTimer->isActive())
                m_saveOsTimer->start();
        }
        else if (!os.isEmpty())
            m_detectedOs.remove(id);
        auto *status = m_table->item(row, 3);
        if (status)
        {
            status->setText(state == QStringLiteral("online") ? tr("Online")
                : state == QStringLiteral("offline") ? tr("Offline") : tr("Unknown"));
            status->setToolTip(status->text());
        }
        if (m_table->item(row, 4) && !os.isEmpty())
        {
            m_table->item(row, 4)->setText(os);
            m_table->item(row, 4)->setToolTip(os);
        }
        break;
    }
}

void DeviceManagerDialog::saveDetectedOperatingSystems()
{
    if (!m_vault.isUnlocked() || m_detectedOs.isEmpty())
        return;
    auto entries = m_vault.entries();
    bool changed = false;
    for (auto &entry : entries)
    {
        const auto detected = m_detectedOs.constFind(entry.id);
        if (detected != m_detectedOs.constEnd() && entry.os != detected.value())
        {
            entry.os = detected.value();
            changed = true;
        }
    }
    if (changed && !m_vault.save(entries))
        m_status->setText(m_vault.errorString());
    else
        m_detectedOs.clear();
}
