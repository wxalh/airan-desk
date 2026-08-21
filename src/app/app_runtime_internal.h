#ifndef APP_RUNTIME_INTERNAL_H
#define APP_RUNTIME_INTERNAL_H

#ifndef AIRAN_DESK_VERSION
#define AIRAN_DESK_VERSION "1.1.3"
#endif

namespace AppRuntime
{
    
    void registerCustomTypes();

    
    void initLog();

    
    bool isRunning(bool serviceMode = false);

    
    int runApplication(int argc, char *argv[], bool forceNoUi, bool serviceMode = false);
}

#endif /* APP_RUNTIME_INTERNAL_H */
