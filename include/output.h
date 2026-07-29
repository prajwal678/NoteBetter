#ifndef OUTPUT_H
#define OUTPUT_H

#include "buffer.h"


void editorRefreshScreen(void);
void setStatusMessage(const char *fmt, ...);
void editorDrawStatusBar(APPEND_BUFFER *ab);
void editorDrawMessageBar(APPEND_BUFFER *ab);
void editorDrawRows(APPEND_BUFFER *ab);

// recompute line number gutter width and the text area left over
void editorUpdateGutter(void);

#endif