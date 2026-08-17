#include "util/file/stream/file_packet_util_stream_helpers.h"

#include <QFile>
#include <QCryptographicHash>

#include <cstring>

namespace
{

void writeBigEndian64(rtc::binary &target, size_t offset, quint64 value)
{
    for (int i = 0; i < 8; ++i)
    {
        const int shift = (7 - i) * 8;
        target[offset + i] = static_cast<std::byte>((static_cast<uint64_t>(value) >> shift) & 0xFF);
    }
}
} // namespace

namespace FilePacketStreamHelpers
{

QByteArray takeFragmentPayload(QByteArray &dataBuffer, QFile *file,
                               QCryptographicHash *fileHash)
{
    QByteArray fragmentPayload;

    if (!dataBuffer.isEmpty())
    {
        const int toTake = qMin(static_cast<int>(PAYLOAD_SIZE), dataBuffer.size());
        fragmentPayload.append(dataBuffer.left(toTake));
        dataBuffer.remove(0, toTake);
    }

    while (file && fragmentPayload.size() < PAYLOAD_SIZE && !file->atEnd())
    {
        QByteArray fileData = file->read(PAYLOAD_SIZE - fragmentPayload.size());
        if (fileData.isEmpty())
            break;
        if (fileHash)
            fileHash->addData(fileData);
        fragmentPayload.append(fileData);
    }

    return fragmentPayload;
}

rtc::binary makeFragment(const QByteArray &messageIdBytes,
                         quint64 totalFragments,
                         quint64 fragmentIndex,
                         const QByteArray &fragmentPayload)
{
    rtc::binary fragment(HEADER_SIZE + static_cast<quint64>(fragmentPayload.size()));
    std::memcpy(fragment.data(), messageIdBytes.constData(), 16);
    writeBigEndian64(fragment, 16, totalFragments);
    writeBigEndian64(fragment, 24, fragmentIndex);
    std::memcpy(fragment.data() + HEADER_SIZE, fragmentPayload.constData(), fragmentPayload.size());
    return fragment;
}

} // namespace FilePacketStreamHelpers
