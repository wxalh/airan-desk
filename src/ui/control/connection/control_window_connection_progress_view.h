#ifndef CONTROL_WINDOW_CONNECTION_PROGRESS_VIEW_H
#define CONTROL_WINDOW_CONNECTION_PROGRESS_VIEW_H

#include "ui/common/adaptive_ui.h"
#include "ui/control/connection/control_window_connection_progress_state.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>
#include <QScrollArea>
#include <QTextLayout>
#include <QTimer>
#include <QVBoxLayout>

namespace ControlConnectionProgress
{
// This page covers the video viewport until the first frame. It never changes
// the renderer's widget, palette or scroll policies.
class View final : public QScrollArea
{
public:
    explicit View(QWidget *videoViewport) : QScrollArea(videoViewport)
    {
        setObjectName(QStringLiteral("connectionProgressView"));
        viewport()->setObjectName(QStringLiteral("connectionProgressViewport"));
        setFrameShape(QFrame::NoFrame);
        setWidgetResizable(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setFocusPolicy(Qt::NoFocus);

        m_content = new QWidget;
        m_content->setObjectName(QStringLiteral("connectionProgressContent"));
        m_content->setMinimumWidth(0);
        m_content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_outer = new QVBoxLayout(m_content);
        m_outer->setSizeConstraint(QLayout::SetNoConstraint);
        auto *center = new QHBoxLayout;
        center->setContentsMargins(0, 0, 0, 0);
        center->setSpacing(0);
        center->addStretch(1);
        m_card = new QFrame(m_content);
        m_card->setObjectName(QStringLiteral("connectionProgressCard"));
        m_cardLayout = new QVBoxLayout(m_card);
        m_cardLayout->setSizeConstraint(QLayout::SetNoConstraint);
        m_heading = createLabel(m_card, QStringLiteral("connectionProgressHeading"));
        m_cardLayout->addWidget(m_heading);
        m_progress = new QProgressBar(m_card);
        m_progress->setRange(0, kConnectionStepCount);
        m_progress->setTextVisible(false);
        m_cardLayout->addWidget(m_progress);

        const QStringList titles = {
            translate("Initializing local decoder..."), translate("Sending connection request..."),
            translate("Initializing remote encoder..."), translate("Negotiating codec..."),
            translate("Connection result"), translate("Waiting for video...")};
        for (int i = 0; i < kConnectionStepCount; ++i)
        {
            auto *row = new QWidget(m_card);
            row->setObjectName(QStringLiteral("connectionProgressStep"));
            auto *layout = new QGridLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setColumnStretch(1, 1);
            auto *number = createLabel(row, QStringLiteral("connectionStepNumber"));
            number->setText(QString::number(i + 1));
            number->setAlignment(Qt::AlignCenter);
            number->setWordWrap(false);
            auto *title = createLabel(row, QStringLiteral("connectionStepTitle"));
            title->setText(titles.value(i));
            auto *status = createLabel(row, QStringLiteral("connectionStepStatus"));
            layout->addWidget(number, 0, 0, 2, 1, Qt::AlignTop);
            layout->addWidget(title, 0, 1);
            layout->addWidget(status, 1, 1);
            m_rows.append(row);
            m_numbers.append(number);
            m_statuses.append(status);
            m_cardLayout->addWidget(row);
        }
        m_reason = createLabel(m_card, QStringLiteral("connectionFailureReason"));
        m_cardLayout->addWidget(m_reason);
        center->addWidget(m_card);
        center->addStretch(1);
        // Equal vertical spacers center a fitting card. The content's minimum
        // height keeps both spacers collapsed when the card needs scrolling.
        m_outer->addStretch(1);
        m_outer->addLayout(center);
        m_outer->addStretch(1);
        setWidget(m_content);
        videoViewport->installEventFilter(this);
        setGeometry(videoViewport->rect());
        hide();
        scheduleRefresh();
    }

    void setProgress(QList<int> states, const QString &failureReason)
    {
        if (m_finished)
            return;
        ensureConnectionSteps(states);
        m_heading->setText(translate(states.contains(StepFailed) ? "Connection failed"
                                                                : "Connecting to remote device"));
        int completed = 0;
        for (int i = 0; i < kConnectionStepCount; ++i)
        {
            const int state = states.value(i);
            completed += state == StepDone ? 1 : 0;
            m_numbers[i]->setProperty("connectionState", stateName(state));
            m_statuses[i]->setProperty("connectionState", stateName(state));
            m_statuses[i]->setText(translate(state == StepRunning ? "In progress" :
                                            state == StepFailed ? "Failed" :
                                            state == StepDone ? (i == StepConnectionResult ? "Succeeded" : "Completed") :
                                            "Waiting"));
        }
        m_progress->setValue(completed);
        m_reason->setText(failureReason.isEmpty() ? QString() : translate("Reason: %1").arg(failureReason));
        m_reason->setVisible(!failureReason.isEmpty());
        // Dynamic property selectors must be refreshed when the step advances.
        for (QLabel *status : m_statuses)
        {
            status->style()->unpolish(status);
            status->style()->polish(status);
        }
        for (QLabel *number : m_numbers)
        {
            number->style()->unpolish(number);
            number->style()->polish(number);
        }
        show();
        raise();
        scheduleRefresh();
    }

    void complete()
    {
        m_finished = true;
        hide();
    }

protected:
    bool event(QEvent *event) override
    {
        const bool result = QScrollArea::event(event);
        if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange ||
            event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::PaletteChange ||
            event->type() == QEvent::ApplicationPaletteChange)
            scheduleRefresh();
        return result;
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parentWidget() && event->type() == QEvent::Resize)
            setGeometry(parentWidget()->rect());
        if (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest)
            scheduleRefresh();
        return QScrollArea::eventFilter(watched, event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QScrollArea::resizeEvent(event);
        scheduleRefresh();
    }

private:
    static QString translate(const char *source)
    {
        return QCoreApplication::translate("ControlWindow", source);
    }

    static QString stateName(int state)
    {
        return state == StepRunning ? QStringLiteral("running") :
               state == StepDone ? QStringLiteral("done") :
               state == StepFailed ? QStringLiteral("failed") : QStringLiteral("pending");
    }

    static QLabel *createLabel(QWidget *parent, const QString &name)
    {
        auto *label = new QLabel(parent);
        label->setObjectName(name);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setMinimumWidth(0);
        QSizePolicy policy = label->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Ignored);
        label->setSizePolicy(policy);
        return label;
    }

    void scheduleRefresh()
    {
        if (m_refreshPending)
            return;
        m_refreshPending = true;
        QTimer::singleShot(0, this, [this]() {
            if (m_content && !m_finished)
            {
                refreshAppearance();
                refreshGeometry();
            }
            m_refreshPending = false;
        });
    }

    void refreshAppearance()
    {
        const QPalette palette = QApplication::palette();
        const bool dark = palette.color(QPalette::Window).lightness() < 128;
        const QString window = palette.color(QPalette::Window).name();
        const QString base = palette.color(QPalette::Base).name();
        const QString text = palette.color(QPalette::Text).name();
        const QString primary = palette.color(QPalette::Highlight).name();
        const QString secondary = dark ? QStringLiteral("#a6abb4") : QStringLiteral("#606266");
        const QString border = dark ? QStringLiteral("#41464d") : QStringLiteral("#e4e7ed");
        const QString success = dark ? QStringLiteral("#95d475") : QStringLiteral("#529b2e");
        const QString danger = dark ? QStringLiteral("#f89898") : QStringLiteral("#c45656");
        const double scale = UiAdaptive::scaleFactor();
        const QString sheet = QStringLiteral(
            "QScrollArea#connectionProgressView, QWidget#connectionProgressViewport, QWidget#connectionProgressContent {"
            "background:%1; border:0; margin:0; padding:0;}"
            "QFrame#connectionProgressCard {background:%2; border:1px solid %3; border-radius:%4px;}"
            "QWidget#connectionProgressStep {background:transparent; border:0;}"
            "QLabel {background:transparent; border:0; margin:0; padding:0; color:%5; font-size:%6pt;}"
            "QLabel#connectionProgressHeading {font-size:%7pt; font-weight:600;}"
            "QLabel#connectionStepStatus {color:%8; font-size:%9pt;}"
            "QLabel#connectionStepNumber {background:%1; color:%8; border-radius:%10px;}"
            "QLabel#connectionStepStatus[connectionState=running] {color:%11;}"
            "QLabel#connectionStepNumber[connectionState=running] {background:%11; color:white;}"
            "QLabel#connectionStepNumber[connectionState=done] {color:%12;}"
            "QLabel#connectionStepNumber[connectionState=failed], QLabel#connectionStepStatus[connectionState=failed], QLabel#connectionFailureReason {color:%13;}"
            "QScrollArea#connectionProgressView QProgressBar {background:%3; border:0; border-radius:%14px;}"
            "QScrollArea#connectionProgressView QProgressBar::chunk {background:%11; border-radius:%14px;}"
            "QScrollBar:vertical {background:%1; border:0; width:%15px;}"
            "QScrollBar::handle:vertical {background:%3; border-radius:%14px; min-height:%16px;}"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {height:0; background:transparent; border:0;}"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {background:transparent;}")
            .arg(window, base, border).arg(UiAdaptive::scaled(8)).arg(text)
            .arg(10.0 * scale, 0, 'f', 1).arg(14.0 * scale, 0, 'f', 1).arg(secondary)
            .arg(9.0 * scale, 0, 'f', 1).arg(UiAdaptive::scaled(12)).arg(primary, success, danger)
            .arg(UiAdaptive::scaled(2)).arg(UiAdaptive::scaled(8)).arg(UiAdaptive::scaled(24));
        if (styleSheet() != sheet)
            setStyleSheet(sheet);
        const int outerMargin = qMin(UiAdaptive::scaled(24), qMax(UiAdaptive::scaled(8), viewport()->width() / 16));
        const int innerMargin = qMin(UiAdaptive::scaled(24), qMax(UiAdaptive::scaled(12), viewport()->width() / 20));
        m_outer->setContentsMargins(outerMargin, UiAdaptive::scaled(24),
                                   outerMargin, UiAdaptive::scaled(24));
        m_outer->setSpacing(0);
        m_cardLayout->setContentsMargins(innerMargin, UiAdaptive::scaled(24),
                                        innerMargin, UiAdaptive::scaled(24));
        m_cardLayout->setSpacing(UiAdaptive::scaled(16));
        m_progress->setFixedHeight(UiAdaptive::scaled(4));
        for (int i = 0; i < m_rows.size(); ++i)
        {
            auto *layout = static_cast<QGridLayout *>(m_rows[i]->layout());
            layout->setHorizontalSpacing(UiAdaptive::scaled(12));
            layout->setVerticalSpacing(UiAdaptive::scaled(3));
            layout->setColumnMinimumWidth(0, UiAdaptive::scaled(24));
            m_numbers[i]->setFixedSize(UiAdaptive::scaled(24), UiAdaptive::scaled(24));
        }
    }

    void refreshGeometry()
    {
        const int width = viewport()->width();
        if (width <= 0)
            return;
        const QMargins margins = m_outer->contentsMargins();
        m_card->setFixedWidth(qMax(1, qMin(UiAdaptive::scaled(600), width - margins.left() - margins.right())));
        const QMargins cardMargins = m_cardLayout->contentsMargins();
        const int textWidth = qMax(1, m_card->width() - cardMargins.left() - cardMargins.right());
        wrapLabel(m_heading, textWidth);
        wrapLabel(m_reason, textWidth);
        for (QWidget *row : m_rows)
        {
            auto *layout = static_cast<QGridLayout *>(row->layout());
            const int rowTextWidth = qMax(1, textWidth - layout->columnMinimumWidth(0) - layout->horizontalSpacing());
            wrapLabel(row->findChild<QLabel *>(QStringLiteral("connectionStepTitle")), rowTextWidth);
            wrapLabel(row->findChild<QLabel *>(QStringLiteral("connectionStepStatus")), rowTextWidth);
        }
        m_cardLayout->activate();
        const int cardHeight = qMax(m_cardLayout->minimumSize().height(),
                                   m_cardLayout->totalHeightForWidth(m_card->width()));
        m_card->setFixedHeight(cardHeight);
        m_content->setMinimumHeight(cardHeight + margins.top() + margins.bottom());
        m_outer->activate();
    }

    static void wrapLabel(QLabel *label, int width)
    {
        const QString rendered = label->property("connectionRenderedText").toString();
        if (!label->property("connectionOriginalText").isValid() || label->text() != rendered)
            label->setProperty("connectionOriginalText", label->text());
        const QString original = label->property("connectionOriginalText").toString();
        QStringList lines;
        for (const QString &paragraph : original.split(QLatin1Char('\n')))
        {
            QTextLayout text(paragraph, label->font());
            QTextOption option;
            option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            text.setTextOption(option);
            text.beginLayout();
            while (true)
            {
                QTextLine line = text.createLine();
                if (!line.isValid())
                    break;
                // Leave room for integer font-metric rounding at fractional scales.
                line.setLineWidth(qMax(1, width - UiAdaptive::scaled(3)));
                lines.append(paragraph.mid(line.textStart(), line.textLength()).trimmed());
            }
            text.endLayout();
            if (paragraph.isEmpty())
                lines.append(QString());
        }
        const QString wrapped = lines.join(QLatin1Char('\n'));
        label->setProperty("connectionRenderedText", wrapped);
        if (label->text() != wrapped)
            label->setText(wrapped);
    }

    QWidget *m_content{nullptr};
    QVBoxLayout *m_outer{nullptr};
    QFrame *m_card{nullptr};
    QVBoxLayout *m_cardLayout{nullptr};
    QLabel *m_heading{nullptr};
    QProgressBar *m_progress{nullptr};
    QLabel *m_reason{nullptr};
    QList<QWidget *> m_rows;
    QList<QLabel *> m_numbers;
    QList<QLabel *> m_statuses;
    bool m_refreshPending{false};
    bool m_finished{false};
};
}

#endif
