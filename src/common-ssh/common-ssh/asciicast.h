#include <time.h>
#include <stdlib.h>


typedef struct asciicast_event {

    /*
    the timestamp of the event
    */
    time_t timestamp;

    /*
    the mode of the event
    */
   char mode;

   /*
   the data of the event
   */
   char* data;

} asciicast_event;

asciicast_event *asciicast_event_create(time_t timestamp, char mode, char *buffer, int bytes_read);
void free_asciicast_event(asciicast_event *e);

typedef struct slice {
   asciicast_event* *array;

   size_t size;

   size_t cap; 

} slice;

slice* append(slice *s, asciicast_event *e);
void free_slice(slice *s);

typedef struct asciicast_recording {
   char input_start;

   time_t epoch;

   time_t timestamp;

   time_t seconds;

   slice *input_events;

   slice *output_events;

} asciicast_recording;

asciicast_recording* asciicast_recording_create();
void free_asciicast_recording(asciicast_recording *rec);