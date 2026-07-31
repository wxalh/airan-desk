#include "ui/control/control_window.h"
#include "ui/control/connection/control_window_connection_progress_state.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QStringList>

using namespace ControlConnectionProgress;

namespace
{
int textWidth(const QFontMetrics &metrics, const QString &text)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    return metrics.horizontalAdvance(text);
#else
    return metrics.width(text);
#endif
}

struct ProgressStepPaint
{
    QColor color;
    QColor lineColor;
    QString statusText;
    bool emphasized = false;
};

ProgressStepPaint progressStepPaint(int state, int step)
{
    ProgressStepPaint paint{QColor(QStringLiteral("#7f8794")),
                            QColor(QStringLiteral("#3b4656")),
                            QCoreApplication::translate("ControlWindow", "Waiting"),
                            false};
    if (state == StepRunning)
    {
        paint.color = QColor(QStringLiteral("#4aa3ff"));
        paint.lineColor = paint.color;
        paint.statusText = QCoreApplication::translate("ControlWindow", "In progress");
        paint.emphasized = true;
    }
    else if (state == StepDone)
    {
        paint.color = QColor(QStringLiteral("#3fd17f"));
        paint.lineColor = paint.color;
        paint.statusText = (step == StepConnectionResult)
                               ? QCoreApplication::translate("ControlWindow", "Succeeded")
                               : QCoreApplication::translate("ControlWindow", "Completed");
    }
    else if (state == StepFailed)
    {
        paint.color = QColor(QStringLiteral("#ff5c5c"));
        paint.lineColor = paint.color;
        paint.statusText = QCoreApplication::translate("ControlWindow", "Failed");
        paint.emphasized = true;
    }
    return paint;
}

void drawCenteredText(QPainter &painter,
                      const QRect &rect,
                      const QFont &font,
                      const QColor &color,
                      const QString &text)
{
    painter.setFont(font);
    painter.setPen(color);
    const QFontMetrics metrics(font);
    painter.drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter,
                     metrics.elidedText(text, Qt::ElideRight, rect.width()));
}

void drawLeftAlignedText(QPainter &painter,
                         const QRect &rect,
                         const QFont &font,
                         const QColor &color,
                         const QString &text)
{
    painter.setFont(font);
    painter.setPen(color);
    const QFontMetrics metrics(font);
    painter.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(text, Qt::ElideRight, rect.width()));
}

void drawStepNumber(QPainter &painter,
                    const QPoint &center,
                    int diameter,
                    const QFont &font,
                    const QColor &color,
                    int step)
{
    const QRect circleRect(center.x() - diameter / 2,
                           center.y() - diameter / 2,
                           diameter,
                           diameter);
    QPen pen(color, 2);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(circleRect);
    drawCenteredText(painter, circleRect, font, color, QString::number(step + 1));
}
}

/*
 * Resets the connection progress and starts from the local decoder step.
 */
void ControlWindow::resetConnectionProgress()
{
    m_connectionStepStates.clear();
    ensureConnectionSteps(m_connectionStepStates);
    m_connectionStepStates[StepLocalDecoder] = StepRunning;
    m_connectionFailureReason.clear();
    renderConnectionProgress();
}

/*
 * Renders centered connection progress with stable left-aligned text.
 */
void ControlWindow::renderConnectionProgress()
{
    if (isReceivedImg)
        return;

    ensureConnectionSteps(m_connectionStepStates);
    const QStringList titles = {
        tr("Initializing local decoder..."),
        tr("Sending connection request..."),
        tr("Initializing remote encoder..."),
        tr("Negotiating codec..."),
        tr("Connection result"),
        tr("Waiting for video...")};
    const QStringList statusSamples = {
        tr("Waiting"),
        tr("In progress"),
        tr("Completed"),
        tr("Succeeded"),
        tr("Transfer idle"),
        tr("Failed")};

    const QSize viewportSize = scrollArea.viewport()->size();
    if (!viewportSize.isValid() || viewportSize.isEmpty())
        return;

    QFont pendingTitleFont = label.font();
    pendingTitleFont.setPixelSize(16);
    QFont activeTitleFont = label.font();
    activeTitleFont.setPixelSize(20);
    activeTitleFont.setBold(true);
    QFont statusFont = label.font();
    statusFont.setPixelSize(13);
    QFont reasonFont = label.font();
    reasonFont.setPixelSize(14);

    const QFontMetrics pendingTitleMetrics(pendingTitleFont);
    const QFontMetrics activeTitleMetrics(activeTitleFont);
    const QFontMetrics statusMetrics(statusFont);
    const QFontMetrics reasonMetrics(reasonFont);
    int maxTextWidth = 0;
    for (const QString &title : titles)
    {
        maxTextWidth = qMax(maxTextWidth, textWidth(pendingTitleMetrics, title));
        maxTextWidth = qMax(maxTextWidth, textWidth(activeTitleMetrics, title));
    }
    for (const QString &status : statusSamples)
        maxTextWidth = qMax(maxTextWidth, textWidth(statusMetrics, status));
    if (!m_connectionFailureReason.isEmpty())
    {
        maxTextWidth = qMax(maxTextWidth,
                            textWidth(reasonMetrics, tr("Reason: %1").arg(m_connectionFailureReason)));
    }

    const int viewportCenterX = viewportSize.width() / 2;
    const int horizontalMargin = 24;
    const int numberColumnWidth = 38;
    const int numberGap = 16;
    const int textFrameWidth = qMin(qMax(260, maxTextWidth + 28),
                                    qMax(180, viewportSize.width() - horizontalMargin * 2 - numberColumnWidth - numberGap));
    const int contentWidth = numberColumnWidth + numberGap + textFrameWidth;
    const int contentLeft = qMax(horizontalMargin, viewportCenterX - contentWidth / 2);
    const int numberCenterX = contentLeft + numberColumnWidth / 2;
    const int textLeft = contentLeft + numberColumnWidth + numberGap;
    const int titleHeight = qMax(QFontMetrics(activeTitleFont).height(), QFontMetrics(pendingTitleFont).height());
    const int statusHeight = QFontMetrics(statusFont).height();
    const int rowHeight = titleHeight + statusHeight + 8;
    const int connectorHeight = 24;
    const int reasonHeight = m_connectionFailureReason.isEmpty() ? 0 : QFontMetrics(reasonFont).height() + 8;
    const int totalHeight = kConnectionStepCount * rowHeight +
                            (kConnectionStepCount - 1) * connectorHeight +
                            reasonHeight;
    int y = qMax(0, (viewportSize.height() - totalHeight) / 2);

    QPixmap pixmap(viewportSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    for (int i = 0; i < kConnectionStepCount; ++i)
    {
        const int state = m_connectionStepStates.value(i, StepPending);
        const ProgressStepPaint stepPaint = progressStepPaint(state, i);
        QFont numberFont = label.font();
        numberFont.setPixelSize(stepPaint.emphasized ? 17 : 15);
        numberFont.setBold(stepPaint.emphasized);
        QFont titleFont = stepPaint.emphasized ? activeTitleFont : pendingTitleFont;
        if (state == StepDone)
            titleFont.setBold(true);

        drawStepNumber(painter,
                       QPoint(numberCenterX, y + titleHeight / 2),
                       stepPaint.emphasized ? 34 : 30,
                       numberFont,
                       stepPaint.color,
                       i);
        drawLeftAlignedText(painter,
                            QRect(textLeft, y, textFrameWidth, titleHeight),
                            titleFont,
                            stepPaint.color,
                            titles.value(i));
        drawLeftAlignedText(painter,
                            QRect(textLeft, y + titleHeight + 4, textFrameWidth, statusHeight),
                            statusFont,
                            state == StepPending ? QColor(QStringLiteral("#7f8794")) : stepPaint.color,
                            stepPaint.statusText);
        y += rowHeight;

        if (i == StepConnectionResult && state == StepFailed && !m_connectionFailureReason.isEmpty())
        {
            drawLeftAlignedText(painter,
                                QRect(textLeft, y, textFrameWidth, reasonHeight),
                                reasonFont,
                                QColor(QStringLiteral("#ffb3b3")),
                                tr("Reason: %1").arg(m_connectionFailureReason));
            y += reasonHeight;
        }

        if (i + 1 < kConnectionStepCount)
        {
            QPen pen(stepPaint.lineColor, 2);
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);
            const int lineX = numberCenterX;
            painter.drawLine(QPoint(lineX, y + 2), QPoint(lineX, y + connectorHeight - 2));
            y += connectorHeight;
        }
    }

    label.clear();
    label.setPixmap(pixmap);
    label.setAlignment(Qt::AlignCenter);
    label.resize(viewportSize);
}

/*
 * Marks the waiting-for-video step as complete after the first frame arrives.
 */
void ControlWindow::markConnectionWaitingFrameDone()
{
    markStepDone(m_connectionStepStates, StepWaitingFrame);
}
