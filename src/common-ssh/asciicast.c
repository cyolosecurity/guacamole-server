#include "common-ssh/asciicast.h"

#include <string.h>
#include <stdio.h>


//asciicast_event

asciicast_event *asciicast_event_create(float timestamp, char mode, char *buffer, int bytes_read) {
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

// asciicast_recording

asciicast_recording* asciicast_recording_create(char* path, char* name) {
    char* name_with_ext =  
    asciicast_event* rec = (asciicast_recording*)malloc(sizeof(asciicast_recording));
    
    return rec;
}

void free_asciicast_recording(asciicast_recording *rec) {
    free_slice(rec->output_events);
    free(rec->output_events);
    rec->output_events = NULL; 

    free_slice(rec->input_events);
    free(rec->input_events);
    rec->input_events = NULL;
}

// slice

size_t slice_len(slice *s) {
    return s->size;
}

slice* append(slice *s, asciicast_event *e) {
    if (s == NULL) {
        s = (slice*)malloc(sizeof(slice));
        s->array = malloc(2 * sizeof(asciicast_event*));
        s->size = 0;
        s->cap = 2;
    }

    if (s->size == s->cap) {
        s->cap *= 2;
        s->array = realloc(s->array, s->cap * sizeof(asciicast_event*));
    }

    s->array[s->size++] = e;

    return s;
}

void free_slice(slice *s) {
    for (int i = 0; i < s->size; i++) {
       free_asciicast_event(s->array[i]);
       free(s->array[i]);
       s->array[i] = NULL;
    }

    free(s->array);
    s->array = NULL;
    s->cap = s->size = 0;
}