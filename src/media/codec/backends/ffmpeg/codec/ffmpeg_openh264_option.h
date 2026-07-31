#pragma once

#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_types.h"
#include "media/codec/openh264/openh264_binary_manager.h"

#include <QString>

namespace airan::media::ffmpeg
{

#if defined(AIRAN_HAVE_FFMPEG)
using OpenH264OptionSetter = int (*)(void *, const char *, const char *, int);

bool configureOpenH264LibraryOption(const CodecProbe &probe,
                                    AVCodecContext *context,
                                    bool enabled,
                                    const QString &configuredPath,
                                    OpenH264OptionSetter optionSetter,
                                    QString *reasonCode = nullptr,
                                    const QString &storageRoot = QString());
#endif

} // namespace airan::media::ffmpeg
