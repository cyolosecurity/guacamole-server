#include <time.h>
#include <stdlib.h>
#include "guacamole/timestamp.h"
#include "guacamole/timestamp-types.h"


typedef struct asciicast_event {

    /*
    the timestamp of the event
    */
    float timestamp;

    /*
    the mode of the event
    */
   char mode;

   /*
   the data of the event
   */
   char* data;

} asciicast_event;

asciicast_event *asciicast_event_create(float timestamp, char mode, char *buffer, int bytes_read);
void free_asciicast_event(asciicast_event *e);

typedef struct slice {
   asciicast_event* *array;

   size_t size;

   size_t cap; 

} slice;

size_t slice_len(slice *s);
slice* append(slice *s, asciicast_event *e);
void free_slice(slice *s);

typedef struct asciicast_recording {

   guac_timestamp epoch;

   guac_timestamp timestamp;

   guac_timestamp seconds;

   char input_start;

   slice *input_events;

   slice *output_events;

   char* path;

} asciicast_recording;

asciicast_recording* asciicast_recording_create(char* path, char* name);
void free_asciicast_recording(asciicast_recording *rec);
void prepare_asciicast_file(asciicast_recording *rec);