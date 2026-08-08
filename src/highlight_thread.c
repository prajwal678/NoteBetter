#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "highlight_thread.h"
#include "highlight.h"
#include "row.h"
#include "config.h"


static struct {
    pthread_t thread;
    pthread_mutex_t m;
    pthread_cond_t work_cv;  // main to worker; range waiting
    pthread_cond_t idle_cv;  // worker to main; parked
    int running;
    int have_work;
    int active;  // worker is inside the row loop
    int lo, hi;
    int started;
    atomic_int pause;  // checked per row so pause is quick
} PF;

static void *prefetchThread(void *arg) {
    (void)arg;

    for (;;) {
        pthread_mutex_lock(&PF.m);
        while (PF.running && !PF.have_work) {
            pthread_cond_wait(&PF.work_cv, &PF.m);
        }
        if (!PF.running) {
            pthread_mutex_unlock(&PF.m);
            break;
        }

        int lo = PF.lo;
        int hi = PF.hi;
        PF.have_work = 0;
        PF.active = 1;
        pthread_mutex_unlock(&PF.m);

        // rows are stable here; main is blocked in read() and will call
        for (int i = lo; i <= hi; i++) {
            if (atomic_load_explicit(&PF.pause, memory_order_relaxed)) {
                break;
            }
            if (i < 0 || i >= CONFIG.numrows) {
                continue;
            }

            ROW_DATA *row = &CONFIG.row[i];
            editorRowEnsureRender(row);
            editorHighlightRow(row);
        }

        pthread_mutex_lock(&PF.m);
        PF.active = 0;
        pthread_cond_broadcast(&PF.idle_cv);
        pthread_mutex_unlock(&PF.m);
    }

    return NULL;
}

void highlightThreadInit(void) {
    if (PF.started) {
        return;
    }

    memset(&PF, 0, sizeof(PF));
    pthread_mutex_init(&PF.m, NULL);
    pthread_cond_init(&PF.work_cv, NULL);
    pthread_cond_init(&PF.idle_cv, NULL);
    atomic_init(&PF.pause, 1);
    PF.running = 1;

    if (pthread_create(&PF.thread, NULL, prefetchThread, NULL) != 0) {
        PF.running = 0;

        return;
    }
    PF.started = 1;
}

void highlightThreadPause(void) {
    if (!PF.started) {
        return;
    }

    atomic_store_explicit(&PF.pause, 1, memory_order_relaxed);

    pthread_mutex_lock(&PF.m);
    PF.have_work = 0;
    while (PF.active) {
        pthread_cond_wait(&PF.idle_cv, &PF.m);
    }
    pthread_mutex_unlock(&PF.m);
}

void highlightThreadResume(int lo, int hi) {
    if (!PF.started || lo > hi) {
        return;
    }

    pthread_mutex_lock(&PF.m);
    PF.lo = lo;
    PF.hi = hi;
    PF.have_work = 1;
    atomic_store_explicit(&PF.pause, 0, memory_order_relaxed);
    pthread_cond_signal(&PF.work_cv);
    pthread_mutex_unlock(&PF.m);
}

void highlightThreadShutdown(void) {
    if (!PF.started) {
        return;
    }
    PF.started = 0;

    atomic_store_explicit(&PF.pause, 1, memory_order_relaxed);

    pthread_mutex_lock(&PF.m);
    PF.running = 0;
    PF.have_work = 0;
    pthread_cond_broadcast(&PF.work_cv);
    pthread_mutex_unlock(&PF.m);

    pthread_join(PF.thread, NULL);

    pthread_mutex_destroy(&PF.m);
    pthread_cond_destroy(&PF.work_cv);
    pthread_cond_destroy(&PF.idle_cv);
}
