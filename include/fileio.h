#ifndef FILEIO_H
#define FILEIO_H


void editorOpen(char *filename);
char *editorRowsToString(int *buflen);
void editorSave(void);
void editorCloseFile(void);

#endif