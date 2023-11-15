#ifndef ASCIICAST_H
#define ASCIICAST_H

#include <time.h>
#include <stdlib.h>
#include <guacamole/client.h>
#include "guacamole/timestamp.h"
#include "guacamole/timestamp-types.h"
typedef struct asciicast_recording {

   guac_socket *socket;

   guac_timestamp epoch;

   guac_timestamp timestamp;

   char input_start;

   char *path;

   char *name;

} asciicast_recording;

asciicast_recording* asciicast_recording_create(char* path, char* name, int create_path, guac_client *client);
char* create_asciicast_header(asciicast_recording *rec);
char save_asciicast_file(asciicast_recording *rec, guac_client *client);
void free_asciicast_recording(asciicast_recording *rec);


char write_cast_event(float sec, const char* mode, const char* buffer, int bytes_read, guac_client* client, asciicast_recording* rec);
char *asciicast_event_to_json(float timestamp, const char* mode, const char* data);

#endif