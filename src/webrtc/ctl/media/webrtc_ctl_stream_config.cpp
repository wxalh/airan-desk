#include "webrtc/ctl/webrtc_ctl.h"

#include "common/constant.h"
#include "util/json/json_util.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMargins>
#include <QSize>

namespace
{
QString captureDisplayLabel(const QString &method, const QString &backend, const QString &path)
{
    QString label = backend.trimmed().isEmpty() ? method.trimmed() : backend.trimmed();
    if (label.isEmpty())
        label = QStringLiteral("--");
    if (path == QStringLiteral("CaptureReprobe") && !label.contains(QStringLiteral("reprobe"), Qt::CaseInsensitive))
        label += QStringLiteral("/reprobe");
    return label.toLower();
}

/*
 * Clamps remote video coded size, visible size, and padding to valid ranges.
 */
void sanitizeRemoteVideoRect(QSize &codedSize, QSize &visibleSize, QMargins &padding)
{
    codedSize = QSize(qMax(0, codedSize.width()), qMax(0, codedSize.height()));
    padding = QMargins(qMax(0, padding.left()),
                       qMax(0, padding.top()),
                       qMax(0, padding.right()),
                       qMax(0, padding.bottom()));

    int visibleW = qMax(0, visibleSize.width());
    int visibleH = qMax(0, visibleSize.height());

    if (codedSize.width() > 0)
    {
        visibleW = qBound(0, visibleW, codedSize.width());
        const int horizontalPad = qMax(0, codedSize.width() - visibleW);
        const int left = qMin(padding.left(), horizontalPad);
        padding.setLeft(left);
        padding.setRight(qMin(padding.right(), qMax(0, horizontalPad - left)));
    }
    if (codedSize.height() > 0)
    {
        visibleH = qBound(0, visibleH, codedSize.height());
        const int verticalPad = qMax(0, codedSize.height() - visibleH);
        const int top = qMin(padding.top(), verticalPad);
        padding.setTop(top);
        padding.setBottom(qMin(padding.bottom(), qMax(0, verticalPad - top)));
    }
    visibleSize = QSize(visibleW, visibleH);
}
} // namespace

/*
 * Applies stream status/configuration reported by the remote side over DataChannel.
 */
void WebRtcCtl::applyLocalStreamConfig(const QJsonObject &object)
{
    if (JsonUtil::getString(object, Constant::KEY_MSGTYPE) != Constant::TYPE_STREAM_CONFIG)
        return;

    bool streamResetRequired = false;
    const bool statusOnly = JsonUtil::getBool(object, Constant::KEY_STATUS_ONLY, false);

    const QString networkPath = JsonUtil::getString(object, Constant::KEY_NETWORK_PATH).toLower();
    if (!statusOnly && (networkPath == QStringLiteral("auto") ||
                        networkPath == QStringLiteral("direct") ||
                        networkPath == QStringLiteral("turn_udp") ||
                        networkPath == QStringLiteral("turn_tcp")))
    {
        m_networkPath = networkPath;
        publishNetworkPathState();
    }

    const QString qualityProfile = JsonUtil::getString(object, Constant::KEY_QUALITY_PROFILE).trimmed().toLower();
    if (!qualityProfile.isEmpty())
    {
        QString normalizedQuality = qualityProfile == QStringLiteral("auto")
                                        ? QStringLiteral("auto")
                                        : QStringLiteral("lan_hd");
        if (qualityProfile == QStringLiteral("balanced"))
            normalizedQuality = QStringLiteral("balanced");
        else if (qualityProfile == QStringLiteral("weak") ||
                 qualityProfile == QStringLiteral("weak_clear") ||
                 qualityProfile == QStringLiteral("lowbandwidth") ||
                 qualityProfile == QStringLiteral("clear"))
            normalizedQuality = QStringLiteral("weak_clear");
        if (!statusOnly && normalizedQuality != m_qualityProfile)
        {
            m_qualityProfile = normalizedQuality;
            LOG_INFO("Control stream quality profile updated: {}", m_qualityProfile);
        }
        else if (statusOnly)
        {
            LOG_DEBUG("Remote stream quality profile reported: {}", normalizedQuality);
        }
    }

    const bool hasAnyVideoRect = object.contains(Constant::KEY_CODED_WIDTH) || object.contains(Constant::KEY_CODED_HEIGHT) ||
                                 object.contains(Constant::KEY_VISIBLE_WIDTH) || object.contains(Constant::KEY_VISIBLE_HEIGHT) ||
                                 object.contains(Constant::KEY_PAD_LEFT) || object.contains(Constant::KEY_PAD_TOP) ||
                                 object.contains(Constant::KEY_PAD_RIGHT) || object.contains(Constant::KEY_PAD_BOTTOM);
    const bool hasFullVideoRect = object.contains(Constant::KEY_CODED_WIDTH) && object.contains(Constant::KEY_CODED_HEIGHT) &&
                                  object.contains(Constant::KEY_VISIBLE_WIDTH) && object.contains(Constant::KEY_VISIBLE_HEIGHT) &&
                                  object.contains(Constant::KEY_PAD_LEFT) && object.contains(Constant::KEY_PAD_TOP) &&
                                  object.contains(Constant::KEY_PAD_RIGHT) && object.contains(Constant::KEY_PAD_BOTTOM);
    if (hasFullVideoRect)
    {
        QSize remoteCodedSize(JsonUtil::getInt(object, Constant::KEY_CODED_WIDTH, 0),
                              JsonUtil::getInt(object, Constant::KEY_CODED_HEIGHT, 0));
        QSize remoteVisibleSize(JsonUtil::getInt(object, Constant::KEY_VISIBLE_WIDTH, 0),
                                JsonUtil::getInt(object, Constant::KEY_VISIBLE_HEIGHT, 0));
        QMargins remotePadding(JsonUtil::getInt(object, Constant::KEY_PAD_LEFT, 0),
                               JsonUtil::getInt(object, Constant::KEY_PAD_TOP, 0),
                               JsonUtil::getInt(object, Constant::KEY_PAD_RIGHT, 0),
                               JsonUtil::getInt(object, Constant::KEY_PAD_BOTTOM, 0));
        sanitizeRemoteVideoRect(remoteCodedSize, remoteVisibleSize, remotePadding);
        if (remoteCodedSize != m_remoteCodedSize ||
            remoteVisibleSize != m_remoteVisibleSize ||
            remotePadding != m_remotePadding)
        {
            m_remoteCodedSize = remoteCodedSize;
            m_remoteVisibleSize = remoteVisibleSize;
            m_remotePadding = remotePadding;
            LOG_INFO("Remote video rect: coded={}x{}, visible={}x{}, pad L{} T{} R{} B{}",
                     m_remoteCodedSize.width(), m_remoteCodedSize.height(),
                     m_remoteVisibleSize.width(), m_remoteVisibleSize.height(),
                     m_remotePadding.left(), m_remotePadding.top(), m_remotePadding.right(), m_remotePadding.bottom());
        }
    }
    else if (hasAnyVideoRect)
    {
        LOG_WARN("Ignoring incomplete stream video rect from same-version protocol peer");
    }

    if (!statusOnly && (object.contains(Constant::KEY_WIDTH) || object.contains(Constant::KEY_HEIGHT)))
    {
        const int requestedWidth = JsonUtil::getInt(object, Constant::KEY_WIDTH, m_requestedWidth);
        const int requestedHeight = JsonUtil::getInt(object, Constant::KEY_HEIGHT, m_requestedHeight);
        if (requestedWidth != m_requestedWidth || requestedHeight != m_requestedHeight)
        {
            m_requestedWidth = requestedWidth;
            m_requestedHeight = requestedHeight;
            streamResetRequired = true;
            LOG_INFO("Control receive resolution switched to {}x{}", m_requestedWidth, m_requestedHeight);
        }
    }

    if (streamResetRequired)
    {
        LOG_INFO("Control receive stream settings changed");
    }

    const QJsonArray screens = JsonUtil::getArray(object, Constant::KEY_SCREENS);
    const QString normalizedScreenId = JsonUtil::getString(object, Constant::KEY_SCREEN_ID);
    const QString screensSignature = screens.isEmpty()
                                         ? QString()
                                         : QString::fromUtf8(QJsonDocument(screens).toJson(QJsonDocument::Compact));
    const bool screensChanged = !screens.isEmpty() && screensSignature != m_remoteScreensSignature;
    const bool screenIdChanged = normalizedScreenId != m_remoteScreenId;
    if (screensChanged || screenIdChanged)
    {
        if (!screens.isEmpty())
        {
            m_remoteScreens = screens;
            m_remoteScreensSignature = screensSignature;
        }
        m_remoteScreenId = normalizedScreenId;
        Q_EMIT remoteScreensChanged(m_remoteScreens, m_remoteScreenId);
        LOG_INFO("Remote screens reported: count={}, current={}", m_remoteScreens.size(), m_remoteScreenId);
    }

    const QString osName = JsonUtil::getString(object, Constant::KEY_OS).toLower();
    if (!osName.isEmpty() && osName != m_remoteOsName)
    {
        m_remoteOsName = osName;
        Q_EMIT remoteOsChanged(osName);
        LOG_INFO("Remote OS reported: {}", osName);
    }

    const QString encodePath = JsonUtil::getString(object, Constant::KEY_ENCODE_PATH);
    QString encoderName = JsonUtil::getString(object, Constant::KEY_ENCODER_NAME);
    if (!encoderName.isEmpty() && !encodePath.isEmpty())
        encoderName = QStringLiteral("%1/%2").arg(encoderName, encodePath);
    const QString encoderType = JsonUtil::getString(object, Constant::KEY_ENCODER_TYPE);
    if ((!encoderName.isEmpty() || !encoderType.isEmpty()) &&
        (encoderName != m_remoteEncoderName || encoderType != m_remoteEncoderType))
    {
        m_remoteEncoderName = encoderName;
        m_remoteEncoderType = encoderType;
        Q_EMIT remoteEncoderChanged(encoderName, encoderType);
        LOG_DEBUG("Remote encoder status: encoder={}, type={}, encodePath={}", encoderName, encoderType, encodePath);
    }

    const QString videoCodec = JsonUtil::getString(object, Constant::KEY_VIDEO_CODEC);
    const QString captureMethod = JsonUtil::getString(object, Constant::KEY_CAPTURE_METHOD);
    const QString captureBackend = JsonUtil::getString(object, Constant::KEY_CAPTURE_BACKEND);
    const QString capturePath = JsonUtil::getString(object, Constant::KEY_CAPTURE_PATH);
    const QString fallbackReason = JsonUtil::getString(object, Constant::KEY_FALLBACK_REASON);
    const QString captureLabel = captureDisplayLabel(captureMethod, captureBackend, capturePath);
    if ((!videoCodec.isEmpty() || !captureLabel.isEmpty()) &&
        (videoCodec != m_remoteMediaCodec || captureLabel != m_remoteMediaCaptureLabel))
    {
        m_remoteMediaCodec = videoCodec;
        m_remoteMediaCaptureLabel = captureLabel;
        Q_EMIT remoteMediaStateChanged(videoCodec, captureLabel);
        LOG_DEBUG("Remote media status: codec={}, capture={}, backend={}, capturePath={}, encodePath={}, fallback={}",
                  videoCodec,
                  captureLabel,
                  captureBackend,
                  capturePath,
                  encodePath,
                  fallbackReason);
    }
}
