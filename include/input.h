#ifndef INPUT_H
#define INPUT_H


int editorReadKey(void);
char *editorPrompt(char *prompt, void (*callback)(char *, int));

#endif