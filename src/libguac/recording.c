/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "guacamole/mem.h"
#include "guacamole/client.h"
#include "guacamole/protocol.h"
#include "guacamole/recording.h"
#include "guacamole/socket.h"
#include "guacamole/timestamp.h"

#ifdef __MINGW32__
#include <direct.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int guac_recording_open(const char* path,
        const char* name, char* basename, int basename_size) {

    /* Concatenate path and name (separated by a single slash) */
    int basename_length = snprintf(basename,
            basename_size - GUAC_COMMON_RECORDING_MAX_SUFFIX_LENGTH,
            "%s/%s", path, name);

    /* Abort if maximum length reached */
    if (basename_length ==
            basename_size - GUAC_COMMON_RECORDING_MAX_SUFFIX_LENGTH) {
        errno = ENAMETOOLONG;
        return -1;
    }

    /* Attempt to open recording */
    int fd = open(basename,
            O_CREAT | O_WRONLY,
            S_IRUSR | S_IWUSR | S_IRGRP);

    if (fd == -1) {
        return -1;
    }

/* Explicit file locks are required only on POSIX platforms */
#ifndef __MINGW32__
    /* Lock entire output file for writing by the current process */
    struct flock file_lock = {
        .l_type   = F_WRLCK,
        .l_whence = SEEK_SET,
        .l_start  = 0,
        .l_len    = 0,
        .l_pid    = getpid()
    };

    /* Abort if file cannot be locked for reading */
    if (fcntl(fd, F_SETLK, &file_lock) == -1) {
        close(fd);
        return -1;
    }
#endif

    return fd;

}

guac_recording* guac_recording_create(guac_client* client,
        const char* path, const char* name, int create_path,
        int include_output, int include_mouse, int include_touch,
        int include_keys) {

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
                "Creation of recording failed: %s", strerror(errno));
        return NULL;
    }

    /* Create recording structure with reference to underlying socket */
    guac_recording* recording = guac_mem_alloc(sizeof(guac_recording));
    recording->socket = guac_socket_open(fd);
    recording->include_output = include_output;
    recording->include_mouse = include_mouse;
    recording->include_touch = include_touch;
    recording->include_keys = include_keys;

    /* Replace client socket with wrapped recording socket only if including
     * output within the recording */
    if (include_output)
        client->socket = guac_socket_tee(client->socket, recording->socket);

    /* Recording creation succeeded */
    guac_client_log(client, GUAC_LOG_INFO,
            "Recording of session will be saved to \"%s\".",
            filename);

    return recording;

}

void guac_recording_free(guac_recording* recording) {

    /* If not including broadcast output, the output socket is not associated
     * with the client, and must be freed manually */
    if (!recording->include_output)
        guac_socket_free(recording->socket);

    /* Free recording itself */
    guac_mem_free(recording);

}

void guac_recording_report_mouse(guac_recording* recording,
        int x, int y, int button_mask) {

    /* Report mouse location only if recording should contain mouse events */
    if (recording->include_mouse)
        guac_protocol_send_mouse(recording->socket, x, y, button_mask,
                guac_timestamp_current());

}

void guac_recording_report_touch(guac_recording* recording,
        int id, int x, int y, int x_radius, int y_radius,
        double angle, double force) {

    /* Report touches only if recording should contain touch events */
    if (recording->include_touch)
        guac_protocol_send_touch(recording->socket, id, x, y,
                x_radius, y_radius, angle, force, guac_timestamp_current());

}

void guac_recording_report_key(guac_recording* recording,
        int keysym, int pressed) {

    /* Report key state only if recording should contain key events */
    if (recording->include_keys)
        guac_protocol_send_key(recording->socket, keysym, pressed,
                guac_timestamp_current());

}

