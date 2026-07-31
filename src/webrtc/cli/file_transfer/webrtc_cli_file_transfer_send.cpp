#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"

#include <QFileInfo>


void WebRtcCli::sendFile(const QString &cliPath, const QString &ctlPath, const QString &transferId)
{
    QFileInfo info(cliPath);
    if (!info.exists())
    {
        LOG_ERROR("File or directory does not exist: {}", cliPath);
        sendFileErrorResponse(cliPath, "File or directory does not exist");
        return;
    }

    if (info.isFile())
    {
        sendSingleFile(cliPath, ctlPath, transferId, 0, info.size(), 1, 1);
    }
    else if (info.isDir())
    {
        sendDirectory(cliPath, ctlPath, transferId);
    }
    else
    {
        LOG_ERROR("Unknown file type: {}", cliPath);
        sendFileErrorResponse(cliPath, "Unknown file type");
    }
}
