#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "highlight_thread.h"
#include "highlight.h"
#include "config.h"

#define MAX_QUEUE_SIZE 128 // rando pows of 2


typedef struct {
    ROW_DATA *row;
    int is_processed;
} HighlightTask;

static struct {
    pthread_t thread;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    pthread_cond_t process_cond;
    int running;
    
    HighlightTask queue[MAX_QUEUE_SIZE];
    int queue_size;
    int queue_head;
    int queue_tail;
} ThreadState;

static int queueTask(ROW_DATA *row) {
    if ((ThreadState.queue_tail + 1) % MAX_QUEUE_SIZE == ThreadState.queue_head) {
        return 0;
    }
    
    HighlightTask task = { row, 0 };
    ThreadState.queue[ThreadState.queue_tail] = task;
    ThreadState.queue_tail = (ThreadState.queue_tail + 1) % MAX_QUEUE_SIZE;
    ThreadState.queue_size++;
    
    return 1;
}

static HighlightTask *getTask(void) {
    if (ThreadState.queue_head == ThreadState.queue_tail) {
        return NULL;
    }
    
    HighlightTask *task = &ThreadState.queue[ThreadState.queue_head];
    return task;
}

static void removeTask(void) {
    if (ThreadState.queue_head == ThreadState.queue_tail) {
        return;
    }
    
    ThreadState.queue_head = (ThreadState.queue_head + 1) % MAX_QUEUE_SIZE;
    ThreadState.queue_size--;
}

static void *highlightThreadFunc(void *arg) {
    (void)arg;
    
    while (1) {
        pthread_mutex_lock(&ThreadState.queue_mutex);
        
        while (ThreadState.queue_head == ThreadState.queue_tail && ThreadState.running) {
            pthread_cond_wait(&ThreadState.queue_cond, &ThreadState.queue_mutex);
        }
        
        if (!ThreadState.running && ThreadState.queue_head == ThreadState.queue_tail) {
            pthread_mutex_unlock(&ThreadState.queue_mutex);
            break;
        }
        
        HighlightTask *task = getTask();
        if (task == NULL) {
            pthread_mutex_unlock(&ThreadState.queue_mutex);
            continue;
        }
        
        pthread_mutex_unlock(&ThreadState.queue_mutex);
        
        if (task->row && CONFIG.syntax) {
            editorUpdateSyntax(task->row);
        }
        
        pthread_mutex_lock(&ThreadState.queue_mutex);
        task->is_processed = 1;
        pthread_cond_signal(&ThreadState.process_cond);
        pthread_mutex_unlock(&ThreadState.queue_mutex);
    }
    
    return NULL;
}

void highlightThreadInit(void) {
    memset(&ThreadState, 0, sizeof(ThreadState));
    
    pthread_mutex_init(&ThreadState.queue_mutex, NULL);
    pthread_cond_init(&ThreadState.queue_cond, NULL);
    pthread_cond_init(&ThreadState.process_cond, NULL);
    
    ThreadState.running = 1;
    ThreadState.queue_head = 0;
    ThreadState.queue_tail = 0;
    ThreadState.queue_size = 0;
    
    pthread_create(&ThreadState.thread, NULL, highlightThreadFunc, NULL);
}

void highlightThreadShutdown(void) {
    pthread_mutex_lock(&ThreadState.queue_mutex);
    ThreadState.running = 0;
    pthread_cond_signal(&ThreadState.queue_cond);
    pthread_mutex_unlock(&ThreadState.queue_mutex);
    
    pthread_join(ThreadState.thread, NULL);
    
    pthread_mutex_destroy(&ThreadState.queue_mutex);
    pthread_cond_destroy(&ThreadState.queue_cond);
    pthread_cond_destroy(&ThreadState.process_cond);
}

void highlightThreadQueueRow(ROW_DATA *row) {
    if (row == NULL || CONFIG.syntax == NULL) return;
    
    pthread_mutex_lock(&ThreadState.queue_mutex);
    
    if (queueTask(row)) {
        pthread_cond_signal(&ThreadState.queue_cond);
    }
    
    pthread_mutex_unlock(&ThreadState.queue_mutex);
}

void highlightThreadProcess(void) {
    pthread_mutex_lock(&ThreadState.queue_mutex);
    
    while (ThreadState.queue_head != ThreadState.queue_tail) {
        HighlightTask *task = getTask();
        
        if (task && task->is_processed) {
            removeTask();
        }
        else {
            break;
        }
    }
    
    pthread_mutex_unlock(&ThreadState.queue_mutex);
} 