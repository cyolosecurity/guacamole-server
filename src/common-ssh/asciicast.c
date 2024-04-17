#include "common-ssh/asciicast.h"
#include "common/cJSON.h"
#include "guacamole/recording.h"
#include "guacamole/socket.h"

#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/time.h>

//asciicast_event

char write_cast_event(float sec, const char* mode, const char* buffer, int bytes_read, guac_client* client, asciicast_recording* rec) {
    char data[bytes_read + 1];
    memcpy(data, buffer, bytes_read);
    data[bytes_read] = 0;

    /* Create asciicast event as json */
    char* event = asciicast_event_to_json(sec, mode, data);
    if (event == NULL) {
        guac_client_log(client, GUAC_LOG_ERROR,
             "Error creating json event for: %s", rec->name);
        return 0;
    }

    /* Make the asciicast event new line delimited */
    char event_line[strlen(event) + 2]; // +2 for \n and \0
     if (snprintf(event_line, sizeof(event_line), "%s\n", event) != strlen(event) + 1) {
         guac_client_log(client, GUAC_LOG_ERROR,
                "Error preparing event line for: %s", rec->name);
        free(event);
        return 0;
    }

    /* Write the event to the cast file */
    if (guac_socket_write(rec->socket, event_line, strlen(event_line)) != 0) {
        guac_client_log(client, GUAC_LOG_ERROR,
                "Error writing event for: %s", rec->name);
        free(event);
        return 0;
    }

    free(event);

    return 1;
}

char *asciicast_event_to_json(float timestamp, const char* mode, const char* data) {
    char *json = NULL;
    cJSON *arr = NULL;

    arr = cJSON_CreateArray();
    if (arr == NULL) {
        goto end;
    }

    cJSON *ts = cJSON_CreateNumber(timestamp);
    if (ts == NULL) {
        goto end;
    }
    cJSON_AddItemToArray(arr, ts);

    cJSON *m = cJSON_CreateString(mode);
    if (m == NULL) {
        goto end;
    }
    cJSON_AddItemToArray(arr, m);

    cJSON *d = cJSON_CreateString(data);
    if (d == NULL) {
        goto end;
    }
    cJSON_AddItemToArray(arr, d);

    json = cJSON_PrintUnformatted(arr);

    end:
        cJSON_Delete(arr);
        return json;
}

// asciicast_recording

asciicast_recording* asciicast_recording_create(char* path, char* name, int height, int width, int create_path, guac_client *client) {
    char filename[GUAC_COMMON_RECORDING_MAX_NAME_LENGTH];

    /* Create path if it does not exist, fail if impossible */
#ifndef __MINGW32__
    if (create_path && mkdir(path, S_IRWXU | S_IRGRP | S_IXGRP)
            && errno != EEXIST) {
#else
    if (create_path && _mkdir(path) && errno != EEXIST) {
#endif
        guac_client_log(client, GUAC_LOG_ERROR,
                "Creation of recording failed: %s", strerror(errno));
        return NULL;
    }

    /* Attempt to open recording file */
    int fd = guac_recording_open(path, name, filename, sizeof(filename));
    if (fd == -1) {
        guac_client_log(client, GUAC_LOG_ERROR,
                "Failed to open recording file '%s': %s", name, strerror(errno));
        return NULL;
    }

    asciicast_recording* rec = (asciicast_recording*)malloc(sizeof(asciicast_recording));

    rec->height = height;
    rec->width = width;
    rec->input_start = 0;
    
    rec->name = malloc(strlen(name) + 1);
    strcpy(rec->name, name);

    rec->path = malloc(strlen(path) + 1);
    strcpy(rec->path, path);
    
    rec->socket = guac_socket_open(fd);

    rec->epoch.tv_sec = 0;
    rec->epoch.tv_nsec = 0;

    /* Write header to the file */
    char* header = create_asciicast_header(rec);
    if (header == NULL) {
        guac_client_log(client, GUAC_LOG_ERROR,
                "Creation of asciicast header failed for: %s", name);
        free_asciicast_recording(rec);

        return NULL;
    }

    char header_line[strlen(header) + 2]; // +2 for \n and \0
    if (snprintf(header_line, sizeof(header_line), "%s\n", header) != strlen(header_line)) {
        guac_client_log(client, GUAC_LOG_ERROR,
                "Error preparing header line for: %s", name);
        free(header);
        free_asciicast_recording(rec);

        return NULL;
    }

    free(header);

    if (guac_socket_write(rec->socket, header_line, strlen(header_line)) != 0) {
        guac_client_log(client, GUAC_LOG_ERROR,
                "Error writing header line to cast file: %s", name);
        free_asciicast_recording(rec);

        return NULL;
    }

    /* Recording creation succeeded */
    guac_client_log(client, GUAC_LOG_INFO,
            "Recording of session will be saved to %s/%s" ,
            path, name);
    
    return rec;
}

char save_asciicast_file(asciicast_recording* rec, guac_client* client) {
    if (guac_socket_flush(rec->socket) != 0) {
        guac_client_log(client, GUAC_LOG_ERROR,
                "Error flushing cast file for: %s", rec->name);
        return 0;
    }
    
    return 1;
}

void free_asciicast_recording(asciicast_recording *rec) {
    rec->epoch.tv_sec = 0;
    rec->epoch.tv_nsec = 0;

    guac_socket_free(rec->socket);
    free(rec->name);
    free(rec->path);
    free(rec);
}

char *create_asciicast_header(asciicast_recording *rec) {
    char *json = NULL;
    cJSON *header = NULL;

    header = cJSON_CreateObject();
    if (header == NULL) {
        goto end;
    }

    cJSON *version = cJSON_CreateNumber(2);
    if (version == NULL) {
        goto end;
    }
    cJSON_AddItemToObject(header, "version", version);

    cJSON *width = cJSON_CreateNumber(rec->width);
    if (width == NULL) {
        goto end;
    }
    cJSON_AddItemToObject(header, "width", width);

    cJSON *height = cJSON_CreateNumber(rec->height);
    if (height == NULL) {
        goto end;
    }
    cJSON_AddItemToObject(header, "height", height);

    cJSON *timestamp = cJSON_CreateNumber(0);
    if (timestamp == NULL) {
        goto end;
    }
    cJSON_AddItemToObject(header, "timestamp", timestamp);

    cJSON *env = cJSON_CreateObject();
    if (env == NULL) {
        goto end;
    }
    cJSON_AddItemToObject(header, "env", env);


    cJSON *term = cJSON_CreateString("xterm-256color");
    if (term == NULL) {
        goto end;
    }
    cJSON_AddItemToObject(env, "TERM", term);

    json = cJSON_PrintUnformatted(header);

    end:
        cJSON_Delete(header);
        return json;
}