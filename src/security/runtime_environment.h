#ifndef RUNTIME_ENVIRONMENT_H
#define RUNTIME_ENVIRONMENT_H

namespace RuntimeEnvironment
{
bool detectInteractiveUi();
void setDetectedUiAvailability(bool available);
bool uiAvailable();
}

#endif /* RUNTIME_ENVIRONMENT_H */
