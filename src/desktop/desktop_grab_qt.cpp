#include "desktop_grab_qt.h"
#include "../common/constant.h"
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>

DesktopGrabQt::DesktopGrabQt(QObject *parent)
    : DesktopGrab(parent)
{
}

DesktopGrabQt::~DesktopGrabQt()
{
}

bool DesktopGrabQt::init(int screenIndex)
{
    QMutexLocker locker(&m_mutex);
    if (screenIndex < 0)
    {
        LOG_WARN("Invalid screen index, using primary screen (0) instead");
        screenIndex = 0;
    }
    m_screenIndex = screenIndex;

    QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.size() == 0)
    {
        LOG_ERROR("No screens found");
        return false;
    }
    m_screen = screens[m_screenIndex];
    QRect screenGeometry = m_screen ? m_screen->geometry() : QRect(0, 0, 1920, 1080);
    m_screen_width = screenGeometry.width();
    m_screen_height = screenGeometry.height();

    return true;
}

bool DesktopGrabQt::grabFrameCPU(QImage &outImage)
{
    // capture and emit raw frame
    QMutexLocker locker(&m_mutex);
    if (!m_screen)
    {
        LOG_ERROR("Screen not initialized");
        return false;
    }
    QPixmap pixmap = m_screen->grabWindow(0);
    outImage = pixmap.toImage();
    return true;
}

