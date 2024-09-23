#ifndef QEMU_THREAD_WIN32_H
#define QEMU_THREAD_WIN32_H

#include <windows.h>

struct QemuMutex {
    SRWLOCK lock;
#ifdef CONFIG_DEBUG_MUTEX
    const char *file;
    int line;
#endif
    bool initialized;
};

#ifdef CONFIG_DEBUG_MUTEX
#define QEMU_MUTEX_INITIALIZER {SRWLOCK_INIT, NULL, 0, true}
#else
#define QEMU_MUTEX_INITIALIZER {SRWLOCK_INIT, true}
#endif

typedef struct QemuRecMutex QemuRecMutex;
struct QemuRecMutex {
    CRITICAL_SECTION lock;
    bool initialized;
};

struct QemuCond {
    CONDITION_VARIABLE var;
    bool initialized;
};

#define QEMU_COND_INITIALIZER {CONDITION_VARIABLE_INIT, true}

struct QemuSemaphore {
    HANDLE sema;
    bool initialized;
};

struct QemuEvent {
    int value;
    HANDLE event;
    bool initialized;
};

typedef struct QemuThreadData QemuThreadData;
struct QemuThread {
    QemuThreadData *data;
    unsigned tid;
};

/* Only valid for joinable threads.  */
HANDLE qemu_thread_get_handle(struct QemuThread *thread);

#endif
