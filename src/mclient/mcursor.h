/*
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

#ifndef M_CURSOR_H
#define M_CURSOR_H

#include <pthread.h>
#include <X11/Xlib.h>
#include "mlib.h"

/*
 * Empirically, these appear to be the max dims
 * that X returns for cursor images.
 */
#define CURSOR_WIDTH  (24)
#define CURSOR_HEIGHT (24)

struct MCursor {
    Display *mXdpy;
    MDisplay *mMdpy;
    MBuffer mBuffer;
    pthread_t mMotionThread;
    int mXFixesEventBase;
};

int mcursor_init(struct MCursor *this, Display *xdpy, MDisplay *mdpy);
void mcursor_on_event(struct MCursor *this, XEvent *ev);

#endif // M_CURSOR_H
