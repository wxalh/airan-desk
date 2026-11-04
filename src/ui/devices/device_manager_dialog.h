#ifndef DEVICE_MANAGER_DIALOG_H
#define DEVICE_MANAGER_DIALOG_H

#include "devices/device_vault.h"
#include <QDialog>
#include <QHash>

class QCheckBox;
class QBoxLayout;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTimer;
class QResizeEvent;

class DeviceManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DeviceManagerDialog(QWidget *parent = nullptr, const QString &vaultPath = QString());
    bool openVault();
    void setInitialDevice(const QString &id, const QString &password);

signals:
    void deviceSelected(const QString &id, const QString &password);
    void queryPresence(const QStringList &ids);

public slots:
    void updatePresence(const QString &id, const QString &state, const QString &os);
    void refreshPresence();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    bool authenticate(bool creating);
    bool editEntry(DeviceEntry &entry, int editingIndex = -1);
    void changeMasterPassword();
    void rebuildTable();
    void applyFilter();
    void updateActions();
    void addDevice();
    void editDevice();
    void removeDevice();
    void loadDevice();
    int selectedIndex() const;
    void showError();
    void saveDetectedOperatingSystems();
    QStringList groups() const;
    void updateResponsiveLayout();

    DeviceVault m_vault;
    DeviceEntry m_initialDevice;
    QHash<QString, QString> m_presence;
    QHash<QString, QString> m_detectedOs;
    QTableWidget *m_table{nullptr};
    QLineEdit *m_search{nullptr};
    QComboBox *m_group{nullptr};
    QCheckBox *m_remember{nullptr};
    QLabel *m_status{nullptr};
    QPushButton *m_edit{nullptr};
    QPushButton *m_remove{nullptr};
    QPushButton *m_load{nullptr};
    QTimer *m_refreshTimer{nullptr};
    QTimer *m_saveOsTimer{nullptr};
    QBoxLayout *m_filterLayout{nullptr};
    QBoxLayout *m_actionLayout{nullptr};
    QBoxLayout *m_securityLayout{nullptr};
    QBoxLayout *m_editActionsLayout{nullptr};
    QBoxLayout *m_loadActionsLayout{nullptr};
    QBoxLayout *m_passwordActionsLayout{nullptr};
};

#endif
