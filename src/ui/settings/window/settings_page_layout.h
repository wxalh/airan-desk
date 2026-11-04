#ifndef AIRAN_SETTINGS_PAGE_LAYOUT_H
#define AIRAN_SETTINGS_PAGE_LAYOUT_H

#include "ui/common/adaptive_ui.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyleOptionButton>
#include <QTabWidget>
#include <QTextLayout>
#include <QTimer>

namespace SettingsPageLayout
{
inline void allowHorizontalShrink(QWidget *widget)
{
    QSizePolicy policy = widget->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Ignored);
    if (widget->sizePolicy() != policy)
        widget->setSizePolicy(policy);
    if (widget->minimumWidth() != 0)
        widget->setMinimumWidth(0);
}

inline bool isField(QWidget *widget)
{
    return qobject_cast<QLineEdit *>(widget) || qobject_cast<QComboBox *>(widget) ||
           qobject_cast<QSpinBox *>(widget);
}

inline QString originalButtonText(QAbstractButton *button)
{
    // Preserve translated/runtime text, and undo inserted line breaks on resize.
    const QString rendered = button->property("settingsRenderedText").toString();
    if (!button->property("settingsOriginalText").isValid() || button->text() != rendered)
        button->setProperty("settingsOriginalText", button->text());
    return button->property("settingsOriginalText").toString();
}

inline QSize naturalButtonSize(QAbstractButton *button)
{
    QStyleOptionButton option;
    option.initFrom(button);
    option.text = originalButtonText(button);
    option.icon = button->icon();
    option.iconSize = button->iconSize();
    QSize textSize = button->fontMetrics().size(Qt::TextShowMnemonic, option.text);
    if (!option.icon.isNull())
        textSize.rwidth() += option.iconSize.width() + UiAdaptive::scaled(8);
    return button->style()->sizeFromContents(qobject_cast<QCheckBox *>(button)
                                                 ? QStyle::CT_CheckBox : QStyle::CT_PushButton,
                                             &option, textSize, button);
}

inline int preferredWidth(QWidget *widget)
{
    if (auto *button = qobject_cast<QAbstractButton *>(widget))
        return naturalButtonSize(button).width();
    if (isField(widget))
        return qBound(UiAdaptive::scaled(100), widget->sizeHint().width(), UiAdaptive::scaled(200));
    if (auto *label = qobject_cast<QLabel *>(widget))
        return qMin(label->fontMetrics().size(Qt::TextShowMnemonic, label->text()).width(),
                    UiAdaptive::scaled(200));
    if (QLayout *layout = widget->layout())
    {
        int width = 0;
        for (int i = 0; i < layout->count(); ++i)
            if (QWidget *child = layout->itemAt(i)->widget())
                if (!child->isHidden())
                    width = qMax(width, preferredWidth(child));
        return qMin(width, UiAdaptive::scaled(260));
    }
    return qMin(widget->sizeHint().width(), UiAdaptive::scaled(200));
}

inline void wrapButtonText(QAbstractButton *button, int availableWidth)
{
    const QString original = originalButtonText(button);
    if (naturalButtonSize(button).width() <= availableWidth)
    {
        button->setProperty("settingsRenderedText", original);
        if (button->text() != original)
            button->setText(original);
        return;
    }
    const QFontMetrics metrics(button->font());
    const int extra = qMax(0, naturalButtonSize(button).width() -
                              metrics.size(Qt::TextShowMnemonic, original).width());
    const int textWidth = qMax(1, availableWidth - extra - UiAdaptive::scaled(2));
    QStringList lines;
    for (const QString &paragraph : original.split(QLatin1Char('\n')))
    {
        QTextLayout text(paragraph, button->font());
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        text.setTextOption(option);
        text.beginLayout();
        while (true)
        {
            QTextLine line = text.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(textWidth);
            lines.append(paragraph.mid(line.textStart(), line.textLength()).trimmed());
        }
        text.endLayout();
        if (paragraph.isEmpty())
            lines.append(QString());
    }
    const QString wrapped = lines.join(QLatin1Char('\n'));
    button->setProperty("settingsRenderedText", wrapped);
    if (button->text() != wrapped)
        button->setText(wrapped);
}

inline void reflowGroup(QBoxLayout *box, int availableWidth)
{
    int requiredWidth = 0;
    int count = 0;
    bool hasField = false;
    const int spacing = UiAdaptive::scaled(12);
    for (int i = 0; i < box->count(); ++i)
        if (QWidget *child = box->itemAt(i)->widget())
        {
            if (child->isHidden())
                continue;
            requiredWidth += preferredWidth(child);
            hasField = hasField || isField(child);
            ++count;
        }
    requiredWidth += qMax(0, count - 1) * spacing;
    const bool stacked = requiredWidth > availableWidth;
    const QBoxLayout::Direction direction = stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    if (box->direction() != direction)
        box->setDirection(direction);
    if (box->spacing() != spacing)
        box->setSpacing(spacing);
    const bool compactActions = hasField || box->objectName() == QStringLiteral("audioToolsLayout");
    for (int i = 0; i < box->count(); ++i)
    {
        QLayoutItem *item = box->itemAt(i);
        QWidget *child = item->widget();
        if (compactActions && child)
        {
            // Ignored lets fields shrink on narrow pages. Give only the field
            // horizontal stretch so the remaining width cannot enlarge actions.
            const int stretch = !stacked && isField(child) ? 1 : 0;
            if (box->stretch(i) != stretch)
                box->setStretch(i, stretch);
            if (auto *button = qobject_cast<QPushButton *>(child))
            {
                QSizePolicy policy = button->sizePolicy();
                policy.setHorizontalPolicy(QSizePolicy::Fixed);
                if (button->sizePolicy() != policy)
                    button->setSizePolicy(policy);
                const Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter;
                if (item->alignment() != alignment)
                    box->setAlignment(button, alignment);
            }
        }
        if (QSpacerItem *spacer = item->spacerItem())
        {
            const QSizePolicy::Policy horizontal = stacked ? QSizePolicy::Minimum : QSizePolicy::Expanding;
            if (spacer->sizeHint() != QSize(0, 0) || spacer->sizePolicy().horizontalPolicy() != horizontal)
            {
                spacer->changeSize(0, 0, horizontal, QSizePolicy::Minimum);
                box->invalidate();
            }
        }
        else if (auto *button = qobject_cast<QAbstractButton *>(item->widget()))
            wrapButtonText(button, stacked ? availableWidth : naturalButtonSize(button).width());
    }
}

inline void applyPage(QScrollArea *scroll)
{
    QWidget *page = scroll->widget();
    QLayout *layout = page ? page->layout() : nullptr;
    if (!layout || scroll->viewport()->width() <= 0)
        return;

    const int viewportWidth = scroll->viewport()->width();
    const int horizontalMargin = UiAdaptive::scaled(24);
    const int verticalMargin = UiAdaptive::scaled(20);
    const QMargins margins(horizontalMargin, verticalMargin, horizontalMargin, verticalMargin);
    if (layout->contentsMargins() != margins)
        layout->setContentsMargins(margins);
    if (layout->sizeConstraint() != QLayout::SetNoConstraint)
        layout->setSizeConstraint(QLayout::SetNoConstraint);
    allowHorizontalShrink(page);
    if (layout->spacing() != UiAdaptive::scaled(16))
        layout->setSpacing(UiAdaptive::scaled(16));

    for (QWidget *widget : page->findChildren<QWidget *>())
    {
        if (auto *label = qobject_cast<QLabel *>(widget))
        {
            if (!label->wordWrap())
                label->setWordWrap(true);
            QSizePolicy policy = label->sizePolicy();
            policy.setHorizontalPolicy(QSizePolicy::Preferred);
            if (label->sizePolicy() != policy)
                label->setSizePolicy(policy);
            if (label->minimumWidth() != 0)
                label->setMinimumWidth(0);
        }
        else if (isField(widget))
        {
            allowHorizontalShrink(widget);
            if (widget->minimumHeight() != UiAdaptive::scaled(34))
                widget->setMinimumHeight(UiAdaptive::scaled(34));
        }
        else if (widget->layout())
            allowHorizontalShrink(widget);
    }

    const int contentWidth = qMax(1, viewportWidth - horizontalMargin * 2);
    if (auto *form = qobject_cast<QFormLayout *>(layout))
    {
        int labelWidth = 0;
        int fieldWidth = UiAdaptive::scaled(160);
        for (int row = 0; row < form->rowCount(); ++row)
        {
            if (QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole))
                if (auto *label = qobject_cast<QLabel *>(item->widget()))
                    if (!label->isHidden())
                        labelWidth = qMax(labelWidth, label->fontMetrics().size(Qt::TextShowMnemonic, label->text()).width());
            if (QLayoutItem *item = form->itemAt(row, QFormLayout::FieldRole))
                if (item->widget() && !item->widget()->isHidden())
                    fieldWidth = qMax(fieldWidth, preferredWidth(item->widget()));
        }
        const int gap = UiAdaptive::scaled(16);
        if (form->horizontalSpacing() != gap)
            form->setHorizontalSpacing(gap);
        const bool wrapped = labelWidth + gap + fieldWidth > contentWidth;
        const auto policy = wrapped ? QFormLayout::WrapAllRows : QFormLayout::DontWrapRows;
        if (form->rowWrapPolicy() != policy)
            form->setRowWrapPolicy(policy);
        const Qt::Alignment alignment = (wrapped ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter;
        if (form->labelAlignment() != alignment)
            form->setLabelAlignment(alignment);
        if (form->fieldGrowthPolicy() != QFormLayout::AllNonFixedFieldsGrow)
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    }

    // Measure each real field slot after assigning the scroll viewport's width.
    // Intrinsic horizontal minima must not enlarge the page before reflowing it.
    if (page->width() != viewportWidth)
        page->resize(viewportWidth, page->height());
    layout->activate();
    for (QBoxLayout *box : page->findChildren<QBoxLayout *>())
        if (box != layout && box->parentWidget()->parentWidget() == page)
            reflowGroup(box, qMax(1, box->parentWidget()->contentsRect().width()));
    layout->activate();
    for (QAbstractButton *button : page->findChildren<QAbstractButton *>())
        if (button->parentWidget() == page)
            wrapButtonText(button, qMax(1, button->width()));
    layout->activate();
    const int heightForWidth = layout->hasHeightForWidth() ? layout->totalHeightForWidth(viewportWidth) : 0;
    const int minimumHeight = qMax(layout->minimumSize().height(), heightForWidth);
    if (page->minimumHeight() != minimumHeight)
        page->setMinimumHeight(minimumHeight);
}

inline void apply(QTabWidget *tabs, QDialogButtonBox *buttons)
{
    if (!tabs)
        return;
    if (buttons)
    {
        QWidget *content = buttons->parentWidget();
        QLayout *contentLayout = content ? content->layout() : nullptr;
        if (contentLayout)
        {
            const QMargins margins(UiAdaptive::scaled(24), UiAdaptive::scaled(20),
                                   UiAdaptive::scaled(24), UiAdaptive::scaled(20));
            if (contentLayout->contentsMargins() != margins)
                contentLayout->setContentsMargins(margins);
            if (contentLayout->spacing() != UiAdaptive::scaled(16))
                contentLayout->setSpacing(UiAdaptive::scaled(16));
            if (auto *box = qobject_cast<QBoxLayout *>(contentLayout))
            {
                const int tabIndex = box->indexOf(tabs);
                const int footerIndex = box->indexOf(buttons);
                if (tabIndex >= 0 && box->stretch(tabIndex) != 1)
                    box->setStretch(tabIndex, 1);
                if (footerIndex >= 0 && box->stretch(footerIndex) != 0)
                    box->setStretch(footerIndex, 0);
            }
        }
        int requiredWidth = 0;
        const auto buttonList = buttons->buttons();
        for (QAbstractButton *button : buttonList)
            requiredWidth += button->sizeHint().width();
        requiredWidth += qMax(0, buttonList.size() - 1) * UiAdaptive::scaled(12);
        const QMargins margins = contentLayout ? contentLayout->contentsMargins() : QMargins();
        const int availableWidth = content
            ? qMax(1, content->contentsRect().width() - margins.left() - margins.right()) : tabs->width();
        const Qt::Orientation orientation = requiredWidth > availableWidth ? Qt::Vertical : Qt::Horizontal;
        if (buttons->orientation() != orientation)
            buttons->setOrientation(orientation);
        // Qt rotates the button box's default size policy with its orientation.
        // Keep it a full-width footer, and let the tabs consume spare height.
        const QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (buttons->sizePolicy() != policy)
            buttons->setSizePolicy(policy);
        if (contentLayout)
            contentLayout->activate();
    }
    for (int i = 0; i < tabs->count(); ++i)
        if (auto *scroll = qobject_cast<QScrollArea *>(tabs->widget(i)))
            applyPage(scroll);
}

class Controller : public QObject
{
public:
    Controller(QTabWidget *tabs, QDialogButtonBox *buttons, QObject *parent)
        : QObject(parent), m_tabs(tabs), m_buttons(buttons)
    {
        for (int i = 0; i < tabs->count(); ++i)
        {
            QWidget *page = tabs->widget(i);
            const QString title = tabs->tabText(i);
            auto *scroll = new QScrollArea(tabs);
            scroll->setObjectName(QStringLiteral("settingsPageScrollArea"));
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            tabs->removeTab(i);
            scroll->setWidget(page);
            tabs->insertTab(i, scroll, title);
            scroll->viewport()->installEventFilter(this);
            page->installEventFilter(this);
        }
        tabs->installEventFilter(this);
        connect(tabs, &QTabWidget::currentChanged, this, [this]() { schedule(); });
        schedule();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest ||
            event->type() == QEvent::Show || event->type() == QEvent::StyleChange ||
            event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange)
            schedule();
        return QObject::eventFilter(watched, event);
    }

private:
    void schedule()
    {
        if (m_pending)
            return;
        m_pending = true;
        QTimer::singleShot(0, this, [this]() {
            apply(m_tabs, m_buttons);
            m_pending = false;
        });
    }
    QTabWidget *m_tabs;
    QDialogButtonBox *m_buttons;
    bool m_pending{false};
};
}

#endif
