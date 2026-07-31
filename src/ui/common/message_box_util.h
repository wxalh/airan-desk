#ifndef MESSAGE_BOX_UTIL_H
#define MESSAGE_BOX_UTIL_H

#include <QApplication>
#include <QMessageBox>
#include <QTimer>

#include "util/config/config_util.h"

namespace UiMessageBox
{

inline QMessageBox::StandardButton suppressedButton(QMessageBox::StandardButtons buttons,
                                                    QMessageBox::StandardButton defaultButton)
{
    if (defaultButton != QMessageBox::NoButton)
        return defaultButton;
    if (buttons.testFlag(QMessageBox::Ok))
        return QMessageBox::Ok;
    return QMessageBox::NoButton;
}

inline QMessageBox::StandardButton critical(QWidget *parent,
                                            const QString &title,
                                            const QString &text,
                                            QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                            QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    if (!RuntimeEnvironment::uiAvailable())
        return suppressedButton(buttons, defaultButton);

    QWidget *effectiveParent = parent ? parent : QApplication::activeWindow();
    QMessageBox box(QMessageBox::Critical, title, text, buttons, effectiveParent);
    box.setWindowModality(Qt::ApplicationModal);
    box.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    if (defaultButton != QMessageBox::NoButton)
        box.setDefaultButton(defaultButton);

    QTimer::singleShot(0, &box, [&box]() {
        box.raise();
        box.activateWindow();
    });
    return static_cast<QMessageBox::StandardButton>(box.exec());
}


inline QMessageBox::StandardButton warning(QWidget *parent,
                                           const QString &title,
                                           const QString &text,
                                           QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                           QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    if (!RuntimeEnvironment::uiAvailable())
        return suppressedButton(buttons, defaultButton);

    QWidget *effectiveParent = parent ? parent : QApplication::activeWindow();
    QMessageBox box(QMessageBox::Warning, title, text, buttons, effectiveParent);
    box.setWindowModality(Qt::ApplicationModal);
    box.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    if (defaultButton != QMessageBox::NoButton)
        box.setDefaultButton(defaultButton);

    QTimer::singleShot(0, &box, [&box]() {
        box.raise();
        box.activateWindow();
    });
    return static_cast<QMessageBox::StandardButton>(box.exec());
}
} // namespace UiMessageBox

#endif // MESSAGE_BOX_UTIL_H
