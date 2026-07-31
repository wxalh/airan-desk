#include "webrtc/cli/webrtc_cli.h"

#include "media/capture/core/airan_capture_frame.h"
#include "webrtc/cli/media/webrtc_cli_media_labels.h"

#include <QMetaObject>

namespace
{

QString qstr(const char *value)
{
    return QString::fromLatin1(value ? value : "Unknown");
}

QString captureBackendLabel(const airan::media::CaptureFrameDescriptor &frame)
{
    if (!frame.capture_backend.empty())
        return QString::fromStdString(frame.capture_backend).toLower();

    switch (frame.native_handle.type)
    {
    case airan::media::NativeHandleType::D3D11Texture2D:
        return QStringLiteral("wgc");
    case airan::media::NativeHandleType::DmaBuf:
        return QStringLiteral("pipewire");
    case airan::media::NativeHandleType::CVPixelBuffer:
    case airan::media::NativeHandleType::IOSurface:
        return QStringLiteral("sck");
    case airan::media::NativeHandleType::kNone:
        break;
    }

    const QString fallback = webrtc_cli_internal::desktopCaptureMethodLabel().toLower();
    if (fallback.contains(QStringLiteral("dxgi")))
        return QStringLiteral("dxgi");
    if (fallback.contains(QStringLiteral("gdi")))
        return QStringLiteral("gdi");
    if (fallback.contains(QStringLiteral("pipewire")) || fallback.contains(QStringLiteral("wayland")))
        return QStringLiteral("pipewire");
    if (fallback.contains(QStringLiteral("x11")) || fallback.contains(QStringLiteral("xorg")))
        return QStringLiteral("xorg");
    return fallback.isEmpty() || fallback == QStringLiteral("auto")
               ? QStringLiteral("unknown")
               : fallback;
}

QString captureMethodLabel(const QString &backend, airan::media::CapturePath path)
{
    const QString normalizedBackend = backend.trimmed().isEmpty() ? QStringLiteral("unknown") : backend.trimmed().toLower();
    switch (path)
    {
    case airan::media::CapturePath::NativeGpuCapture:
        return normalizedBackend;
    case airan::media::CapturePath::WebRtcDerivedCpuCapture:
        return normalizedBackend == QStringLiteral("unknown")
                   ? QStringLiteral("cpu")
                   : normalizedBackend;
    case airan::media::CapturePath::CaptureReprobe:
        return QStringLiteral("%1/reprobe").arg(normalizedBackend);
    }
    return normalizedBackend;
}

bool isHardwareEncodePath(const QString &path)
{
    return path == QStringLiteral("GpuZeroCopyEncode") ||
           path == QStringLiteral("GpuCopyHwEncode") ||
           path == QStringLiteral("CpuUploadHwEncode") ||
           path == QStringLiteral("CpuReadbackHwEncode");
}

bool isGpuEncodePath(const QString &path)
{
    return path == QStringLiteral("GpuZeroCopyEncode") ||
           path == QStringLiteral("GpuCopyHwEncode");
}

bool isCpuHwFallbackEncodePath(const QString &path)
{
    return path == QStringLiteral("CpuUploadHwEncode") ||
           path == QStringLiteral("CpuReadbackHwEncode");
}

bool isSoftwareEncoderType(const QString &encoderType)
{
    const QString type = encoderType.trimmed().toLower();
    return type == QStringLiteral("software") ||
           type == QStringLiteral("cpu");
}

QString stableEncodePath(const QString &incoming,
                         const QString &current,
                         const QString &encoderType)
{
    if (isSoftwareEncoderType(encoderType))
        return QStringLiteral("CpuSoftwareEncode");
    if ((encoderType == QStringLiteral("hardware") ||
         encoderType == QStringLiteral("hardware_preferred")) &&
        isCpuHwFallbackEncodePath(current) &&
        isGpuEncodePath(incoming))
    {
        return current;
    }
    if (incoming != QStringLiteral("CpuSoftwareEncode"))
        return incoming;
    if ((encoderType.isEmpty() ||
         encoderType == QStringLiteral("hardware_preferred")) &&
        (current.isEmpty() || current == QStringLiteral("unknown")))
    {
        return QString();
    }
    if ((encoderType == QStringLiteral("hardware") ||
         encoderType == QStringLiteral("hardware_preferred")) &&
        isHardwareEncodePath(current))
    {
        return current;
    }
    return incoming;
}

} // namespace

void WebRtcCli::onAiranCaptureFrame(airan::media::CaptureFrameDescriptor frame)
{
    noteSessionTransportProgress();
    const QString backend = captureBackendLabel(frame);
    const QString capturePath = qstr(airan::media::toString(frame.current_capture_path));
    const QString encodePath = qstr(airan::media::toString(frame.current_encode_path));
    const QString fallbackReason = qstr(airan::media::toString(frame.fallback_reason));
    const QString captureMethod = captureMethodLabel(backend, frame.current_capture_path);

    QMetaObject::invokeMethod(this,
                              "applyCapturePipelineState",
                              Qt::QueuedConnection,
                              Q_ARG(QString, captureMethod),
                              Q_ARG(QString, capturePath),
                              Q_ARG(QString, encodePath),
                              Q_ARG(QString, fallbackReason));
    if (backend != captureMethod)
    {
        QMetaObject::invokeMethod(this,
                                  "applyCaptureBackendState",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, backend));
    }
}

void WebRtcCli::onAiranCaptureTransition(const airan::media::PathTransition &transition)
{
    QMetaObject::invokeMethod(this,
                              "applyCapturePipelineState",
                              Qt::QueuedConnection,
                              Q_ARG(QString, QString()),
                              Q_ARG(QString, qstr(airan::media::toString(transition.current_capture_path))),
                              Q_ARG(QString, qstr(airan::media::toString(transition.current_encode_path))),
                              Q_ARG(QString, qstr(airan::media::toString(transition.fallback_reason))));
}

void WebRtcCli::applyCapturePipelineState(const QString &captureMethod,
                                          const QString &capturePath,
                                          const QString &encodePath,
                                          const QString &fallbackReason)
{
    if (m_destroying)
        return;

    bool changed = false;
    if (!captureMethod.isEmpty() && captureMethod != m_currentCaptureMethod)
    {
        m_currentCaptureMethod = captureMethod;
        m_currentCaptureBackend = captureMethod.section(QLatin1Char('/'), 0, 0);
        changed = true;
    }
    if (!capturePath.isEmpty() && capturePath != m_currentCapturePath)
    {
        m_currentCapturePath = capturePath;
        changed = true;
    }
    const QString effectiveEncodePath = encodePath.isEmpty()
                                            ? encodePath
                                            : stableEncodePath(encodePath, m_currentEncodePath, m_currentEncoderType);
    if (!effectiveEncodePath.isEmpty() && effectiveEncodePath != m_currentEncodePath)
    {
        m_currentEncodePath = effectiveEncodePath;
        changed = true;
    }
    if (!fallbackReason.isEmpty() && fallbackReason != m_currentFallbackReason)
    {
        m_currentFallbackReason = fallbackReason;
        changed = true;
    }

    if (changed)
    {
        applyEffectiveVideoFpsIfNeeded("capture-pipeline");
        LOG_INFO("Capture pipeline state updated: capture={}, capturePath={}, encodePath={}, fallback={}",
                 m_currentCaptureMethod,
                 m_currentCapturePath,
                 m_currentEncodePath,
                 m_currentFallbackReason);
        notifyCurrentStreamConfig();
    }
}

void WebRtcCli::applyCaptureBackendState(const QString &captureBackend)
{
    if (m_destroying || m_currentCaptureBackend == captureBackend)
        return;
    m_currentCaptureBackend = captureBackend;
    notifyCurrentStreamConfig();
}
