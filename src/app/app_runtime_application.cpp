#include "app/app_runtime_internal.h"

#include "app/app_headless_controller.h"
#include "app/app_style.h"
#include "common/logger_manager.h"
#include "rtc/core/rtc.hpp"
#include "security/owner_consent.h"
#include "security/runtime_environment.h"
#include "security/audit_logger.h"
#include "security/controlled_access_gate.h"
#include "ui/main/main_window.h"
#include "util/config/config_util.h"
#include "util/input/input_util.h"
#include "util/text/i18n_util.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QThread>
#include <QTranslator>
#include <QStringList>

#include <memory>
#include <thread>

namespace
{
    int requestedStartupDelaySeconds(const QStringList &arguments)
    {
        for (int i = 1; i + 1 < arguments.size(); ++i)
        {
            if (arguments.at(i) != QStringLiteral("--delaystart"))
                continue;

            bool ok = false;
            const int seconds = arguments.at(i + 1).toInt(&ok);
            return ok ? qBound(0, seconds, 300) : 0;
        }
        return 0;
    }

    
    void applyApplicationIdentity()
    {
        QCoreApplication::setOrganizationName("wxalh.com");
        QCoreApplication::setApplicationName("airan");
        QCoreApplication::setApplicationVersion(QStringLiteral(AIRAN_DESK_VERSION));
    }

    
    void installTranslators(QCoreApplication &app, QTranslator &qtTranslator, QTranslator &appTranslator)
    {
        ConfigUtil->language = I18nUtil::normalizeUiLanguage(ConfigUtil->language);
        const QString uiLocaleName = I18nUtil::resolveUiLanguage(ConfigUtil->language);
        const QString qtLocaleName = I18nUtil::resolveQtLanguage(ConfigUtil->language);
        I18nUtil::installTranslator(app, qtTranslator, QStringLiteral("qtbase_"), qtLocaleName);
        I18nUtil::installTranslator(app, appTranslator, QStringLiteral("airan-desk_"), uiLocaleName);
    }

    std::thread startWindowsUnattendedServiceReadiness()
    {
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
        if (ConfigUtil->allow_remote &&
            (!ConfigUtil->identity_storage_ready ||
             !RuntimeEnvironment::uiAvailable() ||
             !AuditLogger::instance().isReady()))
            return {};

        InputUtil::resetWindowsUnattendedInputReadinessCancellation();
        const bool allowRemote = ConfigUtil->allow_remote;
        return std::thread([allowRemote]() {
            QString errorMessage;
            const bool ready = !allowRemote
                                   ? (!InputUtil::isWindowsUnattendedInputInstalled() ||
                                      InputUtil::uninstallWindowsUnattendedInput(&errorMessage))
                                   : (InputUtil::isWindowsUnattendedInputInstalled()
                                          ? InputUtil::ensureWindowsUnattendedInputServiceReady(&errorMessage)
                                          : InputUtil::installWindowsUnattendedInput(&errorMessage));
            if (ready)
            {
                ControlledAccessGate::setRuntimePrerequisiteReady(true);
                if (!allowRemote)
                    return;
                QString brokerError;
                if (!InputUtil::prepareWindowsInputBroker(&brokerError))
                    LOG_WARN("Windows input broker preparation failed: {}", brokerError);
            }
            else
            {
                ControlledAccessGate::setRuntimePrerequisiteReady(false);
                LOG_WARN("Windows unattended service reconciliation failed: {}",
                         errorMessage.isEmpty() ? QStringLiteral("unknown error") : errorMessage);
            }
        });
#else
        return {};
#endif
    }

    void waitForWindowsUnattendedServiceReadiness(std::thread &thread)
    {
        InputUtil::cancelWindowsUnattendedInputReadiness();
        if (thread.joinable())
            thread.join();
    }

    
    int runHeadlessApplication(int argc, char *argv[], bool serviceMode)
    {
        applyApplicationIdentity();
        QCoreApplication app(argc, argv);

        if (AppRuntime::isRunning(serviceMode))
            return 0;

        if (!serviceMode && !OwnerConsent::ensureAccepted(false))
            return 1;
        QTranslator qtTranslator;
        QTranslator appTranslator;
        installTranslators(app, qtTranslator, appTranslator);

        AppRuntime::initLog();
        QString auditError;
        if (!AuditLogger::instance().initialize(&auditError))
            LOG_ERROR("Controlled access disabled because strict audit initialization failed: {}", auditError);
        ControlledAccessGate::setRuntimePrerequisiteReady(
            !ConfigUtil->allow_remote ||
            (AuditLogger::instance().isReady() && ConfigUtil->identity_storage_ready));
        ConfigUtil->applyAutoStartSetting();
        std::thread unattendedServiceReadinessThread = startWindowsUnattendedServiceReadiness();
        if (!ConfigUtil->allow_remote || !AuditLogger::instance().isReady() ||
            !ConfigUtil->identity_storage_ready)
        {
            if (unattendedServiceReadinessThread.joinable())
                unattendedServiceReadinessThread.join();
            rtc::Cleanup();
            return ConfigUtil->allow_remote ? 2 : 0;
        }
        std::unique_ptr<HeadlessController> headlessController = std::make_unique<HeadlessController>();

        const int result = app.exec();
        LOG_DEBUG("Application is exiting...");
        waitForWindowsUnattendedServiceReadiness(unattendedServiceReadinessThread);
        headlessController.reset();
        InputUtil::shutdownWindowsInputBroker();
        if (!serviceMode)
            InputUtil::stopWindowsUnattendedInputService();
        rtc::Cleanup();
        return result;
    }

    
    int runGuiApplication(int argc, char *argv[], bool forceNoUi, bool serviceMode)
    {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 1, 2)) && (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
        QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
#endif

#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::Round);
#endif

        applyApplicationIdentity();
        QGuiApplication::setDesktopFileName(QStringLiteral("airan-desk"));
        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
        app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
        applyGlobalStyle(app);

        if (AppRuntime::isRunning(serviceMode))
            return 0;

        QTranslator qtTranslator;
        QTranslator appTranslator;
        installTranslators(app, qtTranslator, appTranslator);

        Q_UNUSED(forceNoUi);
        if (!serviceMode && !OwnerConsent::ensureAccepted(true))
            return 1;
        const QStringList arguments = app.arguments();
        const bool startInTray = arguments.contains(QStringLiteral("--start-in-tray"));
        const int startupDelaySeconds = requestedStartupDelaySeconds(arguments);
        if (startupDelaySeconds > 0)
            QThread::sleep(static_cast<unsigned long>(startupDelaySeconds));

        AppRuntime::initLog();
        QString auditError;
        if (!AuditLogger::instance().initialize(&auditError))
            LOG_ERROR("Controlled access disabled because strict audit initialization failed: {}", auditError);
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
        ControlledAccessGate::setRuntimePrerequisiteReady(!ConfigUtil->allow_remote);
#else
        ControlledAccessGate::setRuntimePrerequisiteReady(
            !ConfigUtil->allow_remote ||
            (AuditLogger::instance().isReady() && ConfigUtil->identity_storage_ready));
#endif
        ConfigUtil->applyAutoStartSetting();
        std::unique_ptr<MainWindow> mainWindow;
        std::unique_ptr<HeadlessController> headlessController;
        std::thread unattendedServiceReadinessThread;
        mainWindow = std::make_unique<MainWindow>();
        if (!startInTray)
            mainWindow->show();
        unattendedServiceReadinessThread = startWindowsUnattendedServiceReadiness();

        const int result = app.exec();
        LOG_DEBUG("Application is exiting...");
        waitForWindowsUnattendedServiceReadiness(unattendedServiceReadinessThread);
        mainWindow.reset();
        headlessController.reset();
        InputUtil::shutdownWindowsInputBroker();
        if (!serviceMode)
            InputUtil::stopWindowsUnattendedInputService();
        rtc::Cleanup();
        return result;
    }
}

int AppRuntime::runApplication(int argc, char *argv[], bool forceNoUi, bool serviceMode)
{
    registerCustomTypes();
    const bool hasUi = !serviceMode && RuntimeEnvironment::detectInteractiveUi();
    RuntimeEnvironment::setDetectedUiAvailability(hasUi);
    if (!hasUi)
        return runHeadlessApplication(argc, argv, serviceMode);

    return runGuiApplication(argc, argv, forceNoUi, serviceMode);
}
