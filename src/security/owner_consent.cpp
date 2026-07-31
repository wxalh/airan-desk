#include "owner_consent.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

#include <cstdio>
#include <iostream>
#include <string>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace
{
constexpr int kConsentCountdownSeconds = 10;

bool stdinIsInteractive()
{
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

bool persistMarker()
{
    const QString path = OwnerConsent::markerPath();
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    if (file.write("owner-agreement-v1\n") < 0 || !file.commit())
        return false;
    return true;
}

bool showGuiConsent(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("ownerConsentDialog"));
    dialog.setWindowTitle(QCoreApplication::translate(
        "OwnerConsentDialog", QT_TRANSLATE_NOOP("OwnerConsentDialog", "Device owner authorization")));
    dialog.setWindowIcon(qApp->windowIcon());
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    dialog.setModal(true);
    QFont dialogFont = QApplication::font();
    dialogFont.setPointSizeF(qMax(13.0, dialogFont.pointSizeF()));
    dialog.setFont(dialogFont);
    dialog.setMinimumSize(680, 540);
    dialog.resize(700, 560);
    dialog.setStyleSheet(QStringLiteral(
        "QDialog#ownerConsentDialog { background: #181818; color: #e5eaf3; }"
        "#ownerConsentHeader { background: transparent; border-bottom: 1px solid #2a2a2a; }"
        "#ownerConsentIcon { background: #202d34; border: 1px solid #426477; border-radius: 6px; }"
        "#ownerConsentTitle { color: rgb(131,193,224); font-size: 18px; font-weight: 600; }"
        "#ownerConsentSubtitle { color: #a6abb3; font-size: 10pt; }"
        "#ownerConsentIntro { color: #d6dae1; font-size: 12pt; }"
        "QCheckBox#ownerConsentConfirmation { background: #202020; border: 1px solid #3a3a3a;"
        "  border-radius: 6px; color: #e5eaf3; padding: 14px 16px; spacing: 12px; font-size: 12pt; }"
        "QCheckBox#ownerConsentConfirmation:hover { border-color: #59616a; background: #242424; }"
        "QCheckBox#ownerConsentConfirmation:checked { border-color: rgb(131,193,224); background: #202d34; }"
        "QCheckBox#ownerConsentConfirmation::indicator { width: 18px; height: 18px; }"
        "#ownerConsentCountdown { color: #90969f; font-size: 10pt; }"
        "#ownerConsentPrivacy { color: #747b84; font-size: 10pt; }"
        "QPushButton { min-height: 44px; border-radius: 6px; padding: 0 20px; font-size: 12pt; }"
        "QPushButton#ownerConsentSecondaryButton { background: transparent; border: 1px solid #565b62; color: #d6dae1; }"
        "QPushButton#ownerConsentSecondaryButton:hover { background: #262626; border-color: #737981; }"
        "QPushButton#ownerConsentPrimaryButton { background: rgb(92,157,190); border: 1px solid rgb(131,193,224);"
        "  color: #101518; font-weight: 600; min-width: 170px; }"
        "QPushButton#ownerConsentPrimaryButton:hover { background: rgb(131,193,224); }"
        "QPushButton#ownerConsentPrimaryButton:pressed { background: rgb(76,139,171); }"
        "QPushButton#ownerConsentPrimaryButton:disabled { background: #303234; border-color: #45484c; color: #72767b; }"));

    const auto translated = [](const char *source) {
        return QCoreApplication::translate("OwnerConsentDialog", source);
    };

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(16);

    auto *header = new QFrame(&dialog);
    header->setObjectName(QStringLiteral("ownerConsentHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 18);
    headerLayout->setSpacing(14);

    auto *icon = new QLabel(header);
    icon->setObjectName(QStringLiteral("ownerConsentIcon"));
    icon->setFixedSize(44, 44);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(qApp->windowIcon().pixmap(QSize(28, 28)));
    headerLayout->addWidget(icon, 0, Qt::AlignTop);

    auto *headingLayout = new QVBoxLayout();
    headingLayout->setContentsMargins(0, 0, 0, 0);
    headingLayout->setSpacing(4);
    auto *title = new QLabel(
        translated(QT_TRANSLATE_NOOP("OwnerConsentDialog", "Device owner authorization")), header);
    title->setObjectName(QStringLiteral("ownerConsentTitle"));
    auto *subtitle = new QLabel(
        translated(QT_TRANSLATE_NOOP("OwnerConsentDialog", "Review before enabling remote access")), header);
    subtitle->setObjectName(QStringLiteral("ownerConsentSubtitle"));
    headingLayout->addWidget(title);
    headingLayout->addWidget(subtitle);
    headerLayout->addLayout(headingLayout, 1);
    layout->addWidget(header);

    auto *intro = new QLabel(
        translated(QT_TRANSLATE_NOOP(
            "OwnerConsentDialog",
            "Remote access can expose this device and its data. Confirm each statement to continue.")),
        &dialog);
    intro->setObjectName(QStringLiteral("ownerConsentIntro"));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *owner = new QCheckBox(
        translated(QT_TRANSLATE_NOOP(
            "OwnerConsentDialog", "I am the owner of this device or an authorized administrator.")),
        &dialog);
    auto *audit = new QCheckBox(
        translated(QT_TRANSLATE_NOOP(
            "OwnerConsentDialog", "I understand that remote operations are recorded in local audit logs.")),
        &dialog);
    auto *legal = new QCheckBox(
        translated(QT_TRANSLATE_NOOP(
            "OwnerConsentDialog", "I understand that remote access without authorization may violate the law.")),
        &dialog);
    for (QCheckBox *box : {owner, audit, legal})
    {
        box->setObjectName(QStringLiteral("ownerConsentConfirmation"));
        box->setMinimumHeight(48);
        box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        layout->addWidget(box);
    }

    auto *countdown = new QLabel(&dialog);
    countdown->setObjectName(QStringLiteral("ownerConsentCountdown"));
    auto *accept = new QPushButton(&dialog);
    accept->setObjectName(QStringLiteral("ownerConsentPrimaryButton"));
    accept->setAccessibleName(
        translated(QT_TRANSLATE_NOOP("OwnerConsentDialog", "Agree and continue")));
    auto *decline = new QPushButton(
        translated(QT_TRANSLATE_NOOP("OwnerConsentDialog", "Exit")), &dialog);
    decline->setObjectName(QStringLiteral("ownerConsentSecondaryButton"));
    decline->setAccessibleName(translated(QT_TRANSLATE_NOOP("OwnerConsentDialog", "Exit")));
    accept->setFont(dialogFont);
    decline->setFont(dialogFont);
    accept->setEnabled(false);
    layout->addWidget(countdown);

    auto *privacy = new QLabel(
        translated(QT_TRANSLATE_NOOP(
            "OwnerConsentDialog", "This confirmation is stored only on this device.")),
        &dialog);
    privacy->setObjectName(QStringLiteral("ownerConsentPrivacy"));
    layout->addWidget(privacy);
    layout->addStretch(1);

    auto *actions = new QHBoxLayout();
    actions->setSpacing(10);
    actions->addStretch(1);
    actions->addWidget(decline);
    actions->addWidget(accept);
    layout->addLayout(actions);

    int remaining = kConsentCountdownSeconds;
    const auto updateState = [&]() {
        countdown->setText(remaining > 0
                               ? translated(QT_TRANSLATE_NOOP(
                                                "OwnerConsentDialog", "Continue available in %1 seconds"))
                                     .arg(remaining)
                               : translated(QT_TRANSLATE_NOOP(
                                     "OwnerConsentDialog",
                                     "Review period complete. Confirm all statements to continue.")));
        accept->setText(remaining > 0
                             ? translated(QT_TRANSLATE_NOOP(
                                              "OwnerConsentDialog", "Agree and continue (%1)"))
                                   .arg(remaining)
                             : translated(QT_TRANSLATE_NOOP(
                                   "OwnerConsentDialog", "Agree and continue")));
        accept->setEnabled(remaining == 0 && owner->isChecked() && audit->isChecked() && legal->isChecked());
    };
    updateState();

    QTimer timer(&dialog);
    timer.setInterval(1000);
    QObject::connect(&timer, &QTimer::timeout, &dialog, [&]() {
        if (remaining > 0)
            --remaining;
        updateState();
        if (remaining == 0)
            timer.stop();
    });
    for (QCheckBox *box : {owner, audit, legal})
        QObject::connect(box, &QCheckBox::toggled, &dialog, updateState);
    QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(decline, &QPushButton::clicked, &dialog, &QDialog::reject);
    timer.start();
    return dialog.exec() == QDialog::Accepted;
}

bool showTerminalConsent()
{
    if (!stdinIsInteractive())
        return false;
    std::cout
        << "Airan-Desk owner authorization\n"
        << "[ ] I am the owner of this device or an authorized administrator.\n"
        << "[ ] I understand that all operations will be recorded in audit logs.\n"
        << "[ ] I understand that unauthorized remote control may be illegal.\n"
        << "Type I AGREE exactly to continue: " << std::flush;
    std::string response;
    return std::getline(std::cin, response) && response == "I AGREE";
}
}

QString OwnerConsent::markerPath()
{
    return QDir(QDir::homePath()).filePath(QStringLiteral(".wxalh/airan-desk/state/owner-agreement-v1"));
}

bool OwnerConsent::isAccepted()
{
    QFile file(markerPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    return file.readAll().trimmed() == QByteArrayLiteral("owner-agreement-v1");
}

bool OwnerConsent::ensureAccepted(bool uiAvailable, QWidget *parent)
{
    if (isAccepted())
        return true;
    const bool accepted = uiAvailable ? showGuiConsent(parent) : showTerminalConsent();
    return accepted && persistMarker();
}
