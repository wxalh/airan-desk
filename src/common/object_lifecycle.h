#ifndef OBJECT_LIFECYCLE_H
#define OBJECT_LIFECYCLE_H

#include "logger_manager.h"
#include "thread_shutdown.h"

#define DELETE_PTR_FUNC(obj_ptr) \
    if (obj_ptr)                 \
    {                            \
        obj_ptr->disconnect();   \
        delete obj_ptr;          \
        obj_ptr = nullptr;       \
    }

#define DELETELATER_PTR_FUNC(obj_ptr) \
    if (obj_ptr)                      \
    {                                 \
        obj_ptr->disconnect();        \
        obj_ptr->deleteLater();       \
        obj_ptr = nullptr;            \
    }

#define STOP_OBJ_THREAD(thread) \
    if (thread.isRunning())     \
    {                           \
        ThreadShutdown::shutdownThread(thread, thread.objectName().toUtf8().constData()); \
    }

#define STOP_PTR_THREAD(thread_ptr) \
    if (thread_ptr && thread_ptr->isRunning()) \
    {                                           \
        ThreadShutdown::shutdownThread(thread_ptr, thread_ptr->objectName().toUtf8().constData()); \
    }

#endif /* OBJECT_LIFECYCLE_H */
