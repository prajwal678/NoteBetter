#ifndef HIGHLIGHT_THREAD_H
#define HIGHLIGHT_THREAD_H

#include "config.h"


void highlightThreadInit(void);
void highlightThreadShutdown(void);
// block until worker is parked; must be called before touching any row
void highlightThreadPause(void);
// hand the worker a row range and let it run
void highlightThreadResume(int lo, int hi);

#endif