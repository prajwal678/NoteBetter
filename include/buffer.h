#ifndef BUFFER_H
#define BUFFER_H


typedef struct AppendBuffer {
    char *buffer;
    int length;
    int capacity;
} APPEND_BUFFER;

#define ABUF_INIT {NULL, 0, 0}

void appendBufferReserve(APPEND_BUFFER *ab, int extra);
void appendBufferAppend(APPEND_BUFFER *ab, const char *s, int len);
void appendBufferFree(APPEND_BUFFER *ab);

#endif