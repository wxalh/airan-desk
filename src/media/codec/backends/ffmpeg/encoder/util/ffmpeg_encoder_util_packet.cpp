#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include <algorithm>

namespace airan::media::ffmpeg
{
namespace
{
constexpr uint8_t kAnnexBStartCode[] = {0, 0, 0, 1};


bool isStartCode3(const uint8_t *data, size_t size, size_t pos)
{
    return pos + 3 <= size && data[pos] == 0 && data[pos + 1] == 0 && data[pos + 2] == 1;
}


bool isStartCode4(const uint8_t *data, size_t size, size_t pos)
{
    return pos + 4 <= size && data[pos] == 0 && data[pos + 1] == 0 && data[pos + 2] == 0 && data[pos + 3] == 1;
}


size_t startCodeSize(const uint8_t *data, size_t size, size_t pos)
{
    if (isStartCode4(data, size, pos))
        return 4;
    if (isStartCode3(data, size, pos))
        return 3;
    return 0;
}


bool hasAnnexBStartCode(const uint8_t *data, size_t size)
{
    for (size_t i = 0; i + 3 <= size; ++i)
        if (startCodeSize(data, size, i) != 0)
            return true;
    return false;
}


bool annexBHasSpsAndPps(const uint8_t *data, size_t size)
{
    bool hasSps = false;
    bool hasPps = false;
    size_t pos = 0;
    while (pos + 3 <= size)
    {
        const size_t prefix = startCodeSize(data, size, pos);
        if (prefix == 0)
        {
            ++pos;
            continue;
        }

        size_t nal = pos + prefix;
        while (nal < size && data[nal] == 0)
            ++nal;
        if (nal < size)
        {
            const uint8_t type = data[nal] & 0x1f;
            hasSps = hasSps || type == 7;
            hasPps = hasPps || type == 8;
            if (hasSps && hasPps)
                return true;
        }
        pos = nal + 1;
    }
    return false;
}


void appendAnnexBNal(std::vector<uint8_t> &out, const uint8_t *data, size_t size)
{
    if (!data || size == 0)
        return;
    out.insert(out.end(), std::begin(kAnnexBStartCode), std::end(kAnnexBStartCode));
    out.insert(out.end(), data, data + size);
}


bool appendAvccExtradataAsAnnexB(std::vector<uint8_t> &out, const uint8_t *data, size_t size)
{
    if (!data || size < 7 || data[0] != 1)
        return false;

    size_t pos = 5;
    const int spsCount = data[pos++] & 0x1f;
    for (int i = 0; i < spsCount; ++i)
    {
        if (size - pos < 2)
            return false;
        const size_t nalSize = (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
        pos += 2;
        if (nalSize > size - pos)
            return false;
        appendAnnexBNal(out, data + pos, nalSize);
        pos += nalSize;
    }

    if (pos >= size)
        return false;
    const int ppsCount = data[pos++];
    for (int i = 0; i < ppsCount; ++i)
    {
        if (size - pos < 2)
            return false;
        const size_t nalSize = (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
        pos += 2;
        if (nalSize > size - pos)
            return false;
        appendAnnexBNal(out, data + pos, nalSize);
        pos += nalSize;
    }
    return true;
}


int avccLengthSize(const AVCodecContext *ctx)
{
    if (!ctx || !ctx->extradata || ctx->extradata_size < 5 || ctx->extradata[0] != 1)
        return 4;
    return (ctx->extradata[4] & 0x03) + 1;
}


bool appendLengthPrefixedPacketAsAnnexB(std::vector<uint8_t> &out, const uint8_t *data, size_t size, int lengthSize)
{
    if (!data || size == 0 || lengthSize <= 0 || lengthSize > 4)
        return false;

    size_t pos = 0;
    bool wrote = false;
    while (size - pos >= static_cast<size_t>(lengthSize))
    {
        size_t nalSize = 0;
        for (int i = 0; i < lengthSize; ++i)
            nalSize = (nalSize << 8) | data[pos + static_cast<size_t>(i)];
        pos += static_cast<size_t>(lengthSize);
        if (nalSize == 0 || nalSize > size - pos)
            return false;
        appendAnnexBNal(out, data + pos, nalSize);
        pos += nalSize;
        wrote = true;
    }
    return wrote && pos == size;
}
} /* namespace */


std::vector<uint8_t> encodedPacketData(const AVCodecContext *ctx, const AVPacket *packet, bool h264KeyFrame)
{
    std::vector<uint8_t> data;
    if (!packet || !packet->data || packet->size <= 0)
        return data;

    const auto *packetData = packet->data;
    const size_t packetSize = static_cast<size_t>(packet->size);
    const bool packetIsAnnexB = hasAnnexBStartCode(packetData, packetSize);
    if (h264KeyFrame && ctx && ctx->extradata && ctx->extradata_size > 0 &&
        (!packetIsAnnexB || !annexBHasSpsAndPps(packetData, packetSize)))
    {
        const auto *extra = ctx->extradata;
        const size_t extraSize = static_cast<size_t>(ctx->extradata_size);
        if (hasAnnexBStartCode(extra, extraSize))
            data.insert(data.end(), extra, extra + extraSize);
        else if (!appendAvccExtradataAsAnnexB(data, extra, extraSize))
            appendAnnexBNal(data, extra, extraSize);
    }

    if (packetIsAnnexB)
    {
        data.insert(data.end(), packetData, packetData + packetSize);
        return data;
    }

    const size_t prefixSize = data.size();
    if (appendLengthPrefixedPacketAsAnnexB(data, packetData, packetSize, avccLengthSize(ctx)))
        return data;

    data.resize(prefixSize);
    data.insert(data.end(), packetData, packetData + packetSize);
    return data;
}
} /* namespace airan::media::ffmpeg */
#endif
