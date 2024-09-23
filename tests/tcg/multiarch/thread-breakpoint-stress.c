/*
 * Test multiple threads hitting breakpoints.
 *
 * The main thread performs a lengthy syscall. The test verifies that this
 * does not interfere with the ability to stop threads.
 *
 * The counter thread constantly increments a value by 1. The test verifies
 * that it is stopped when another thread hits a breakpoint.
 *
 * The break threads constantly and simultaneously hit the same breakpoint.
 * The test verifies that GDB and gdbstub do not lose any hits and do not
 * deadlock.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

struct state {
    int counter;
    int done;
    pthread_barrier_t barrier;
    int break_counter;
};

static void *counter_loop(void *arg)
{
    struct state *s = arg;

    while (!__atomic_load_n(&s->done, __ATOMIC_SEQ_CST)) {
        __atomic_add_fetch(&s->counter, 1, __ATOMIC_SEQ_CST);
    }

    return NULL;
}

#define N_BREAK_THREADS 2
#define N_BREAKS 100

/* Non-static to avoid inlining. */
void break_here(struct state *s)
{
    __atomic_add_fetch(&s->break_counter, 1, __ATOMIC_SEQ_CST);
}

static void *break_loop(void *arg)
{
    struct state *s = arg;
    int i;

    pthread_barrier_wait(&s->barrier);
    for (i = 0; i < N_BREAKS; i++) {
        break_here(s);
    }

    return NULL;
}

int main(void)
{
    pthread_t break_threads[N_BREAK_THREADS], counter_thread;
    struct state s = {};
    int i, ret;

#ifdef __MICROBLAZE__
    /*
     * Microblaze has broken atomics.
     * See https://github.com/Xilinx/meta-xilinx/blob/xlnx-rel-v2024.1/meta-microblaze/recipes-devtools/gcc/gcc-12/0009-Patch-microblaze-Fix-atomic-boolean-return-value.patch
     */
    return EXIT_SUCCESS;
#endif

    ret = pthread_barrier_init(&s.barrier, NULL, N_BREAK_THREADS);
    assert(ret == 0);
    ret = pthread_create(&counter_thread, NULL, counter_loop, &s);
    assert(ret == 0);
    for (i = 0; i < N_BREAK_THREADS; i++) {
        ret = pthread_create(&break_threads[i], NULL, break_loop, &s);
        assert(ret == 0);
    }
    for (i = 0; i < N_BREAK_THREADS; i++) {
        ret = pthread_join(break_threads[i], NULL);
        assert(ret == 0);
    }
    __atomic_store_n(&s.done, 1, __ATOMIC_SEQ_CST);
    ret = pthread_join(counter_thread, NULL);
    assert(ret == 0);
    assert(s.break_counter == N_BREAK_THREADS * N_BREAKS);

    return EXIT_SUCCESS;
}
