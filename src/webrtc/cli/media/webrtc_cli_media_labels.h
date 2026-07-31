#ifndef WEBRTC_CLI_MEDIA_LABELS_H
#define WEBRTC_CLI_MEDIA_LABELS_H

#include <QString>

namespace webrtc_cli_internal
{


QString localOsName();


QString desktopCaptureMethodLabel();


QString encoderBackendFromImplementation(const QString &implementation);


QString encoderTypeFromBackend(const QString &backend);


QString readableEncoderName(const QString &implementation, const QString &codec);


QString encoderTypeFromImplementation(const QString &implementation);

} /* namespace webrtc_cli_internal */

#endif /* WEBRTC_CLI_MEDIA_LABELS_H */
