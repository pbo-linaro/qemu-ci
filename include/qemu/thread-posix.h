#ifndef QEMU_THREAD_POSIX_H
#define QEMU_THREAD_POSIX_H

#include <pthread.h>
#include <semaphore.h>

struct QemuMutex {
    pthread_mutex_t lock;
#ifdef CONFIG_DEBUG_MUTEX
    const char *file;
    int line;
#endif
    bool initialized;
};

#ifdef CONFIG_DEBUG_MUTEX
#define QEMU_MUTEX_INITIALIZER {PTHREAD_MUTEX_INITIALIZER, NULL, 0, true}
#else
#define QEMU_MUTEX_INITIALIZER {PTHREAD_MUTEX_INITIALIZER, true}
#endif

/*
 * QemuRecMutex cannot be a typedef of QemuMutex lest we have two
 * compatible cases in _Generic.  See qemu/lockable.h.
 */
typedef struct QemuRecMutex {
    QemuMutex m;
} QemuRecMutex;

struct QemuCond {
    pthread_cond_t cond;
    bool initialized;
};

#define QEMU_COND_INITIALIZER {PTHREAD_COND_INITIALIZER, true}

struct QemuSemaphore {
    QemuMutex mutex;
    QemuCond cond;
    unsigned int count;
};

struct QemuEvent {
#ifndef __linux__
    pthread_mutex_t lock;
    pthread_cond_t cond;
#endif
    unsigned value;
    bool initialized;
};

struct QemuThread {
    pthread_t thread;
};

#endif
