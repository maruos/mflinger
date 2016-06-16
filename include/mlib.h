/*
 * Copyright 2015-2016 Preetam J. D'Souza
 * Copyright 2016 The Maru OS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MLIB_H
#define MLIB_H

struct MDisplay {
    int sock_fd;        /* server socket */
};
typedef struct MDisplay MDisplay;

struct MDisplayInfo {
    uint32_t width;     /* width in px */
    uint32_t height;    /* height in px */
};
typedef struct MDisplayInfo MDisplayInfo;

struct MBuffer {
    uint32_t width;     /* width in px */
    uint32_t height;    /* height in px */
    uint32_t stride;    /* stride in px, may be >= width */
    void *bits;         /* raw buffer bytes in BGRA8888 format */

    int __fd;
    int32_t __id;
};
typedef struct MBuffer MBuffer;

int     MOpenDisplay    (MDisplay *dpy);
int     MCloseDisplay   (MDisplay *dpy);

int     MGetDisplayInfo (MDisplay *dpy, MDisplayInfo *dpy_info);

//
// Buffer management
//
int     MCreateBuffer   (MDisplay *dpy, MBuffer *buf);
int     MUpdateBuffer   (MDisplay *dpy, MBuffer *buf,
                         uint32_t xpos, uint32_t ypos);

//
// Buffer rendering
//
int     MLockBuffer     (MDisplay *dpy, MBuffer *buf);
int     MUnlockBuffer   (MDisplay *dpy, MBuffer *buf);

#endif // MLIB_H
