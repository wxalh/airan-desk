#pragma once

#include "rtc/core/rtc.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class QObject;

QJsonArray buildLocalVideoCodecCapabilitiesJson();
std::vector<rtc::VideoCodecCapability> parseVideoCodecCapabilities(const QJsonObject &object);
QString summarizeVideoCodecCapabilities(const QJsonArray &array, QObject *context = nullptr);
QString summarizeVideoCodecCapabilities(const std::vector<rtc::VideoCodecCapability> &capabilities, QObject *context = nullptr);
void logVideoCodecCapabilities(const char *label, const std::vector<rtc::VideoCodecCapability> &capabilities);
