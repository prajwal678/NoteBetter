#ifndef HIGHLIGHT_THREAD_H
#define HIGHLIGHT_THREAD_H

#include "config.h"


void highlightThreadInit(void);
void highlightThreadShutdown(void);
void highlightThreadQueueRow(ROW_DATA *row);
void highlightThreadProcess(void);

#endif 