#include "ui/control/control_window.h"
#include "ui/control/control_window_view_helpers.h"

#include <QAction>
#include <QActionGroup>
#include <QBoxLayout>
#include <QFontMetrics>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

/*
 * Adds a checkable action to a toolbar menu and optional exclusive group.
 */
QAction *ControlWindow::addToolbarCheckedAction(QMenu *menu, QActionGroup *group, const QString &text, const QString &data, bool checked)
{
    QAction *action = menu->addAction(text);
    action->setCheckable(true);
    action->setData(data);
    action->setChecked(checked);
    if (group)
        group->addAction(action);
    return action;
}

/*
 * Creates the toolbar statistics label and sizes it for localized text.
 */
void ControlWindow::createToolbarStatsLabel(QVBoxLayout *mainLayout)
{
    m_statsLabel = new QLabel(tr("FPS: -- | Kbps: -- | Resolution: -- | Video: --\nCapture: -- | Encoder: -- | Audio: -- | Network: -- | Display: --"), m_floatingToolbar);
    m_statsLabel->setToolTip(tr("Shows FPS, bitrate, resolution, codec, capture method, encoder, audio, network path, and display mode"));
    m_statsLabel->setAlignment(Qt::AlignCenter);
    m_statsLabel->setWordWrap(true);

    QFontMetrics statsFm(m_statsLabel->font());
    const int statsTextW = controlWindowTextWidth(statsFm, m_statsLabel->text());
    const int minAdaptive = qMax(220, statsTextW + 24);
    m_statsLabel->setMinimumWidth(minAdaptive);
    mainLayout->addWidget(m_statsLabel);
}

/*
 * Creates the toolbar button row and horizontal layout.
 */
void ControlWindow::createToolbarButtonRow()
{
    m_toolbarButtonRow = new QWidget(m_floatingToolbar);
    m_toolbarButtonRow->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_toolbarButtonLayout = new QBoxLayout(QBoxLayout::LeftToRight, m_toolbarButtonRow);
    m_toolbarButtonLayout->setSizeConstraint(QLayout::SetFixedSize);
    m_toolbarButtonLayout->setSpacing(4);
    m_toolbarButtonLayout->setContentsMargins(0, 0, 0, 0);
}

/*
 * Creates a top-level toolbar menu button and mirrors the selected action in the tooltip.
 */
void ControlWindow::addToolbarMenuButton(const QString &title, QMenu *menu, QActionGroup *group)
{
    if (!menu || !group)
        return;

    auto *button = new QToolButton(m_floatingToolbar);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setMenu(menu);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setToolTip(title);

    auto updateButton = [button, title, group]()
    {
        QAction *checked = group->checkedAction();
        QString current = checked ? checked->text() : QString();
        current.remove(QChar('&'));
        button->setText(title);
        button->setToolTip(current.isEmpty() ? title : QStringLiteral("%1: %2").arg(title, current));
    };

    connect(group, &QActionGroup::triggered, button, [updateButton](QAction *)
            { updateButton(); });
    for (QAction *action : group->actions())
        connect(action, &QAction::changed, button, updateButton);
    updateButton();
    fitControlToolButtonWidthToText(button);

    button->setMinimumHeight(0);
    m_toolbarButtonLayout->addWidget(button);
    m_sideMenuButtons.append(button);
}
