#include "common-ssh/asciicast.h"
#include "common/cJSON.h"

#include <string.h>
#include <stdio.h>
#include <fcntl.h>

//asciicast_event

asciicast_event *asciicast_event_create(float timestamp, char* mode, char *buffer, int bytes_read) {
        asciicast_event *e = (asciicast_event*)malloc(sizeof(asciicast_event));
        e->timestamp = timestamp;

        e->mode = malloc(strlen(mode) + 1);
        memcpy(e->mode, mode, strlen(mode));
        e->data[strlen(mode)] = 0;

        e->data = malloc(bytes_read + 1);
        memcpy(e->data, buffer, bytes_read);
        e->data[bytes_read] = 0;

        return e;
}

char *asciicast_event_to_json(asciicast_event *e) {
    char *json = NULL;
    cJSON *arr = NULL;

    cJSON *timestamp = cJSON_CreateNumber(e->timestamp);
    if (timestamp == NULL) {
        goto end;
    }

    cJSON *mode = cJSON_CreateString(e->mode);
    if (mode == NULL) {
        goto end;
    }

    cJSON *data = cJSON_CreateString(e->data);
    if (data == NULL) {
        goto end;
    }

    arr = cJSON_CreateArray();
    if (arr == NULL) {
        goto end;
    }

    cJSON_AddItemToArray(arr, timestamp);
    cJSON_AddItemToArray(arr, mode);
    cJSON_AddItemToArray(arr, data);

    json = cJSON_Print(arr);

    end:
        cJSON_Delete(arr);
        return json;
}

void free_asciicast_event(asciicast_event *e) {
    free(e->mode);
    free(e->data);
    free(e);
}

// asciicast_recording

asciicast_recording* asciicast_recording_create(char* path, char* name) {
    asciicast_recording* rec = (asciicast_recording*)malloc(sizeof(asciicast_recording));

    rec->name = malloc(strlen(name) + 1);
    strcpy(rec->name, name);

    rec->path = malloc(strlen(path) + 1);
    strcpy(rec->path, path);
    
    return rec;
}

// {"version":2,"width":103,"height":25,"timestamp":1699794805,"env":{"TERM":"xterm-256color"}}
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

    cJSON *width = cJSON_CreateNumber(103);
    if (width == NULL) {
        goto end;
    }

    cJSON *height = cJSON_CreateNumber(25);
    if (height == NULL) {
        goto end;
    }

    cJSON *duration = cJSON_CreateNumber(rec->duration);
    if (duration == NULL) {
        goto end;
    }

    cJSON *timestamp = cJSON_CreateNumber(rec->epoch);
    if (timestamp == NULL) {
        goto end;
    }

    cJSON *term = cJSON_CreateString("xterm-256color");
    if (term == NULL) {
        goto end;
    }

    cJSON *env = cJSON_CreateObject();
    if (env == NULL) {
        goto end;
    }

    cJSON_AddItemToObject(env, "TERM", term);
    cJSON_AddItemToObject(header, "version", version);
    cJSON_AddItemToObject(header, "width", width);
    cJSON_AddItemToObject(header, "height", height);
    cJSON_AddItemToObject(header, "duration", duration);
    cJSON_AddItemToObject(header, "timestamp", timestamp);
    cJSON_AddItemToObject(header, "env", env);

    json = cJSON_Print(header);

    end:
        cJSON_Delete(header);
        return json;
}

char save_asciicast_file(asciicast_recording *rec) {
    // open new .cast.tmp file
    char* ext_tmp = ".cast.tmp";
    char tmp_name[strlen(rec->path) + strlen(rec->name) + strlen(ext_tmp) + 1];

    snprintf(tmp_name, sizeof(tmp_name), "%s/%s.%s", rec->path, rec->name, ext_tmp);

    int fd = open(tmp_name, O_CREAT | O_WRONLY | O_APPEND); 
    if (fd ==  -1) {
        return 0;
    }

    // create asciicast header
    char *header = create_asciicast_header(rec);
    if (header == NULL) {
        return 0;
    }

    char header_line[strlen(header) + 2]; // +2 for \n delimiter and a null terminator
    snprintf(header_line, sizeof(header_line), "%s\n\0", header);
    free(header);

    if (write(fd, header_line, strlen(header_line)) < 0) {
        close(fd);
        return 0;
    }


    // merge i/o events and save to file
    int i = 0;
    int j = 0;

    while (i < slice_len(rec->input_events) && j < slice_len(rec->output_events)) {
        asciicast_event *e;
        char *json_event;

        if (get_item(rec->input_events, i)->timestamp <= get_item(rec->output_events, j)->timestamp) {
            e = get_item(rec->input_events, i);
            i++;
        } else {
            e = get_item(rec->output_events, j);
            j++;
        }

        json_event = asciicast_event_to_json(e);
        if (json_event == NULL) {
            close(fd);
            return 0;
        }

        char event_line[strlen(json_event) + 2];
        snprintf(event_line, sizeof(event_line), "%s\n\0", json_event);
        free(json_event);

        if (write(fd, event_line, strlen(event_line)) < 0) {
            close(fd);
            return 0;
        }
     }

     while (i < slice_len(rec->input_events)) {
       char *json_event;
       json_event = asciicast_event_to_json(get_item(rec->input_events, i));
       if (json_event == NULL) {
            close(fd);
            return 0;
        }
        
        char event_line[strlen(json_event) + 2];
        snprintf(event_line, sizeof(event_line), "%s\n\0", json_event);
        free(json_event);

        if (write(fd, event_line, strlen(event_line)) < 0) {
            close(fd);
            return 0;
        }

       free(json_event);
       i++;
     }

    while (j < slice_len(rec->output_events)) {
        char *json_event;
        json_event = asciicast_event_to_json(get_item(rec->output_events, j));
        if (json_event == NULL) {
            close(fd);
            return 0;
        }
       
        char event_line[strlen(json_event) + 2];
        snprintf(event_line, sizeof(event_line), "%s\n\0", json_event);
        free(json_event);

        if (write(fd, event_line, strlen(event_line)) < 0) {
            close(fd);
            return 0;
        }

       j++;
     }

    // change ext to .cast to indicate a proper dump
    char* ext = ".cast";
    char file_name[strlen(rec->path) + strlen(rec->name) + strlen(ext) + 1];

    if (rename(tmp_name, file_name) < 0) {
        close(fd);
        return 0;
    }

    close(fd);
    return 1;
}

void free_asciicast_recording(asciicast_recording *rec) {
    free_slice(rec->output_events);
    rec->output_events = NULL; 

    free_slice(rec->input_events);
    rec->input_events = NULL;

    free(rec->name);
    free(rec->path);

    free(rec);
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

asciicast_event* get_item(slice *s, int i) {
    return s->array[i];
}

void free_slice(slice *s) {
    for (int i = 0; i < s->size; i++) {
       free_asciicast_event(s->array[i]);
       s->array[i] = NULL;
    }

    free(s->array);
    s->array = NULL;
    s->cap = s->size = 0;

    free(s);
}