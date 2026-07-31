#ifndef AIRAN_BOUNDED_JSON_LINE_READER_H
#define AIRAN_BOUNDED_JSON_LINE_READER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QIODevice>

inline bool readBoundedJsonLine(QIODevice *device, QByteArray *line, qint64 maxBytes, int timeoutMs)
{
    if (!device || !line || maxBytes <= 0)
        return false;

    line->clear();
    QElapsedTimer timer;
    timer.start();
    while (true)
    {
        const QByteArray prefix = device->peek(maxBytes + 1);
        const int newline = prefix.indexOf('\n');
        if (newline >= 0)
        {
            if (newline + 1 > maxBytes)
                return false;
            *line = device->readLine();
            return line->size() <= maxBytes && line->contains('\n');
        }
        if (prefix.size() > maxBytes)
            return false;

        const qint64 remaining = static_cast<qint64>(timeoutMs) - timer.elapsed();
        if (remaining <= 0 || !device->waitForReadyRead(static_cast<int>(qMin<qint64>(remaining, 1000))))
            return false;
    }
}

#endif
