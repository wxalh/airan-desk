#include "license_viewer_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QVBoxLayout>

LicenseViewerDialog::LicenseViewerDialog(QWidget *parent)
    : QDialog(parent),
      m_entries(LicenseCatalog::discover(LicenseCatalog::defaultDirectories()))
{
    setWindowTitle(tr("Third-party licenses"));
    resize(820, 560);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(
        tr("These notices describe the third-party components included with this Airan Desk package."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *splitter = new QSplitter(this);
    m_list = new QListWidget(splitter);
    m_text = new QPlainTextEdit(splitter);
    m_text->setReadOnly(true);
    splitter->addWidget(m_list);
    splitter->addWidget(m_text);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    for (const LicenseCatalog::Entry &entry : m_entries)
        m_list->addItem(entry.displayName);
    connect(m_list, &QListWidget::currentRowChanged,
            this, &LicenseViewerDialog::showEntry);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    if (m_entries.isEmpty())
        m_text->setPlainText(tr("No packaged third-party license documents were found."));
    else
        m_list->setCurrentRow(0);
}

void LicenseViewerDialog::showEntry(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    QString error;
    const QString content = LicenseCatalog::readText(m_entries.at(row), &error);
    m_text->setPlainText(error.isEmpty()
                             ? content
                             : tr("The license document could not be opened: %1").arg(error));
}
