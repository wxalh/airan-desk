#ifndef OWNER_CONSENT_H
#define OWNER_CONSENT_H

#include <QString>

class QWidget;

namespace OwnerConsent
{
QString markerPath();
bool isAccepted();
bool ensureAccepted(bool uiAvailable, QWidget *parent = nullptr);
}

#endif /* OWNER_CONSENT_H */
