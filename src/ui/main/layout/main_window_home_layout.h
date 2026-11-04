#ifndef AIRAN_MAIN_WINDOW_HOME_LAYOUT_H
#define AIRAN_MAIN_WINDOW_HOME_LAYOUT_H

#include "ui/common/adaptive_ui.h"
#include "ui/main/layout/main_window_responsive_layout.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QScrollArea>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>

namespace HomePageLayout
{
struct Controls
{
    QLabel *localTitle;
    QLabel *localIdLabel;
    QLabel *localPwdLabel;
    QLabel *remoteTitle;
    QLabel *remoteIdLabel;
    QLabel *remotePwdLabel;
    QLabel *status;
    QLabel *version;
    QPlainTextEdit *localId;
    QPlainTextEdit *localPwd;
    QLineEdit *remoteId;
    QLineEdit *remotePwd;
    QRadioButton *desktop;
    QRadioButton *file;
    QRadioButton *terminal;
    QPushButton *connect;
    QPushButton *share;
    QPushButton *update;
    QPushButton *devices;
    QPushButton *settings;
};

inline QString text(const char *source)
{
    return QCoreApplication::translate("MainWindow", source);
}

inline void apply(QWidget *content)
{
    auto *scroll = content->findChild<QScrollArea *>(QStringLiteral("homeScrollArea"));
    auto *workspace = content->findChild<QWidget *>(QStringLiteral("homeWorkspace"));
    auto *local = content->findChild<QWidget *>(QStringLiteral("localDevicePanel"));
    auto *remote = content->findChild<QWidget *>(QStringLiteral("remoteDevicePanel"));
    auto *grid = content->findChild<QGridLayout *>(QStringLiteral("mainPanelGrid"));
    if (!scroll || !workspace || !local || !remote || !grid)
        return;

    QWidget *page = scroll->widget();
    const int margin = UiAdaptive::scaled(scroll->viewport()->width() < UiAdaptive::scaled(700) ? 20 : 28);
    page->setMaximumWidth(UiAdaptive::scaled(1120));
    page->layout()->setContentsMargins(margin, UiAdaptive::scaled(28), margin, UiAdaptive::scaled(24));
    page->layout()->setSpacing(UiAdaptive::scaled(20));
    for (const QString &bandName : {QStringLiteral("homeToolbar"), QStringLiteral("homeFooter")})
        if (QWidget *band = content->findChild<QWidget *>(bandName))
        {
            band->layout()->setContentsMargins(margin, UiAdaptive::scaled(10), margin, UiAdaptive::scaled(10));
            band->layout()->setSpacing(UiAdaptive::scaled(24));
        }

    const bool wide = applyMainPanelsResponsiveLayout(grid, local, remote,
                                                       scroll->viewport()->width(), UiAdaptive::scaled(760));
    const bool stacked = !wide;
    if (!workspace->property("stacked").isValid() || workspace->property("stacked").toBool() != stacked)
    {
        workspace->setProperty("stacked", stacked);
        for (QWidget *widget : {workspace, local, remote})
        {
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
            widget->update();
        }
    }
    grid->setHorizontalSpacing(0);
    grid->setVerticalSpacing(0);
    for (QWidget *panel : {local, remote})
    {
        panel->layout()->setContentsMargins(UiAdaptive::scaled(24), UiAdaptive::scaled(24),
                                            UiAdaptive::scaled(24), UiAdaptive::scaled(24));
        panel->layout()->setSpacing(UiAdaptive::scaled(20));
        for (QWidget *field : panel->findChildren<QWidget *>(QStringLiteral("homeFormField")))
            field->layout()->setSpacing(UiAdaptive::scaled(8));
    }
    for (QPlainTextEdit *field : local->findChildren<QPlainTextEdit *>())
    {
        // Two visual lines preserve the entire UUID without reducing its font.
        const int height = qMax(UiAdaptive::scaled(72), field->fontMetrics().lineSpacing() * 2 + UiAdaptive::scaled(24));
        field->setFixedHeight(height);
    }
    for (QLineEdit *field : remote->findChildren<QLineEdit *>())
        field->setMinimumHeight(UiAdaptive::scaled(44));
    if (QPushButton *button = remote->findChild<QPushButton *>(QStringLiteral("btn_conn")))
        button->setMinimumHeight(UiAdaptive::scaled(44));

    page->layout()->activate();
    grid->activate();
    if (auto *modes = remote->findChild<QBoxLayout *>(QStringLiteral("remoteModeLayout")))
    {
        int requiredWidth = UiAdaptive::scaled(8);
        const auto buttons = remote->findChildren<QRadioButton *>();
        for (QRadioButton *button : buttons)
            requiredWidth += button->sizeHint().width();
        const int fieldWidth = qMax(1, remote->contentsRect().width() - UiAdaptive::scaled(48));
        modes->setDirection(requiredWidth > fieldWidth ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        modes->setSpacing(UiAdaptive::scaled(4));
        for (int i = 0; i < modes->count(); ++i)
            modes->setStretch(i, 1);
    }
}

class Controller : public QObject
{
public:
    explicit Controller(QWidget *content, QScrollArea *scroll)
        : QObject(content), m_content(content)
    {
        content->installEventFilter(this);
        scroll->viewport()->installEventFilter(this);
        scroll->widget()->installEventFilter(this);
        schedule();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show ||
            event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange ||
            event->type() == QEvent::ApplicationFontChange)
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
            apply(m_content);
            m_pending = false;
        });
    }
    QWidget *m_content;
    bool m_pending{false};
};

inline void build(QWidget *content, const Controls &c)
{
    auto *pageLayout = new QVBoxLayout(content);
    pageLayout->setObjectName(QStringLiteral("mainPageLayout"));
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto *toolbar = new QWidget(content);
    toolbar->setObjectName(QStringLiteral("homeToolbar"));
    toolbar->setAttribute(Qt::WA_StyledBackground, true);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    auto *home = new QPushButton(text("Home"), toolbar);
    home->setProperty("homeNavButton", true);
    home->setProperty("active", true);
    c.devices->setProperty("homeNavButton", true);
    c.settings->setText(text("Settings"));
    c.settings->setIcon(QIcon());
    toolbarLayout->addWidget(home);
    toolbarLayout->addWidget(c.devices);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(c.settings);
    pageLayout->addWidget(toolbar);

    auto *scroll = new QScrollArea(content);
    scroll->setObjectName(QStringLiteral("homeScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("homeScrollContent"));
    page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *body = new QVBoxLayout(page);

    auto *heading = new QWidget(page);
    auto *headingLayout = new QVBoxLayout(heading);
    headingLayout->setContentsMargins(0, 0, 0, 0);
    headingLayout->setSpacing(UiAdaptive::scaled(6));
    auto *title = new QLabel(text("Remote connection"), heading);
    title->setObjectName(QStringLiteral("homePageTitle"));
    title->setWordWrap(true);
    auto *subtitle = new QLabel(text("Connect another device or share this device"), heading);
    subtitle->setObjectName(QStringLiteral("homePageSubtitle"));
    subtitle->setWordWrap(true);
    headingLayout->addWidget(title);
    headingLayout->addWidget(subtitle);
    body->addWidget(heading);

    auto *workspace = new QFrame(page);
    workspace->setObjectName(QStringLiteral("homeWorkspace"));
    workspace->setAttribute(Qt::WA_StyledBackground, true);
    workspace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto *grid = new QGridLayout(workspace);
    grid->setObjectName(QStringLiteral("mainPanelGrid"));
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    body->addWidget(workspace);
    body->addStretch(1);

    auto *local = new QFrame(workspace);
    local->setObjectName(QStringLiteral("localDevicePanel"));
    local->setAttribute(Qt::WA_StyledBackground, true);
    local->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *localLayout = new QVBoxLayout(local);
    c.localTitle->setObjectName(QStringLiteral("localPanelTitle"));
    c.localTitle->setWordWrap(true);
    localLayout->addWidget(c.localTitle);

    auto field = [](QWidget *parent, QLabel *label, QWidget *edit, QPushButton *action = nullptr) {
        auto *host = new QWidget(parent);
        host->setObjectName(QStringLiteral("homeFormField"));
        host->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *layout = new QVBoxLayout(host);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *header = new QHBoxLayout;
        header->setContentsMargins(0, 0, 0, 0);
        header->setSpacing(UiAdaptive::scaled(8));
        label->setWordWrap(true);
        header->addWidget(label, 1);
        if (action)
        {
            action->setProperty("homeLocalAction", true);
            header->addWidget(action);
        }
        layout->addLayout(header);
        edit->setMinimumWidth(0);
        edit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        layout->addWidget(edit);
        return host;
    };
    for (QPlainTextEdit *edit : {c.localId, c.localPwd})
    {
        edit->setReadOnly(true);
        edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        edit->setWordWrapMode(QTextOption::WrapAnywhere);
        edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        edit->setTabChangesFocus(true);
        edit->document()->setDocumentMargin(0);
    }
    localLayout->addWidget(field(local, c.localIdLabel, c.localId, c.share));
    localLayout->addWidget(field(local, c.localPwdLabel, c.localPwd, c.update));
    auto *hint = new QLabel(text("Share the local ID and verification code with the other person"), local);
    hint->setObjectName(QStringLiteral("homeLocalHint"));
    hint->setWordWrap(true);
    localLayout->addWidget(hint);
    localLayout->addStretch(1);

    auto *remote = new QFrame(workspace);
    remote->setObjectName(QStringLiteral("remoteDevicePanel"));
    remote->setAttribute(Qt::WA_StyledBackground, true);
    remote->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *remoteLayout = new QVBoxLayout(remote);
    c.remoteTitle->setObjectName(QStringLiteral("remotePanelTitle"));
    c.remoteTitle->setWordWrap(true);
    remoteLayout->addWidget(c.remoteTitle);
    auto *modeHost = new QFrame(remote);
    modeHost->setObjectName(QStringLiteral("remoteModeSelector"));
    auto *modes = new QHBoxLayout(modeHost);
    modes->setObjectName(QStringLiteral("remoteModeLayout"));
    modes->setContentsMargins(UiAdaptive::scaled(4), UiAdaptive::scaled(4),
                              UiAdaptive::scaled(4), UiAdaptive::scaled(4));
    for (QRadioButton *mode : {c.desktop, c.file, c.terminal})
    {
        mode->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        modes->addWidget(mode, 1);
    }
    remoteLayout->addWidget(modeHost);
    remoteLayout->addWidget(field(remote, c.remoteIdLabel, c.remoteId));
    remoteLayout->addWidget(field(remote, c.remotePwdLabel, c.remotePwd));
    c.connect->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    remoteLayout->addWidget(c.connect);
    remoteLayout->addStretch(1);

    scroll->setWidget(page);
    pageLayout->addWidget(scroll, 1);
    auto *footer = new QWidget(content);
    footer->setObjectName(QStringLiteral("homeFooter"));
    auto *footerLayout = new QHBoxLayout(footer);
    c.status->setObjectName(QStringLiteral("ws_connect_status"));
    c.status->setWordWrap(true);
    c.version->setObjectName(QStringLiteral("mainVersionLabel"));
    footerLayout->addWidget(c.status, 1);
    footerLayout->addWidget(c.version);
    pageLayout->addWidget(footer);

    new Controller(content, scroll);
    apply(content);
}
} // namespace HomePageLayout

#endif
