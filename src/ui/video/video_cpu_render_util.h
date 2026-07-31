#ifndef AIRAN_VIDEO_CPU_RENDER_UTIL_H
#define AIRAN_VIDEO_CPU_RENDER_UTIL_H

#include <QPixmap>
#include <QSize>

namespace VideoCpuRenderUtil
{
QPixmap scalePixmap(const QPixmap &source, const QSize &targetSize);
}

#endif // AIRAN_VIDEO_CPU_RENDER_UTIL_H
