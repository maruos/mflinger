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

#include <stdint.h>
#include <string.h>
#include <pthread.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XInput2.h>


#include "mcursor.h"
#include "mcursor_cache.h"
#include "mlog.h"
#include "util.h"

/*
 * All cursor-related logic belongs here.
 *
 * There is a dedicated thread created just to handle cursor motion events.
 * Empirically, this improves performance over a single thread processing
 * all events, especially when the mouse moves around a lot while there is
 * continuous damage to the screen (e.g. a video playing).
 *
 * Concurrency needs to be handled carefully here. Any calls to X on the motion
 * thread need to happen with a separate Display connection. Any calls to
 * MDisplay cannot wait for a response because this may call racing issues
 * with the main thread. In short, only MUpdateCursor can be called, no
 * MLock/MUnlock. That is why we keep the cursor image update handling on the
 * main thread.
 *
 * TODO: Ideally, all socket operations should be serialized to avoid races.
 *
 * NOTE: For some reason, moving XISelectEvents to the main thread causes no
 * motion events to be delivered unless XIAllDevices is used...no idea why.
 */

static int copy_xcursor_to_buffer(MDisplay *mdpy, MBuffer *buf,
        XFixesCursorImage *cursor)
{
    int err;
    err = MLockBuffer(mdpy, buf);
    if (err < 0) {
        MLOGE("MLockBuffer failed!\n");
        return -1;
    }

    /* clear out stale pixels */
    memset(buf->bits, 0, buf->height * buf->stride * 4);

    int x, y;
    for (y = 0; y < cursor->height; ++y) {
        for (x = 0; x < cursor->width; ++x) {
            /* bounds check! */
            if (y >= buf->height || x >= buf->width) {
                break;
            }

            int pixel_row_offset = y * cursor->width;
            int pixel_col_offset = x;
            unsigned long *pixel = cursor->pixels +
                pixel_row_offset + pixel_col_offset;

            /*
             * Copy only if opaque pixel to avoid weird artifacts.
             */
            if (argb8888_get_alpha(*pixel) == 255) {
                uint32_t *buf_pixel = buf->bits + (y * buf->stride + x) * 4;
                memcpy(buf_pixel, pixel, 4);
            }
        }
    }

    err = MUnlockBuffer(mdpy, buf);
    if (err < 0) {
        MLOGE("MUnlockBuffer failed!\n");
        return -1;
    }

    return 0;
}

static int update_cursor(Display *dpy,
        MDisplay *mdpy, MBuffer *cursor,
        int root_x, int root_y)
{
    int last_x, last_y;
    cursor_cache_get_last_pos(&last_x, &last_y);
    if (root_x != last_x || root_y != last_y) {
        XFixesCursorImage *xcursor = cursor_cache_get_cur();

        /* adjust so that hotspot is top-left */
        int32_t xpos = root_x - xcursor->xhot;
        int32_t ypos = root_y - xcursor->yhot;

        /* enforce lower bound or surfaceflinger freaks out */
        if (xpos < 0) {
            xpos = 0;
        }
        if (ypos < 0) {
            ypos = 0;
        }

        if (MUpdateBuffer(mdpy, cursor, xpos, ypos) < 0) {
            MLOGE("error calling MUpdateBuffer\n");
        }

        cursor_cache_set_last_pos(root_x, root_y);
    }

    return 0;
}

static void select_image_events(Display *xdpy) {
    /* let me know when the cursor image changes */
    XFixesSelectCursorInput(xdpy, DefaultRootWindow(xdpy),
        XFixesDisplayCursorNotifyMask);
}

/* Return 1 if XI2 is available, 0 otherwise */
static int has_xi2(Display *dpy)
{
    int major, minor;
    int rc;

    /* We support XI 2.2 */
    major = 2;
    minor = 2;

    rc = XIQueryVersion(dpy, &major, &minor);
    if (rc == BadRequest) 
     {
    	MLOGE("No XI2 support. Server supports version %d.%d only.\n", major, minor);
    	return 0;
    } 
 
    return 1;
}

static void select_events(Display *dpy, Window win)
{
    XIEventMask evmasks[1];
    unsigned char mask1[(XI_LASTEVENT + 7)/8];

    memset(mask1, 0, sizeof(mask1));

    /* Select for motion from the default cursor */
    XISetMask(mask1, XI_RawMotion);

    int pointer_dev_id;

    XIGetClientPointer(dpy, None, &pointer_dev_id);

    evmasks[0].deviceid = pointer_dev_id;
    evmasks[0].mask_len = sizeof(mask1);
    evmasks[0].mask = mask1;

    XISelectEvents(dpy, win, evmasks, 1);
    XFlush(dpy);
}

void *cursor_motion_thread(void *targs) {

    struct MCursor *this = (struct MCursor *)targs;
 
    Display *dpy;
    int xi_opcode, event, error;
    XEvent ev;

    dpy = XOpenDisplay(NULL);

    if (!dpy) {
    	MLOGE("Failed to open display.\n");
    	return (void *)-1;
    }

    if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event, &error)) {
       MLOGE("X Input extension not available.\n");
       return (void *)-1;
    }

    if (!has_xi2(dpy))
       return (void *)-1;

    /* select for XI2 events */
    select_events(dpy, DefaultRootWindow(dpy));

    while(1) 
    {
    	XGenericEventCookie *cookie = &ev.xcookie;
    	Window      	root_ret, child_ret;
    	int         	root_x, root_y;
    	int         	win_x, win_y;
    	unsigned int    mask;

    	XNextEvent(dpy, &ev);

    	if (cookie->type != GenericEvent ||cookie->extension != xi_opcode )
           continue;

        if (XGetEventData(dpy, cookie))
        {
    	    if (cookie->evtype == XI_RawMotion) 
            {
        	XQueryPointer(dpy, DefaultRootWindow(dpy),&root_ret, &child_ret, &root_x, &root_y, &win_x, &win_y, &mask);
                update_cursor(dpy, this->mMdpy, &this->mBuffer,root_x, root_y);
    	    }
            XFreeEventData(dpy, cookie);

        }

    	
    }
 
    XCloseDisplay(dpy);
    return NULL;
}

static void spawn_motion_thread(struct MCursor *this) {
    pthread_create(&this->mMotionThread, NULL,
        &cursor_motion_thread, (void *)this);
}

int mcursor_init(struct MCursor *this, Display *xdpy, MDisplay *mdpy) {
    this->mXdpy = xdpy;
    this->mMdpy = mdpy;

    int error;
    if (!XFixesQueryExtension(this->mXdpy, &this->mXFixesEventBase, &error)) {
        MLOGE("Xfixes extension unavailable!\n");
        return -1;
    }

    XFixesCursorImage *xcursor = XFixesGetCursorImage(this->mXdpy);
    cursor_cache_add(xcursor);
    cursor_cache_set_cur(xcursor);

    this->mBuffer.width = CURSOR_WIDTH;
    this->mBuffer.height = CURSOR_HEIGHT;
    if (MCreateBuffer(this->mMdpy, &this->mBuffer) < 0) {
        MLOGE("error creating cursor buffer\n");
        return -1;
    }

    if (copy_xcursor_to_buffer(this->mMdpy, &this->mBuffer, xcursor) < 0) {
        MLOGE("failed to render cursor sprite\n");
    }

    /* place the cursor at the right starting position */
    update_cursor(this->mXdpy, this->mMdpy, &this->mBuffer, xcursor->x, xcursor->y);

    select_image_events(this->mXdpy);

    spawn_motion_thread(this);

    return 0;
}

void mcursor_on_event(struct MCursor *this, XEvent *ev) {
    if (ev->type == this->mXFixesEventBase + XFixesCursorNotify) {
        MLOGD("XFixesCursorNotifyEvent!\n");
        XFixesCursorNotifyEvent *cev = (XFixesCursorNotifyEvent *)ev;
        MLOGD("cursor_serial: %lu\n", cev->cursor_serial);

        /* first, check if we have the new cursor in our cache... */
        XFixesCursorImage *xcursor = cursor_cache_get(cev->cursor_serial);

        /* ...if not, make the server request */
        if (xcursor == NULL) {
            xcursor = XFixesGetCursorImage(this->mXdpy);
            cursor_cache_add(xcursor);
        }

        /* render the new cursor */
        if (copy_xcursor_to_buffer(this->mMdpy, &this->mBuffer, xcursor) < 0) {
            MLOGE("failed to render cursor sprite\n");
        }

        cursor_cache_set_cur(xcursor);
    } else {
        MLOGW("unknown event %d\n", ev->type);
    }
}
