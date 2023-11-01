#include <stdlib.h>

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

typedef struct slice {
   asciicast_event *array;
   size_t size;
   size_t cap; 
} slice;

slice* append(slice *s, asciicast_event e);
void free_slice(slice *s);

typedef struct asciicast_recording {
   char input_start;
   slice input_events;
   slice output_events;
} asciicast_recording;