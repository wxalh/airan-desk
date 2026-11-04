#ifndef DEVICE_DIALOG_LAYOUT_H
#define DEVICE_DIALOG_LAYOUT_H

#include "ui/common/adaptive_ui.h"
#include "ui/chrome/app_title_bar.h"

#include <QDialog>
#include <QEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace DeviceDialogLayout
{
inline QVBoxLayout *createWindowLayout(QDialog *dialog, bool showMinimize = false)
{
    dialog->setWindowFlags((dialog->windowFlags() | Qt::FramelessWindowHint |
                           Qt::WindowMaximizeButtonHint) & ~Qt::WindowContextHelpButtonHint);
    dialog->setAttribute(Qt::WA_StyledBackground, true);
    auto *root = new QVBoxLayout(dialog);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *titleBar = new AppTitleBar(dialog, showMinimize, true, dialog);
    // These controls are window actions, so Enter must stay with the dialog's
    // ordinary default button instead of activating a title-bar action.
    for (QPushButton *button : titleBar->findChildren<QPushButton *>())
    {
        button->setAutoDefault(false);
        button->setDefault(false);
    }
    root->addWidget(titleBar);
    auto *body = new QWidget(dialog);
    body->setObjectName(QStringLiteral("deviceDialogContent"));
    auto *content = new QVBoxLayout(body);
    content->setContentsMargins(UiAdaptive::scaled(24), UiAdaptive::scaled(20),
                               UiAdaptive::scaled(24), UiAdaptive::scaled(20));
    content->setSpacing(UiAdaptive::scaled(12));
    root->addWidget(body, 1);
    return content;
}

inline void fitVisibleFrame(QDialog *dialog, const QRect &available)
{
    if (!dialog || !available.isValid() || dialog->isMaximized() || dialog->isFullScreen())
        return;
    const QSize frame = dialog->frameGeometry().size() - dialog->size();
    const QSize maximum(qMax(1, available.width() - qMax(0, frame.width())),
                        qMax(1, available.height() - qMax(0, frame.height())));
    const QSize minimum = dialog->minimumSize().boundedTo(maximum);
    dialog->setMinimumSize(minimum);
    dialog->setMaximumSize(maximum);
    dialog->resize(dialog->size().boundedTo(maximum).expandedTo(minimum));

    const QRect bounds = dialog->frameGeometry();
    const QPoint target(qBound(available.left(), bounds.left(), qMax(available.left(), available.right() - bounds.width() + 1)),
                        qBound(available.top(), bounds.top(), qMax(available.top(), available.bottom() - bounds.height() + 1)));
    dialog->move(dialog->pos() + target - bounds.topLeft());
}

class ScreenFitFilter final : public QObject
{
public:
    explicit ScreenFitFilter(QDialog *dialog) : QObject(dialog), m_dialog(dialog)
    {
        dialog->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_dialog && (event->type() == QEvent::Show || event->type() == QEvent::Resize ||
                event->type() == QEvent::ScreenChangeInternal || event->type() == QEvent::WindowStateChange ||
                event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::StyleChange))
        {
            if (!m_pending)
            {
                m_pending = true;
                QTimer::singleShot(0, this, [this]() {
                    m_pending = false;
                    if (!m_dialog || !m_dialog->isVisible())
                        return;
                    const int margin = UiAdaptive::scaled(8);
                    fitVisibleFrame(m_dialog, UiAdaptive::availableGeometry(m_dialog)
                        .adjusted(margin, margin, -margin, -margin));
                });
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QPointer<QDialog> m_dialog;
    bool m_pending{false};
};

inline QVBoxLayout *addScrollableContent(QDialog *dialog, QVBoxLayout *outer, const QString &name)
{
    auto *scroll = new QScrollArea(dialog);
    scroll->setObjectName(name);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setMinimumSize(0, 0);
    auto *body = new QWidget(scroll);
    auto *content = new QVBoxLayout(body);
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(UiAdaptive::scaled(12));
    content->setSizeConstraint(QLayout::SetMinAndMaxSize);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);
    return content;
}

inline QSize constrainToScreen(QDialog *dialog, const QSize &preferred, const QSize &minimum)
{
    if (!dialog)
        return preferred;
    dialog->setWindowFlags((dialog->windowFlags() | Qt::WindowMaximizeButtonHint) &
                           ~Qt::WindowContextHelpButtonHint);
    const QRect available = UiAdaptive::availableGeometry(dialog)
                                .adjusted(UiAdaptive::scaled(8), UiAdaptive::scaled(8),
                                          -UiAdaptive::scaled(8), -UiAdaptive::scaled(8));
    const QSize maximum(qMax(1, available.width()), qMax(1, available.height()));
    const QSize scaledPreferred = UiAdaptive::scaledSize(preferred, 1.0);
    const QSize scaledMinimum = UiAdaptive::scaledSize(minimum, 1.0);
    const QSize adaptiveMinimum(qMax(1, qMin(scaledMinimum.width(), maximum.width())),
                                qMax(1, qMin(scaledMinimum.height(), maximum.height())));
    const QSize target(qMin(scaledPreferred.width(), maximum.width()),
                       qMin(scaledPreferred.height(), maximum.height()));
    dialog->setMinimumSize(adaptiveMinimum);
    dialog->setMaximumSize(maximum);
    dialog->resize(target.expandedTo(adaptiveMinimum));
    if (!dialog->property("deviceScreenFitInstalled").toBool())
    {
        new ScreenFitFilter(dialog);
        dialog->setProperty("deviceScreenFitInstalled", true);
    }
    return dialog->size();
}
}

#endif
