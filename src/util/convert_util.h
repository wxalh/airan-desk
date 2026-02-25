#ifndef CONVERT_H
#define CONVERT_H

#include <QObject>

class ConvertUtil
{
public:
    static QString formatFileSize(qint64 bytes);
};

#endif // CONVERT_H
