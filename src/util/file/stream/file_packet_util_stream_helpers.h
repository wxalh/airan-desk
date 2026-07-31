#ifndef FILE_PACKET_UTIL_STREAM_HELPERS_H
#define FILE_PACKET_UTIL_STREAM_HELPERS_H

#include "util/file/file_packet_util.h"

#include <QByteArray>

class QFile;

namespace FilePacketStreamHelpers
{


QByteArray takeFragmentPayload(QByteArray &dataBuffer, QFile *file);


rtc::binary makeFragment(const QByteArray &messageIdBytes,
                         quint64 totalFragments,
                         quint64 fragmentIndex,
                         const QByteArray &fragmentPayload);

} // namespace FilePacketStreamHelpers

#endif /* FILE_PACKET_UTIL_STREAM_HELPERS_H */
