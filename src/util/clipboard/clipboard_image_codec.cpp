#include "util/clipboard/clipboard_image_codec.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QPixmap>
#include <QRegularExpression>
#include <QString>
#include <QVariant>

namespace
{
constexpr qint64 kMaxClipboardImagePixels = 40LL * 1024 * 1024;
constexpr int kMaxClipboardImageDimension = 16384;
constexpr qint64 kMaxClipboardEncodedImageBytes = 32LL * 1024 * 1024;

class LimitedByteArrayDevice final : public QIODevice
{
public:
    explicit LimitedByteArrayDevice(qint64 limit)
        : m_limit(limit)
    {
    }

    const QByteArray &data() const
    {
        return m_data;
    }

protected:
    qint64 readData(char *, qint64) override
    {
        return -1;
    }

    qint64 writeData(const char *data, qint64 length) override
    {
        if (length < 0 || length > m_limit - m_data.size())
            return -1;
        m_data.append(data, static_cast<int>(length));
        return length;
    }

private:
    QByteArray m_data;
    qint64 m_limit = 0;
};

bool isSupportedEncodedImageMime(const QString &format)
{
    if (format.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive))
        return true;

    static const QRegularExpression windowsMime(
        QStringLiteral("^application/x-qt-windows-mime;value=\\\"?([^\\\";]+)\\\"?(?:;.*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = windowsMime.match(format);
    if (!match.hasMatch())
        return false;

    const QString value = match.captured(1).trimmed().toLower();
    return value == QStringLiteral("png") ||
           value == QStringLiteral("jfif") ||
           value == QStringLiteral("jpeg") ||
           value == QStringLiteral("jpg") ||
           value == QStringLiteral("webp") ||
           value == QStringLiteral("tiff") ||
           value == QStringLiteral("tif") ||
           value == QStringLiteral("bmp") ||
           value == QStringLiteral("dib") ||
           value == QStringLiteral("dibv5") ||
           value == QStringLiteral("cf_dib") ||
           value == QStringLiteral("cf_dibv5");
}

QImage qtImageFromMimeData(const QMimeData *mimeData)
{
    if (!mimeData || !mimeData->hasImage())
        return QImage();

    const QVariant imageData = mimeData->imageData();
    QImage image = qvariant_cast<QImage>(imageData);
    if (!image.isNull())
        return image;

    const QPixmap pixmap = qvariant_cast<QPixmap>(imageData);
    return pixmap.isNull() ? QImage() : pixmap.toImage();
}

QImage rawImageFromMimeData(const QMimeData *mimeData)
{
    if (!mimeData)
        return QImage();

    const QStringList formats = mimeData->formats();
    for (const QString &format : formats)
    {
        if (!isSupportedEncodedImageMime(format))
            continue;

        const QByteArray encodedImage = mimeData->data(format);
        if (encodedImage.isEmpty() || encodedImage.size() > kMaxClipboardEncodedImageBytes)
            continue;

        const QImage image = ClipboardImageCodec::imageFromEncodedData(encodedImage);
        if (!image.isNull())
            return image;
    }
    return QImage();
}
} // namespace

bool ClipboardImageCodec::isImageSizeAllowed(const QSize &size)
{
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0 ||
        size.width() > kMaxClipboardImageDimension ||
        size.height() > kMaxClipboardImageDimension)
    {
        return false;
    }
    return static_cast<qint64>(size.width()) * size.height() <= kMaxClipboardImagePixels;
}

QImage ClipboardImageCodec::imageFromEncodedData(const QByteArray &encodedImage)
{
    if (encodedImage.isEmpty() || encodedImage.size() > kMaxClipboardEncodedImageBytes)
        return QImage();

    QBuffer buffer;
    buffer.setData(encodedImage);
    if (!buffer.open(QIODevice::ReadOnly))
        return QImage();

    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    if (!isImageSizeAllowed(reader.size()))
        return QImage();

    const QImage image = reader.read();
    return isImageSizeAllowed(image.size()) ? image : QImage();
}

QByteArray ClipboardImageCodec::pngDataFromMimeData(const QMimeData *mimeData)
{
    QImage image = qtImageFromMimeData(mimeData);
    if (image.isNull())
        image = rawImageFromMimeData(mimeData);
    if (image.isNull() || !isImageSizeAllowed(image.size()))
        return QByteArray();

    LimitedByteArrayDevice output(kMaxClipboardEncodedImageBytes);
    if (!output.open(QIODevice::WriteOnly) || !image.save(&output, "PNG"))
        return QByteArray();
    return output.data();
}
