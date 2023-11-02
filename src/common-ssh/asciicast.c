#include "asciicast.h"

asciicast_event *asciicast_event_create(time_t timestamp, char mode, char *buffer, int bytes_read) {
        asciicast_event *e = (asciicast_event*)malloc(sizeof(asciicast_event));
        e->timestamp = timestamp;
        e->mode = mode;
        e->data = malloc(bytes_read + 1);
        memcpy(e->data, buffer, bytes_read);
        e->data[bytes_read] = 0;

        return e;
}

void free_asciicast_event(asciicast_event *e) {
    free(e->data);
}

slice* append(slice *s, asciicast_event *e) {
    if (s->cap == 0) {
        s->array = malloc(2 * sizeof(asciicast_event*));
        s->size = 0;
        s->cap = 2;
    }

    if (s->size == s->cap) {
        s->cap *= 2;
        s->array = realloc(s->array, s->cap * sizeof(asciicast_event*)); // TODO: check for errors
    }

    s->array[s->size++] = e;
}

void free_slice(slice *s) {
    for (int i = 0; i < s->size; i++) {
       free_asciicast_event(s->array[i]);
    }

    free(s->array);
    s->array = NULL;
    s->cap = s->size = 0;
}

asciicast_recording* asciicast_recording_create() {
    return (asciicast_recording*)malloc(sizeof(asciicast_recording));
}

void free_asciicast_recording(asciicast_recording *rec) {
    free_slice(rec->output_events);
    free_slice(rec->input_events);
    rec->input_events = rec->output_events = NULL;
}
