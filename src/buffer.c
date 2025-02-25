#include <stdlib.h>
#include <string.h>
#include "buffer.h"


void appendBufferAppend(APPEND_BUFFER *ab, const char *s, int len) {
    char *new = realloc(ab->buffer, ab->length + len);
    if (new == NULL) return;
    
    memcpy(&new[ab->length], s, len);
    ab->buffer = new;
    ab->length += len;
}

void appendBufferFree(APPEND_BUFFER *ab) {
    free(ab->buffer);
}