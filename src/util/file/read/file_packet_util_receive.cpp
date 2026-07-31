

#include "util/file/file_packet_util.h"
#include "util/text/convert_util.h"


void FilePacketUtil::processReceivedFragment(const rtc::binary &data, const QString &channelName)
{
    if (data.size() < HEADER_SIZE)
    {
        LOG_ERROR("Fragment too small: {}", ConvertUtil::formatFileSize(data.size()));
        return;
    }
    if (data.size() > HEADER_SIZE + PAYLOAD_SIZE)
    {
        LOG_ERROR("Fragment payload is too large: {} bytes", data.size() - HEADER_SIZE);
        return;
    }

    
    QByteArray messageIdArray;
    messageIdArray.resize(16);
    for (int i = 0; i < 16; ++i)
    {
        messageIdArray[i] = static_cast<char>(data[i]);
    }
    QUuid messageId = QUuid::fromRfc4122(messageIdArray);

    if (messageId.isNull())
    {
        LOG_ERROR("Invalid message ID in fragment");
        return;
    }

    
    quint64 totalFragments = 0;
    for (int i = 0; i < 8; ++i)
    {
        totalFragments = (totalFragments << 8) | static_cast<quint8>(data[16 + i]);
    }

    
    quint64 fragmentIndex = 0;
    for (int i = 0; i < 8; ++i)
    {
        fragmentIndex = (fragmentIndex << 8) | static_cast<quint8>(data[24 + i]);
    }

    
    constexpr quint64 kMaxFragments = (MAX_REASONABLE_OFFSET + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;
    if (totalFragments == 0 || totalFragments > kMaxFragments) {
        LOG_ERROR("Invalid totalFragments: {}", totalFragments);
        return;
    }

    if (fragmentIndex >= totalFragments) {
        LOG_ERROR("Invalid fragmentIndex: {} >= {}", fragmentIndex, totalFragments);
        return;
    }

    rtc::binary fragment(data.begin() + HEADER_SIZE, data.end());
    if (fragment.empty())
    {
        LOG_ERROR("Fragment payload is empty");
        return;
    }

    QString fullMessageId = channelName + "_" + messageId.toString();
    reassembleFragment(fullMessageId, fragmentIndex, totalFragments, fragment);
}
