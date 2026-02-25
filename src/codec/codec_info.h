#ifndef CODEC_INFO_H
#define CODEC_INFO_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <QString>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <memory>

struct HwAccelInfo
{
    int score = 0;
    const AVCodecHWConfig *config = nullptr;
    AVHWDeviceType hwDeviceType = AV_HWDEVICE_TYPE_NONE;
    QString hwDeviceTypeName;
    QList<AVPixelFormat> supportedPixFormats;
    QList<QString> supportedPixFormatNames;

    QJsonObject toJson() const
    {
        QJsonObject jsonObj;
        jsonObj["hwDeviceType"] = static_cast<int>(hwDeviceType);
        jsonObj["hwDeviceTypeName"] = hwDeviceTypeName;
        QJsonArray pixFormatsArray;
        for (const auto &pixFmtName : supportedPixFormatNames)
        {
            pixFormatsArray.append(pixFmtName);
        }
        jsonObj["supportedPixFormats"] = pixFormatsArray;
        return jsonObj;
    }
    QString toString()
    {
        return QJsonDocument(toJson()).toJson(QJsonDocument::Compact);
    }
};

class CodecInfo
{
public:
    enum Type
    {
        UNKNOWN = 0,
        ENCODER = 1,
        DECODER = 2
    };
    int score = 0;
    QString name;
    QString longName;
    // 0: unknown, 1: encoder, 2: decoder
    Type type = UNKNOWN;
    // 是否硬件加速
    bool isHardware = false;
    // ffmpeg codec pointer
    const AVCodec *codec = nullptr;
    // 支持的硬件类型
    QList<std::shared_ptr<HwAccelInfo>> supportedHwTypes;

    CodecInfo() {}
    ~CodecInfo() {supportedHwTypes.clear();}
    
    QJsonObject toJson()
    {
        QJsonObject jsonObj;
        jsonObj["name"] = name;
        jsonObj["longName"] = longName;
        jsonObj["type"] = static_cast<int>(type);
        jsonObj["isHardware"] = isHardware;
        QJsonArray hwTypesArray;
        for (const auto &hwAccelInfo : supportedHwTypes)
        {
            hwTypesArray.append(hwAccelInfo->toJson());
        }
        jsonObj["supportedHwTypes"] = hwTypesArray;
        return jsonObj;
    }

    QString toString()
    {
        return QJsonDocument(toJson()).toJson(QJsonDocument::Compact);
    }
};
#endif // CODEC_INFO_H