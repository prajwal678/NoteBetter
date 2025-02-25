#ifndef TERMINAL_H
#define TERMINAL_H


void die(const char *s);
void disableRawMode(void);
void enableRawMode(void);
int getWindowSize(int *rows, int *cols);
int getCursorPosition(int *rows, int *cols);

#endif