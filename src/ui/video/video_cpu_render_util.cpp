#include "video_cpu_render_util.h"

#include <QImage>

#include <libyuv/scale_argb.h>

namespace VideoCpuRenderUtil
{

QPixmap scalePixmap(const QPixmap &source, const QSize &targetSize)
{
    if (source.isNull() || targetSize.isEmpty())
        return QPixmap();

    QImage src = source.toImage();
    if (src.isNull())
        return QPixmap();
    if (src.format() != QImage::Format_ARGB32)
        src = src.convertToFormat(QImage::Format_ARGB32);

    QImage dst(targetSize, QImage::Format_ARGB32);
    if (dst.isNull())
        return QPixmap();

    const int result = libyuv::ARGBScale(src.constBits(),
                                         src.bytesPerLine(),
                                         src.width(),
                                         src.height(),
                                         dst.bits(),
                                         dst.bytesPerLine(),
                                         dst.width(),
                                         dst.height(),
                                         libyuv::kFilterBox);
    if (result != 0)
        return QPixmap();

    return QPixmap::fromImage(dst, Qt::ColorOnly);
}

} // namespace VideoCpuRenderUtil
