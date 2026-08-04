#ifndef OUTPUT_H
#define OUTPUT_H

void editorRefreshScreen(void);
void setStatusMessage(const char *fmt, ...);

// recompute line number gutter width and the text area left over
void editorUpdateGutter(void);

#endif