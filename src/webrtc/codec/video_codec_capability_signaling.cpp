#include "webrtc/codec/video_codec_capability_signaling.h"

#include "media/codec/airan_video_codec_backend.h"
#include "common/constant.h"
#include "common/logger_manager.h"
#include "util/json/json_util.h"

#include <QStringList>
#include <QCoreApplication>

#include <utility>


QJsonArray buildLocalVideoCodecCapabilitiesJson()
{
    QJsonArray array;
    for (const auto &capability : airan::media::localVideoCodecCapabilities())
    {
        QJsonArray scalabilityModes;
        for (const auto &mode : capability.scalabilityModes)
            scalabilityModes.append(QString::fromStdString(mode));
        QJsonObject item = JsonUtil::createObject()
                               .add(Constant::KEY_CODEC, capability.codec)
                               .add(Constant::KEY_BACKEND, capability.backend)
                               .add(Constant::KEY_CAN_ENCODE, capability.canEncode)
                               .add(Constant::KEY_CAN_DECODE, capability.canDecode)
                               .add(Constant::KEY_HARDWARE, capability.hardware)
                               .add(Constant::KEY_ZERO_COPY, QString::fromStdString(capability.zeroCopyPath))
                               .add(Constant::KEY_MAX_SPATIAL_LAYERS, capability.maxSpatialLayers)
                               .add(Constant::KEY_MAX_TEMPORAL_LAYERS, capability.maxTemporalLayers)
                               .add(Constant::KEY_SIMULCAST, capability.simulcast)
                               .add(Constant::KEY_SVC, capability.svc)
                               .add(Constant::KEY_SCALABILITY_MODES, scalabilityModes)
                               .add(Constant::KEY_NOTES, QString::fromStdString(capability.notes))
                               .build();
        array.append(item);
    }
    return array;
}


std::vector<rtc::VideoCodecCapability> parseVideoCodecCapabilities(const QJsonObject &object)
{
    std::vector<rtc::VideoCodecCapability> capabilities;
    const QJsonArray array = JsonUtil::getArray(object, Constant::KEY_VIDEO_CODEC_CAPABILITIES);
    capabilities.reserve(static_cast<size_t>(array.size()));
    for (const QJsonValue &value : array)
    {
        if (!value.isObject())
            continue;
        const QJsonObject item = value.toObject();
        rtc::VideoCodecCapability capability;
        capability.codec = JsonUtil::getString(item, Constant::KEY_CODEC).toStdString();
        capability.backend = JsonUtil::getString(item, Constant::KEY_BACKEND).toStdString();
        capability.canEncode = JsonUtil::getBool(item, Constant::KEY_CAN_ENCODE);
        capability.canDecode = JsonUtil::getBool(item, Constant::KEY_CAN_DECODE);
        capability.hardware = JsonUtil::getBool(item, Constant::KEY_HARDWARE);
        capability.zeroCopyPath = JsonUtil::getString(item, Constant::KEY_ZERO_COPY).toStdString();
        capability.maxSpatialLayers = (std::max)(1, JsonUtil::getInt(item, Constant::KEY_MAX_SPATIAL_LAYERS, 1));
        capability.maxTemporalLayers = (std::max)(1, JsonUtil::getInt(item, Constant::KEY_MAX_TEMPORAL_LAYERS, 1));
        capability.simulcast = JsonUtil::getBool(item, Constant::KEY_SIMULCAST);
        capability.svc = JsonUtil::getBool(item, Constant::KEY_SVC);
        const QJsonArray scalabilityModes = JsonUtil::getArray(item, Constant::KEY_SCALABILITY_MODES);
        for (const auto &mode : scalabilityModes)
        {
            if (mode.isString())
                capability.scalabilityModes.push_back(mode.toString().toStdString());
        }
        if (capability.scalabilityModes.empty())
            capability.scalabilityModes.push_back("L1T1");
        capability.notes = JsonUtil::getString(item, Constant::KEY_NOTES).toStdString();
        if (!capability.codec.empty())
            capabilities.push_back(std::move(capability));
    }
    return capabilities;
}


QString capabilityFlagText(const QString &flag, QObject *context)
{
    Q_UNUSED(context);
    if (flag == QStringLiteral("enc"))
        return QCoreApplication::translate("VideoCodecCapability", "Encode");
    if (flag == QStringLiteral("dec"))
        return QCoreApplication::translate("VideoCodecCapability", "Decode");
    if (flag == QStringLiteral("hw"))
        return QCoreApplication::translate("VideoCodecCapability", "Hardware");
    if (flag == QStringLiteral("zero-copy"))
        return QCoreApplication::translate("VideoCodecCapability", "zero-copy");
    return flag;
}


QString summarizeVideoCodecCapabilities(const std::vector<rtc::VideoCodecCapability> &capabilities, QObject *context)
{
    QStringList summary;
    int total = 0;
    for (const auto &capability : capabilities)
    {
        if (!capability.canEncode && !capability.canDecode)
            continue;
        ++total;
        if (summary.size() >= 6)
            continue;
        QStringList flags;
        if (capability.canEncode)
            flags.append(capabilityFlagText(QStringLiteral("enc"), context));
        if (capability.canDecode)
            flags.append(capabilityFlagText(QStringLiteral("dec"), context));
        if (capability.hardware)
            flags.append(capabilityFlagText(QStringLiteral("hw"), context));
        if (!capability.zeroCopyPath.empty())
            flags.append(capabilityFlagText(QStringLiteral("zero-copy"), context));
        if (capability.simulcast)
            flags.append(QStringLiteral("simulcast"));
        if (capability.svc)
            flags.append(QStringLiteral("svc"));
        summary.append(QStringLiteral("%1/%2(%3)")
                           .arg(QString::fromStdString(capability.codec),
                                QString::fromStdString(capability.backend),
                                flags.join(QLatin1Char('+'))));
    }
    if (summary.isEmpty())
        return QCoreApplication::translate("VideoCodecCapability", "none");
    QString text = summary.join(QStringLiteral(", "));
    if (total > summary.size())
        text += QCoreApplication::translate("VideoCodecCapability", ", ... total %1").arg(total);
    return text;
}


QString summarizeVideoCodecCapabilities(const QJsonArray &array, QObject *context)
{
    QJsonObject object;
    object.insert(Constant::KEY_VIDEO_CODEC_CAPABILITIES, array);
    return summarizeVideoCodecCapabilities(parseVideoCodecCapabilities(object), context);
}


void logVideoCodecCapabilities(const char *label, const std::vector<rtc::VideoCodecCapability> &capabilities)
{
    QStringList summary;
    for (const auto &capability : capabilities)
    {
        summary.append(QStringLiteral("%1/%2:e%3:d%4:hw%5:zc%6:S%7:T%8:sim%9:svc%10")
                           .arg(QString::fromStdString(capability.codec))
                           .arg(QString::fromStdString(capability.backend))
                           .arg(capability.canEncode ? QStringLiteral("1") : QStringLiteral("0"))
                           .arg(capability.canDecode ? QStringLiteral("1") : QStringLiteral("0"))
                           .arg(capability.hardware ? QStringLiteral("1") : QStringLiteral("0"))
                           .arg(capability.zeroCopyPath.empty() ? QStringLiteral("0") : QStringLiteral("1"))
                           .arg(QString::number(capability.maxSpatialLayers))
                           .arg(QString::number(capability.maxTemporalLayers))
                           .arg(capability.simulcast ? QStringLiteral("1") : QStringLiteral("0"))
                           .arg(capability.svc ? QStringLiteral("1") : QStringLiteral("0")));
    }
    LOG_INFO("{} video codec capabilities: count={}, {}",
             label ? label : "Remote",
             capabilities.size(),
             summary.isEmpty() ? QStringLiteral("none") : summary.join(QStringLiteral(",")));
}
