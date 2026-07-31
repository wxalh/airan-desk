#ifndef AIRAN_UTIL_TEXT_CONVERT_UTIL_H
#define AIRAN_UTIL_TEXT_CONVERT_UTIL_H

#include <QObject>

class ConvertUtil
{
public:
    static QString formatFileSize(qint64 bytes);
};

#endif /* AIRAN_UTIL_TEXT_CONVERT_UTIL_H */
