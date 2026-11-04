#ifndef APP_RUNTIME_INTERNAL_H
#define APP_RUNTIME_INTERNAL_H

#ifndef AIRAN_DESK_VERSION
#error "AIRAN_DESK_VERSION must be provided by CMakeLists.txt"
#endif

namespace AppRuntime
{
    
    void registerCustomTypes();

    
    void initLog();

    
    bool isRunning(bool serviceMode = false);

    
    int runApplication(int argc, char *argv[], bool forceNoUi, bool serviceMode = false);
}

#endif /* APP_RUNTIME_INTERNAL_H */
