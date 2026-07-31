#include "webrtc/ctl/webrtc_ctl.h"

#include "util/text/convert_util.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <libyuv/convert_argb.h>
#include <cstring>
#include <limits>

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
#include <d3d11.h>
#include <wrl/client.h>
#endif

namespace
{
struct VideoDumpFiles
{
    QFile rawFile;
    QFile sidecarFile;
    qint64 frameIndex = 0;
    bool initialized = false;
    bool enabled = false;
};

bool ensureVideoDump(VideoDumpFiles &dump, const QString &prefix)
{
    if (!dump.initialized)
    {
        dump.initialized = true;
        const QDir appDir(QCoreApplication::applicationDirPath());
        if (!QFile::exists(appDir.filePath(QStringLiteral("conf/airan_dump_video.flag"))))
            return false;

        const QString dumpDir = appDir.filePath(QStringLiteral("out"));
        QDir().mkpath(dumpDir);
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
        dump.rawFile.setFileName(QDir(dumpDir).filePath(prefix + stamp + QStringLiteral(".bgra")));
        dump.sidecarFile.setFileName(QDir(dumpDir).filePath(prefix + stamp + QStringLiteral(".txt")));
        dump.enabled = dump.rawFile.open(QIODevice::WriteOnly) && dump.sidecarFile.open(QIODevice::WriteOnly | QIODevice::Text);
        if (dump.enabled)
        {
            QTextStream stream(&dump.sidecarFile);
            stream << "# index,width,height,format,wallclock_ms\n";
            stream.flush();
        }
    }
    return dump.enabled;
}

bool dumpBgraFrame(VideoDumpFiles &dump, const uchar *pixels, int width, int height, const QString &prefix)
{
    if (!pixels || width <= 0 || height <= 0)
        return false;
    if (!ensureVideoDump(dump, prefix) || dump.frameIndex >= 120)
        return false;

    const qint64 bytes = static_cast<qint64>(width) * height * 4;
    if (dump.rawFile.write(reinterpret_cast<const char *>(pixels), bytes) != bytes)
        return false;
    QTextStream stream(&dump.sidecarFile);
    stream << dump.frameIndex << "," << width << "," << height << ",bgra,"
           << QDateTime::currentMSecsSinceEpoch() << "\n";
    if ((dump.frameIndex & 15) == 0)
    {
        dump.rawFile.flush();
        stream.flush();
    }
    ++dump.frameIndex;
    return true;
}

bool dumpWindowsDecodedBgra(const uchar *pixels, int width, int height)
{
    static VideoDumpFiles dump;
    return dumpBgraFrame(dump, pixels, width, height, QStringLiteral("airan_windows_decoded_"));
}

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
bool dumpD3D11DecodedBgra(const rtc::D3D11VideoFrame &frame)
{
    if (!frame.isValid() || frame.width <= 0 || frame.height <= 0)
        return false;

    static VideoDumpFiles dump;
    if (!ensureVideoDump(dump, QStringLiteral("airan_windows_d3d11_decoded_")) || dump.frameIndex >= 120)
        return false;

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    frame.device->GetImmediateContext(&context);
    if (!context)
        return false;

    D3D11_TEXTURE2D_DESC srcDesc = {};
    frame.texture->GetDesc(&srcDesc);
    D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
    stagingDesc.Width = static_cast<UINT>(frame.width);
    stagingDesc.Height = static_cast<UINT>(frame.height);
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.SampleDesc.Quality = 0;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(frame.device->CreateTexture2D(&stagingDesc, nullptr, &staging)) || !staging)
        return false;

    context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, frame.texture.Get(), frame.subresource, nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    const qint64 frameBytes = static_cast<qint64>(frame.width) * frame.height * 4;
    if (frame.width <= 0 || frame.height <= 0 || frameBytes <= 0 || frameBytes > std::numeric_limits<int>::max())
    {
        context->Unmap(staging.Get(), 0);
        return false;
    }

    QByteArray bgra;
    bgra.resize(static_cast<int>(frameBytes));
    bool ok = false;
    const auto *src = static_cast<const uint8_t *>(mapped.pData);
    if (frame.format == DXGI_FORMAT_B8G8R8A8_UNORM || frame.format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
    {
        for (int y = 0; y < frame.height; ++y)
        {
            std::memcpy(bgra.data() + static_cast<size_t>(y) * frame.width * 4,
                        src + static_cast<size_t>(y) * mapped.RowPitch,
                        static_cast<size_t>(frame.width) * 4);
        }
        ok = true;
    }
    else if (frame.format == DXGI_FORMAT_NV12)
    {
        const uint8_t *yPlane = src;
        const uint8_t *uvPlane = src + static_cast<size_t>(mapped.RowPitch) * frame.height;
        ok = libyuv::NV12ToARGBMatrix(yPlane,
                                      static_cast<int>(mapped.RowPitch),
                                      uvPlane,
                                      static_cast<int>(mapped.RowPitch),
                                      reinterpret_cast<uint8_t *>(bgra.data()),
                                      frame.width * 4,
                                      &libyuv::kYuvH709Constants,
                                      frame.width,
                                      frame.height) == 0;
    }
    context->Unmap(staging.Get(), 0);

    if (!ok)
        return false;
    return dumpBgraFrame(dump,
                         reinterpret_cast<const uchar *>(bgra.constData()),
                         frame.width,
                         frame.height,
                         QStringLiteral("airan_windows_d3d11_decoded_"));
}
#endif
} // namespace

void WebRtcCtl::processAudioFrame(const rtc::binary &audioData, const rtc::FrameInfo &frameInfo)
{
    Q_UNUSED(audioData);
    Q_UNUSED(frameInfo);
    LOG_TRACE("Ignored legacy audio playback path; remote audio is rendered by the libwebrtc audio device");
}

void WebRtcCtl::processVideoFrame(const rtc::binary &data, const rtc::FrameInfo &frameInfo)
{
    Q_UNUSED(frameInfo);
    LOG_TRACE("Received libwebrtc decoded video frame: {}", ConvertUtil::formatFileSize(data.size()));

    if (data.size() < sizeof(int) * 2)
    {
        LOG_WARN("Dropped unsupported video payload: expected libwebrtc decoded BGRA frame, got {} bytes", data.size());
        return;
    }

    try
    {
        int width = 0;
        int height = 0;
        std::memcpy(&width, data.data(), sizeof(width));
        std::memcpy(&height, data.data() + sizeof(width), sizeof(height));

        const size_t pixelBytes = data.size() - sizeof(width) - sizeof(height);
        if (width <= 0 || height <= 0 || pixelBytes != static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
        {
            LOG_WARN("Dropped malformed libwebrtc video payload: {}x{}, bytes={}", width, height, data.size());
            return;
        }

        const uchar *pixels = reinterpret_cast<const uchar *>(data.data() + sizeof(width) + sizeof(height));
        dumpWindowsDecodedBgra(pixels, width, height);
        QImage decodedFrame(pixels, width, height, width * 4, QImage::Format_ARGB32);
        decodedFrame = decodedFrame.copy();

        if (m_remoteVisibleSize.isValid() &&
            m_remoteVisibleSize.width() > 0 && m_remoteVisibleSize.height() > 0)
        {
            const QRect visibleRect(QPoint(qBound(0, m_remotePadding.left(), qMax(0, decodedFrame.width() - 1)),
                                           qBound(0, m_remotePadding.top(), qMax(0, decodedFrame.height() - 1))),
                                    QSize(qMin(m_remoteVisibleSize.width(), decodedFrame.width()),
                                          qMin(m_remoteVisibleSize.height(), decodedFrame.height())));
            const QRect boundedRect = visibleRect.intersected(decodedFrame.rect());
            if (boundedRect.isValid() && boundedRect.size() != decodedFrame.size())
                decodedFrame = decodedFrame.copy(boundedRect);
        }

        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        m_lastVideoDecodedMs = nowMs;
        ++m_videoFeedbackDecodedFrames;

        if (m_videoStatsStartMs == 0)
            m_videoStatsStartMs = nowMs;
        m_videoStatsBytes += static_cast<qint64>(pixelBytes);

        emit videoFrameDecoded(decodedFrame);

        const qint64 elapsedMs = nowMs - m_videoStatsStartMs;
        if (elapsedMs >= 1000)
        {
            const double kbps = (m_videoStatsBytes * 8.0) / elapsedMs;
            emit videoStatsUpdated(kbps, decodedFrame.size());
            m_videoStatsStartMs = nowMs;
            m_videoStatsBytes = 0;
        }

        LOG_TRACE("Rendered libwebrtc-decoded video frame: {}x{}", decodedFrame.width(), decodedFrame.height());
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error processing video frame: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Unknown error processing video frame");
    }
}

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
void WebRtcCtl::processD3D11VideoFrame(rtc::D3D11VideoFrame frame)
{
    if (!frame.isValid())
    {
        LOG_WARN("Dropped invalid D3D11 video frame: {}x{}, format={}",
                 frame.width, frame.height, static_cast<int>(frame.format));
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_lastVideoDecodedMs = nowMs;
    ++m_videoFeedbackDecodedFrames;
    dumpD3D11DecodedBgra(frame);

    if (m_videoStatsStartMs == 0)
        m_videoStatsStartMs = nowMs;
    const int bytesPerPixel = (frame.format == DXGI_FORMAT_NV12 || frame.format == DXGI_FORMAT_P010) ? 2 : 4;
    const qint64 pixelBytes = static_cast<qint64>(frame.width) * frame.height * bytesPerPixel;
    m_videoStatsBytes += pixelBytes;

    const QSize frameSize(frame.width, frame.height);
    emit videoFrameD3D11Decoded(frame);

    const qint64 elapsedMs = nowMs - m_videoStatsStartMs;
    if (elapsedMs >= 1000)
    {
        const double kbps = (m_videoStatsBytes * 8.0) / elapsedMs;
        emit videoStatsUpdated(kbps, frameSize);
        m_videoStatsStartMs = nowMs;
        m_videoStatsBytes = 0;
    }

    LOG_TRACE("Rendered D3D11 video frame: {}x{}, format={}", frame.width, frame.height, static_cast<int>(frame.format));
}
#endif
