#include "app_title_bar.h"
#include "app_title_bar_icons.h"

#include <QApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QWindow>


void AppTitleBar::setUiScale(double scale)
{
    m_customScale = true;
    m_scale = scale > 0.0 ? scale : 1.0;
    applyScale();
}


void AppTitleBar::applyScale()
{
    const double scaleValue = effectiveScale();
    if (m_layout)
    {
        m_layout->setContentsMargins(scaled(14), 0, scaled(8), 0);
        m_layout->setSpacing(scaled(6));
    }
    if (m_titleLabel)
    {
        QFont font = m_titleLabel->font();
        font.setPointSizeF(13.0 * scaleValue / dpiScale());
        font.setBold(true);
        m_titleLabel->setFont(font);
    }
    if (m_iconLabel)
    {
        const QSize iconBox(scaled(20), scaled(20));
        const QSize pixmapSize(scaled(18), scaled(18));
        m_iconLabel->setFixedSize(iconBox);
        const QIcon appIcon = (m_targetWindow && !m_targetWindow->windowIcon().isNull())
                                  ? m_targetWindow->windowIcon()
                                  : qApp->windowIcon();
        m_iconLabel->setPixmap(appIcon.pixmap(pixmapSize));
    }
    if (m_minimizeButton)
    {
        m_minimizeButton->setFixedSize(scaled(36), scaled(28));
        m_minimizeButton->setIconSize(QSize(scaled(12), scaled(12)));
    }
    if (m_maximizeButton)
    {
        m_maximizeButton->setFixedSize(scaled(36), scaled(28));
        m_maximizeButton->setIconSize(QSize(scaled(12), scaled(12)));
    }
    if (m_closeButton)
    {
        m_closeButton->setFixedSize(scaled(36), scaled(28));
        m_closeButton->setIconSize(QSize(scaled(12), scaled(12)));
    }
    setFixedHeight(scaled(38));
    applyStyle();
    emit uiScaleChanged();
}


QPushButton *AppTitleBar::createButton(const QIcon &icon, const QString &tooltip)
{
    auto *button = new QPushButton(this);
    button->setIcon(icon);
    button->setIconSize(QSize(12, 12));
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tooltip);
    button->setCursor(Qt::ArrowCursor);
    return button;
}


int AppTitleBar::scaled(int value) const
{
    return qMax(1, static_cast<int>(qRound(value * effectiveScale())));
}


double AppTitleBar::dpiScale() const
{
    const QWidget *window = m_targetWindow ? m_targetWindow->window() : this->window();
    QScreen *screen = nullptr;
    if (window && window->windowHandle())
        screen = window->windowHandle()->screen();
    if (!screen && this->windowHandle())
        screen = this->windowHandle()->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return screen ? qBound(0.75, screen->logicalDotsPerInch() / 96.0, 3.0) : 1.0;
}


double AppTitleBar::effectiveScale() const
{
    return m_scale * (m_customScale ? 1.0 : dpiScale());
}


void AppTitleBar::applyStyle()
{
    setStyleSheet(QStringLiteral(
        "#appTitleBar { background: #181818; border-bottom: 1px solid #2a2a2a; }"
        "#appTitleBarTitle { color: rgb(131,193,224); font-weight: 600; }"
        "QPushButton { background: transparent; border: none; color: rgb(131,193,224);"
        "              border-radius: 4px; padding: 0; }"
        "QPushButton:hover { background: #2a2a2a; color: white; }"
        "QPushButton:pressed { background: #3a3a3a; color: white; }"
        "#appTitleBarClose:hover { background: #b23b4c; color: white; }"));
}
