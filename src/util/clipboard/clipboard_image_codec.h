#pragma once

#include <QByteArray>

class QImage;
class QMimeData;
class QSize;

namespace ClipboardImageCodec
{
bool isImageSizeAllowed(const QSize &size);
QImage imageFromEncodedData(const QByteArray &encodedImage);
QByteArray pngDataFromMimeData(const QMimeData *mimeData);
}
