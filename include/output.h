#ifndef OUTPUT_H
#define OUTPUT_H


void editorRefreshScreen(void);
void setStatusMessage(const char *fmt, ...);

void editorUpdateGutter(void);
void editorFreeOutput(void);

#endif