#ifndef BUFFER_H
#define BUFFER_H

typedef struct AppendBuffer {
    char *buffer;
    int length;
} APPEND_BUFFER;

#define ABUF_INIT {NULL, 0}

void appendBufferAppend(APPEND_BUFFER *ab, const char *s, int len);
void appendBufferFree(APPEND_BUFFER *ab);

#endif