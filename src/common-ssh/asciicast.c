#include "asciicast.h"

slice* append(slice *s, asciicast_event e) {
    if (s->cap == 0) {
        s->array = malloc(2 * sizeof(asciicast_event));
        s->size = 0;
        s->cap = 2;
    }

    if (s->size == s->cap) {
        s->cap *= 2;
        s->array = realloc(s->array, s->cap * sizeof(asciicast_event)); // TODO: check for errors
    }

    s->array[s->size++] = e;
}

void free_slice(slice *s) {
    free(s->array);
    s->array = NULL;
    s->cap = s->size = 0;
}