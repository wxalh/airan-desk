#include "ui/main/main_window.h"

#include "ui/common/message_box_util.h"

#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QLineEdit>
#include <QRadioButton>
#include <QUuid>


void MainWindow::on_btn_conn_clicked()
{
    tryFillRemoteFieldsFromShareText(m_remoteIdEdit->text());
    QString remote_id = m_remoteIdEdit->text().trimmed();
    QString remote_pwd = m_remotePwdEdit->text().trimmed();
    if (m_remoteIdEdit->text() != remote_id)
        m_remoteIdEdit->setText(remote_id);
    if (m_remotePwdEdit->text() != remote_pwd)
        m_remotePwdEdit->setText(remote_pwd);

    if (remote_id.isEmpty() || remote_pwd.isEmpty())
    {
        LOG_ERROR("Remote id and password must not be empty");
        if (RuntimeEnvironment::uiAvailable())
        {
            UiMessageBox::critical(this, tr("Error"), tr("Remote ID and password cannot be empty"));
        }
        return;
    }
    QByteArray hashResult = QCryptographicHash::hash(remote_pwd.toUtf8(), QCryptographicHash::Md5);
    QString remote_pwd_md5 = hashResult.toHex().toUpper();

    if (m_remoteDesktopRadio->isChecked())
    {
        connDesktopMgr(remote_id, remote_pwd_md5);
    }
    else if (m_remoteFileRadio->isChecked())
    {
        connFileMgr(remote_id, remote_pwd_md5);
    }
    else if (m_remoteTerminalRadio->isChecked())
    {
        connTerminalMgr(remote_id, remote_pwd_md5);
    }
}


void MainWindow::on_local_pwd_change_clicked()
{
    const QString password = QUuid::createUuid().toString().remove("{").remove("}").toUpper();
    if (!ConfigUtil->setLocalPwd(password))
    {
        UiMessageBox::critical(this,
                               tr("Identity storage error"),
                               tr("The new verification code could not be saved. The previous code remains active."));
        return;
    }
    m_accessPolicyGeneration->fetch_add(1);
    m_localPwdEdit->setText(ConfigUtil->getLocalPwd());
}


void MainWindow::on_local_share_clicked()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(textToCopy.arg(windowTitle, ConfigUtil->local_id, ConfigUtil->getLocalPwd()));
}
