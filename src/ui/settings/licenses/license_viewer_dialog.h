#ifndef LICENSE_VIEWER_DIALOG_H
#define LICENSE_VIEWER_DIALOG_H

#include "license_catalog.h"

#include <QDialog>

class QListWidget;
class QPlainTextEdit;

class LicenseViewerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LicenseViewerDialog(QWidget *parent = nullptr);

private:
    void showEntry(int row);

    QList<LicenseCatalog::Entry> m_entries;
    QListWidget *m_list{nullptr};
    QPlainTextEdit *m_text{nullptr};
};

#endif /* LICENSE_VIEWER_DIALOG_H */
