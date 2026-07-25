#include <stdlib.h>
#include <string.h>
#include "buffer.h"


void appendBufferReserve(APPEND_BUFFER *ab, int extra) {
    int need = ab->length + extra;
    if (need <= ab->capacity) {
        return;
    }

    int cap = ab->capacity ? ab->capacity : 256;
    while (cap < need) {
        cap *= 2;
    }

    char *new = realloc(ab->buffer, cap);
    if (new == NULL) {
        return;
    }

    ab->buffer = new;
    ab->capacity = cap;
}

void appendBufferAppend(APPEND_BUFFER *ab, const char *s, int len) {
    if (len <= 0) {
        return;
    }

    appendBufferReserve(ab, len);
    if (ab->length + len > ab->capacity) {
        return;  // realloc failed
    }

    memcpy(&ab->buffer[ab->length], s, len);
    ab->length += len;
}

void appendBufferFree(APPEND_BUFFER *ab) {
    free(ab->buffer);
    ab->buffer = NULL;
    ab->length = 0;
    ab->capacity = 0;
}
