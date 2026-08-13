#ifndef EDITOR_H
#define EDITOR_H

#include "config.h"


void editorInsertChar(int c);
void editorInsertNewline(void);
void editorDelChar(void);
void editorFindCallback(char *query, int key);
void editorFind(void);
void editorScroll(void);
void editorMoveCursor(int key);
void editorProcessKeypress(void);
void initEditor(void);
// stop the worker, then release rows, mmap and slabs
void editorCleanup(void);
// reread terminal size after SIGWINCH
void editorResize(void);

#endif