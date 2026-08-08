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

// stop the worker, then release rows and the mapping, safe to call once
void editorCleanup(void);

// re-read terminal size after SIGWINCH
void editorResize(void);

#endif