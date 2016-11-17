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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>

#include <linux/input.h>

#include "mlib.h"
#include "mcursor_cache.h"
#include "mlog.h"

#define BUF_SIZE (1 << 8)

/*
 * Empirically, these appear to be the max dims
 * that X returns for cursor images.
 */
#define CURSOR_WIDTH  (24)
#define CURSOR_HEIGHT (24)

/**
 * We use a custom error handler here for flexibility over the default handler
 * that just kills the process.
 *
 * This is especially useful when handling screen change events, because there
 * is a a potential race condition between receiving a damage event and a
 * screen change event. If the damage event is received before the screen
 * change event, XShmGetImage will throw BadMatch and kill the process--in
 * reality, this is just a transient error.
 */
int x_error_handler(Display *dpy, XErrorEvent *ev) {
    char error_text[BUF_SIZE];
    XGetErrorText(dpy, ev->error_code, error_text, BUF_SIZE);
    MLOGE("%s: request code %d\n",
            error_text,
            ev->request_code);
    return 0;
}

int copy_ximg_rows_to_buffer_mlocked(MBuffer *buf, XImage *ximg,
         uint32_t row_start, uint32_t row_end) {
    /* TODO ximg->xoffset? */
    uint32_t buf_bytes_per_line = buf->stride * 4;
    uint32_t ximg_bytes_per_pixel = ximg->bits_per_pixel / 8;
    uint32_t y;

    /* row-by-row copy to adjust for differing strides */
    uint32_t *buf_row, *ximg_row;
    for (y = row_start; y < row_end; ++y) {
        buf_row = buf->bits + (y * buf_bytes_per_line);
        ximg_row = (void *)ximg->data + (y * ximg->bytes_per_line);

        /*
         * we don't want to copy any extra XImage row padding
         * so we just copy up to image width instead of bytes_per_line
         */
        memcpy(buf_row, ximg_row, ximg->width * ximg_bytes_per_pixel);
    }

    return 0;
}

int copy_ximg_to_buffer_mlocked(MBuffer *buf, XImage *ximg) {
    return copy_ximg_rows_to_buffer_mlocked(buf, ximg, 0, ximg->height);
}

int copy_xcursor_to_buffer(MDisplay *mdpy, MBuffer *buf, XFixesCursorImage *cursor) {
    int err;
    err = MLockBuffer(mdpy, buf);
    if (err < 0) {
        MLOGE("MLockBuffer failed!\n");
        return -1;
    }

    /* clear out stale pixels */
    memset(buf->bits, 0, buf->height * buf->stride * 4);

    uint32_t cur_x, cur_y;  /* cursor relative coords */
    uint32_t x, y;          /* root window coords */
    for (cur_y = 0; cur_y < cursor->height; ++cur_y) {
        for (cur_x = 0; cur_x < cursor->width; ++cur_x) {
            x = cur_x;
            y = cur_y;

            /* bounds check! */
            if (y >= buf->height || x >= buf->width) {
                break;
            }

            uint8_t *pixel = (uint8_t *)cursor->pixels +
                4 * (cur_y * cursor->width + cur_x);

            /* copy only if opaque pixel */
            if (pixel[3] == 255) {
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

int render_root(Display *dpy, MDisplay *mdpy,
        MBuffer *buf, XImage *ximg) {
    int err;

    err = MLockBuffer(mdpy, buf);
    if (err < 0) {
        MLOGE("MLockBuffer failed!\n");
        return -1;
    }

    Status status;
    status = XShmGetImage(dpy,
        DefaultRootWindow(dpy),
        ximg,
        0, 0,
        AllPlanes);
    if(!status) {
        MLOGE("error calling XShmGetImage\n");
    }

    copy_ximg_to_buffer_mlocked(buf, ximg);

    err = MUnlockBuffer(mdpy, buf);
    if (err < 0) {
        MLOGE("MUnlockBuffer failed!\n");
        return -1;
    }

    return 0;
}

int update_cursor(Display *dpy,
        MDisplay *mdpy, MBuffer *cursor,
        int root_x, int root_y) {
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

struct cursor_thread_args {
    MDisplay *mdpy;
    MBuffer *cursor;
};

void *cursor_thread(void *targs) {
    struct cursor_thread_args *args = (struct cursor_thread_args *)targs;

    /* separate threads need separate client connections */
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        MLOGE("[ct] error calling XOpenDisplay\n");
        return (void *)-1;
    }

    /* check for XInputExtension (XI*) */
    int xi_opcode, event, error;
    if (!XQueryExtension(dpy, "XInputExtension",
            &xi_opcode, &event, &error)) {
        MLOGE("XInputExtension unavailable!\n");
        XCloseDisplay(dpy);
        return (void *)-1;
    }

    /* check for XI2 version */
    int ret;
    int major = XI_2_Major, minor = XI_2_Minor;
    ret = XIQueryVersion(dpy, &major, &minor);
    if (ret == BadRequest) {
        MLOGE("No matching XI2 support. (%d.%d only)\n", major, minor);
        XCloseDisplay(dpy);
        return (void *)-1;
    }

    /*
     * Sadly, the core X protocol is incapable of delivering
     * pointer motion events for the entire root window.
     * You can try to get around this by XSelectInput()
     * on all windows returned by XQueryTree() but root
     * window motion is still not included. XGrabPointer()
     * will deliver events but blocks all other clients,
     * i.e. none of your windows will respond to the cursor.
     *
     * Fortunately, the XInputExtension CAN deliver pointer
     * events cleanly for the whole root window! Yes!
     *
     * Previous to using XInputExtension I had to use a
     * poll(2) loop on /dev/input/event*...nasty.
     */

    /* Get the main pointer device id for XI2 requests */
    int pointer_dev_id;
    XIGetClientPointer(dpy, None, &pointer_dev_id);

    XIEventMask evmask;
    /*
     * The mask is set here in a very non-intuitive way
     * but is more robust for adding additional event masks.
     *
     * (1) check how many bytes you need with XIMaskLen and
     *     alloc an array of unsigned chars accordingly.
     */
    unsigned char mask[XIMaskLen(XI_Motion)] = { 0 };
    evmask.deviceid = pointer_dev_id;
    evmask.mask_len = sizeof(mask);
    evmask.mask = mask;
    /*
     * (2) use XISetMask macro to toggle the right event bits
     */
    XISetMask(mask, XI_Motion);
    XISelectEvents(dpy, DefaultRootWindow(dpy), &evmask, 1);

    XEvent ev;
    do {
        XNextEvent(dpy, &ev);
        /* see http://who-t.blogspot.com/2009/07/xi2-and-xlib-cookies.html */
        if (XGetEventData(dpy, &ev.xcookie)) {
            XGenericEventCookie *cookie = &ev.xcookie;
            if (cookie->extension == xi_opcode &&
                cookie->evtype == XI_Motion) {
                XIDeviceEvent *xiev = (XIDeviceEvent *)cookie->data;
                update_cursor(dpy, args->mdpy, args->cursor,
                    (int)xiev->root_x, (int)xiev->root_y);
            }
            XFreeEventData(dpy, cookie);
        } else {
            MLOGW("[ct] warning: unknown event %d\n", ev.type);
        }
    } while (1);

    XCloseDisplay(dpy);
    return NULL;
}

int cleanup_shm(const void *shmaddr, const int shmid) {
    if (shmdt(shmaddr) < 0) {
        MLOGE("error detaching shm: %s\n", strerror(errno));
        return -1;
    }

    if (shmctl(shmid, IPC_RMID, 0) < 0) {
        MLOGE("error destroying shm: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int xshm_cleanup(Display *dpy, XShmSegmentInfo *shminfo, XImage *ximg) {
    int err = 0;
    if (!XShmDetach(dpy, shminfo)) {
        MLOGE("error detaching shm from X server\n");
        err = -1;
    }
    XDestroyImage(ximg);

    /* try to clean up shm even if X fails to detach to avoid leaks */
    err |= cleanup_shm(shminfo->shmaddr, shminfo->shmid);

    return err;
}

XImage *xshm_init(Display *dpy, XShmSegmentInfo *shminfo, int screen) {
    /* create shared memory XImage structure */
    XImage *ximg = XShmCreateImage(dpy,
                    DefaultVisual(dpy, screen),
                    DefaultDepth(dpy, screen),
                    ZPixmap,
                    NULL,
                    shminfo,
                    XDisplayWidth(dpy, screen),
                    XDisplayHeight(dpy, screen));
    if (ximg == NULL) {
        MLOGE("error creating XShm Ximage\n");
        return NULL;
    }

    //
    // create a shared memory segment to store actual image data
    //
    shminfo->shmid = shmget(IPC_PRIVATE,
             ximg->bytes_per_line * ximg->height, IPC_CREAT|0777);
    if (shminfo->shmid < 0) {
        MLOGE("error creating shm segment: %s\n", strerror(errno));
        return NULL;
    }

    shminfo->shmaddr = ximg->data = shmat(shminfo->shmid, NULL, 0);
    if (shminfo->shmaddr < 0) {
        MLOGE("error attaching shm segment: %s\n", strerror(errno));
        cleanup_shm(shminfo->shmaddr, shminfo->shmid);
        return NULL;
    }

    shminfo->readOnly = False;

    //
    // inform server of shm
    //
    if (!XShmAttach(dpy, shminfo)) {
        MLOGE("error calling XShmAttach\n");
        cleanup_shm(shminfo->shmaddr, shminfo->shmid);
        return NULL;
    }

    return ximg;
}

/**
 * @return only valid as long as @param screenr is not freed
 */
static XRRModeInfo *x_find_matching_mode(Display *dpy,
        const XRRScreenResources *screenr,
        const uint32_t width, const uint32_t height)
{
    if (dpy == NULL || screenr == NULL) {
        return NULL;
    }

    int i;
    for (i = 0; i < screenr->nmode; ++i) {
        XRRModeInfo mode = screenr->modes[i];
        MLOGI("found supported mode: %dx%d\n", mode.width, mode.height);
        if (mode.width == width && mode.height == height) {
            return &screenr->modes[i];
        }
    }

    return NULL;
}

/**
 * Assumes only one crtc
 */
static int x_set_mode(Display *dpy,
        XRRScreenResources *screenr, const XRRModeInfo *mode)
{
    if (dpy == NULL || screenr == NULL || mode == NULL) {
        return -2;
    }

    int screen = DefaultScreen(dpy);

    /* keep Screen PPI constant */
    double ppi = (25.4 * XDisplayHeight(dpy, screen)) / XDisplayHeightMM(dpy, screen);
    int mwidth = (25.4 * mode->width) / ppi;
    int mheight = (25.4 * mode->height) / ppi;

    MLOGI("setting screen size to %dx%d %dmmx%dmm\n",
         mode->width, mode->height,
         mwidth, mheight);

    XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, screenr, screenr->crtcs[0]);

    if (XRRSetCrtcConfig(dpy, screenr, screenr->crtcs[0], CurrentTime,
            0, 0, mode->id, RR_Rotate_0,
            crtc->outputs, crtc->noutput) != RRSetConfigSuccess) {
        MLOGE("error setting crtc config\n");
        return -1;
    }

    XRRSetScreenSize(dpy, DefaultRootWindow(dpy),
         mode->width, mode->height,
         mwidth, mheight);

    return 0;
}

static int x_screenchangenotify_predicate(Display *dpy, XEvent *ev, XPointer arg) {
    int xrandr_event_base = (int)arg;
    return ev->type == xrandr_event_base + RRScreenChangeNotify;
}

/*
 * This must be called after x_set_mode to sync Xlib
 * up with the new screen changes.
 *
 * @note You must select for RRScreenChangeNotify on the root window
 * before calling this function.
 */
static int x_sync_mode(Display *dpy, XRRScreenResources *screenr,
        const XRRModeInfo *mode, const int xrandr_event_base)
{
    if (dpy == NULL || screenr == NULL || mode == NULL || xrandr_event_base < 0) {
        return -2;
    }

    XEvent ev;

    /*
     * I have experimentally observed some screen change events
     * being delivered on startup (perhaps due to the display manager?).
     * Try popping a few times in case we don't get our event at first.
     */
    int i;
    for (i = 0; i < 3; ++i) {
        MLOGI("waiting for ScreenChangeNotify events...\n");
        XIfEvent(dpy, &ev, x_screenchangenotify_predicate, (void *)xrandr_event_base);
        MLOGD("got event: %d\n", ev.type);
        if (ev.type == xrandr_event_base + RRScreenChangeNotify) {
            XRRScreenChangeNotifyEvent *screen_change = (XRRScreenChangeNotifyEvent *)&ev;
            MLOGI("[t=%lu]: screen size changed to %dx%d %dmmx%dmm\n",
                screen_change->timestamp,
                screen_change->width, screen_change->height,
                screen_change->mwidth, screen_change->mheight);

            if (screen_change->width == mode->width &&
                screen_change->height == mode->height) {
                /*
                 * Yes, this is our update! Let Xlib know that
                 * we need to update our local screen config.
                 */
                if (XRRUpdateConfiguration(&ev) == 0) {
                    MLOGE("error updating xrandr configuration\n");
                    return -1;
                }
                return 0;
            }
        }
    }

    return -1;
}

/**
 * Try to sync up MDisplay with XDisplay.
 */
static int sync_displays(Display *dpy, MDisplay *mdpy,
        const int xrandr_event_base)
{
    if (dpy == NULL || mdpy == NULL) {
        return -2;
    }

    uint32_t target_width, target_height;
    target_width = target_height = 0;

    /*
     * If we can get the real display size, set that as our target size.
     */
    MDisplayInfo dinfo = { 0 };
    if (MGetDisplayInfo(mdpy, &dinfo) <= 0) {
        MLOGW("failed to get mdisplay info, using current mode\n");
        return -1;
    }

    MLOGD("mwidth = %d, mheight = %d\n", dinfo.width, dinfo.height);
    int valid_display = dinfo.width > 0 && dinfo.height > 0;
    if (!valid_display) {
        MLOGW("invalid mdisplay size, using current mode\n");
        return -1;
    }

    target_width = dinfo.width;
    target_height = dinfo.height;

    /*
     * Re-sync before our service grab in case the screen
     * config has changed since we connected to the X server.
     *
     * This can be a problem on XFCE when xfsettingsd sets
     * the mode on startup based on a user's saved session.
     */
    XEvent ev;
    if (XCheckIfEvent(dpy, &ev,
            x_screenchangenotify_predicate,
            (void *)xrandr_event_base)) {
        if (XRRUpdateConfiguration(&ev) == 0) {
            MLOGE("error updating xrandr configuration\n");
        }
    }

    /*
     * Prevent any other client from changing the screen
     * config under our feet by "pausing" their X connections.
     *
     * NOTE: Any work with the screen configuration must come AFTER
     * this grab to ensure we are not using stale information!
     */
    XGrabServer(dpy);

    int err = -1;
    int screen = DefaultScreen(dpy);
    uint32_t xwidth = XDisplayWidth(dpy, screen);
    uint32_t xheight = XDisplayHeight(dpy, screen);

    int x_sync_needed = xwidth != target_width ||
                        xheight != target_height;
    if (x_sync_needed) {
        XRRScreenResources *screenr = XRRGetScreenResources(dpy,
                                        DefaultRootWindow(dpy));
        XRRModeInfo *matching_mode = x_find_matching_mode(dpy, screenr,
                                        target_width, target_height);
        if (matching_mode != NULL) {
            if (x_set_mode(dpy, screenr, matching_mode) == 0) {
                if (x_sync_mode(dpy, screenr, matching_mode, xrandr_event_base) == 0) {
                    /* success! */
                    err = 0;
                } else {
                    MLOGE("failed to sync mode with X\n");
                }
            } else {
                MLOGE("failed to set mode with X\n");
            }
        } else {
            MLOGW("couldn't find matching mode, using current mode\n");
        }

        XRRFreeScreenResources(screenr);
    }

    /*
     * We are done so let other clients be informed of
     * the screen changes and resume normal processing.
     */
    XUngrabServer(dpy);

    return err;
}

static int resize_shm(Display *dpy, XImage *ximg, XShmSegmentInfo *shminfo) {
    int screen = DefaultScreen(dpy);
    int xwidth = XDisplayWidth(dpy, screen);
    int xheight = XDisplayHeight(dpy, screen);
    int shm_resize_needed = ximg->width != xwidth ||
                            ximg->height != xheight;
    if (shm_resize_needed) {
        xshm_cleanup(dpy, shminfo, ximg);
        ximg = xshm_init(dpy, shminfo, screen);
    }

    return ximg == NULL ? -1 : 0;
}

static int resize_mbuffer(Display *dpy, MDisplay *mdpy, MBuffer *root) {
    int screen = DefaultScreen(dpy);
    int xwidth = XDisplayWidth(dpy, screen);
    int xheight = XDisplayHeight(dpy, screen);
    int buffer_resize_needed = root->width != xwidth ||
                               root->height != xheight;
    if (buffer_resize_needed) {
        if (MResizeBuffer(mdpy, root, xwidth, xheight) < 0) {
            return -1;
        }
    }

    return 0;
}

int main(void) {
    Display *dpy;
    MDisplay mdpy;
    int err = 0;

    /* TODO Ctrl-C handler to cleanup shm */

    /* must be first Xlib call for multi-threaded programs */
    if (!XInitThreads()) {
        MLOGE("error calling XInitThreads\n");
        return -1;
    }

    /* connect to the X server using the DISPLAY environment variable */
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        MLOGE("error calling XOpenDisplay\n");
        return -1;
    }

    //
    // check for necessary eXtensions
    //
    int xfixes_event_base, error;
    if (!XFixesQueryExtension(dpy, &xfixes_event_base, &error)) {
        MLOGE("Xfixes extension unavailable!\n");
        XCloseDisplay(dpy);
        return -1;
    }

    if (!XShmQueryExtension(dpy)) {
        MLOGE("XShm extension unavailable!\n");
        XCloseDisplay(dpy);
        return -1;
    }

    int xdamage_event_base;
    if (!XDamageQueryExtension(dpy, &xdamage_event_base, &error)) {
        MLOGE("XDamage extension unavailable!\n");
        XCloseDisplay(dpy);
        return -1;
    }

    int xrandr_event_base;
    if (!XRRQueryExtension(dpy, &xrandr_event_base, &error)) {
        MLOGE("Xrandr extension unavailable!\n");
        XCloseDisplay(dpy);
        return -1;
    }

    /* connect to maru display server */
    if (MOpenDisplay(&mdpy) < 0) {
        MLOGE("error calling MOpenDisplay\n");
        XCloseDisplay(dpy);
        return -1;
    }

    if (XSetErrorHandler(x_error_handler) < 0) {
        MLOGE("error setting error handler");
    }

    int screen = DefaultScreen(dpy);

    MLOGI("intial screen config: %dx%d %dmmx%dmm\n",
         XDisplayWidth(dpy, screen), XDisplayHeight(dpy, screen),
         XDisplayWidthMM(dpy, screen), XDisplayHeightMM(dpy, screen));

    XRRSelectInput(dpy, DefaultRootWindow(dpy), RRScreenChangeNotifyMask);
    if (sync_displays(dpy, &mdpy, xrandr_event_base) < 0) {
        MLOGW("couldn't sync resolution, using default mode\n");
    }

    //
    // Create necessary buffers
    //
    MBuffer root = { 0 };
    root.width = XDisplayWidth(dpy, screen);
    root.height = XDisplayHeight(dpy, screen);
    if (MCreateBuffer(&mdpy, &root) < 0) {
        MLOGE("error creating root buffer\n");
        err = -1;
        goto cleanup_1;
    }

    /* cursor buffer */
    XFixesCursorImage *xcursor = XFixesGetCursorImage(dpy);
    cursor_cache_add(xcursor);
    cursor_cache_set_cur(xcursor);

    MBuffer cursor;
    cursor.width = CURSOR_WIDTH;
    cursor.height = CURSOR_HEIGHT;
    if (MCreateBuffer(&mdpy, &cursor) < 0) {
        MLOGE("error creating cursor buffer\n");
        err = -1;
        goto cleanup_1;
    }

    /* render cursor sprite */
    if (copy_xcursor_to_buffer(&mdpy, &cursor, xcursor) < 0) {
        MLOGE("failed to render cursor sprite\n");
    }

    /* place the cursor at the right starting position */
    update_cursor(dpy, &mdpy, &cursor, xcursor->x, xcursor->y);

    //
    // set up XShm
    //
    XShmSegmentInfo shminfo;
    XImage *ximg = xshm_init(dpy, &shminfo, screen);
    if (ximg == NULL) {
        MLOGC("failed to create xshm\n");
        err = -1;
        goto cleanup_1;
    }

    //
    // register for X events
    //

    /* let me know when the cursor image changes */
    XFixesSelectCursorInput(dpy, DefaultRootWindow(dpy),
        XFixesDisplayCursorNotifyMask);

    /* report a single damage event if the damage region is non-empty */
    Damage damage = XDamageCreate(dpy, DefaultRootWindow(dpy),
        XDamageReportNonEmpty);


    //
    // spawn cursor thread
    //
    pthread_t cursor_pthread;
    struct cursor_thread_args args;
    args.mdpy = &mdpy;
    args.cursor = &cursor;
    pthread_create(&cursor_pthread, NULL,
        &cursor_thread, (void *)&args);

    //
    // event loop
    //
    XEvent ev;
    do {
        XNextEvent(dpy, &ev);
        if (ev.type == xdamage_event_base + XDamageNotify) {
            XDamageNotifyEvent *dmg = (XDamageNotifyEvent *)&ev;

            /*
             * clear out all the damage first so we
             * don't miss a DamageNotify while rendering
             */
            XDamageSubtract(dpy, dmg->damage, None, None);

            MLOGD("dmg>more = %d\n", dmg->more);
            MLOGD("dmg->area pos (%d, %d)\n", dmg->area.x, dmg->area.y);
            MLOGD("dmg->area dims %dx%d\n", dmg->area.width, dmg->area.height);

            /* TODO opt: only render damaged areas */
            render_root(dpy, &mdpy, &root, ximg);
        } else if (ev.type == xfixes_event_base + XFixesCursorNotify) {
            MLOGD("XFixesCursorNotifyEvent!\n");
            XFixesCursorNotifyEvent *cev = (XFixesCursorNotifyEvent *)&ev;
            MLOGD("cursor_serial: %lu\n", cev->cursor_serial);

            /* first, check if we have the new cursor in our cache... */
            XFixesCursorImage *xcursor = cursor_cache_get(cev->cursor_serial);

            /* ...if not, make the server request */
            if (xcursor == NULL) {
                xcursor = XFixesGetCursorImage(dpy);
                cursor_cache_add(xcursor);
            }

            /* render the new cursor */
            if (copy_xcursor_to_buffer(&mdpy, &cursor, xcursor) < 0) {
                MLOGE("failed to render cursor sprite\n");
            }

            cursor_cache_set_cur(xcursor);
        } else if (ev.type == xrandr_event_base + RRScreenChangeNotify) {
            /*
             * Someone changed the screen configuration.
             *
             * Common reasons:
             *
             * (1) xfsettingsd applies xrandr config on startup based
             * on the last setting selected in Settings > Display.
             *
             * (2) The user changed the display settings manually.
             */
            XRRScreenChangeNotifyEvent *rev = (XRRScreenChangeNotifyEvent *)&ev;
            MLOGW("[t=%lu] screen size changed to %dx%d %dmmx%dmm in main evloop\n",
                rev->timestamp,
                rev->width, rev->height,
                rev->mwidth, rev->mheight);

            if (XRRUpdateConfiguration(&ev) == 0) {
                MLOGE("error updating xrandr configuration\n");
            }

            /*
             * Attempt to sync XDisplay and MDisplay up again if possible.
             *
             * If we can determine the size of the real attached display, and
             * it doesn't match this change, it will be overriden to correctly
             * match. Otherwise, we just accept this change.
             */
            if (sync_displays(dpy, &mdpy, xrandr_event_base) < 0) {
                MLOGW("failed to sync X with mdisplay, re-configuring to match new size\n");
            }

            /*
             * Make sure our buffer sizes match up with the display size.
             */
            if (resize_shm(dpy, ximg, &shminfo) < 0) {
                MLOGC("failed to resize shm\n");
                break;
            }
            if (resize_mbuffer(dpy, &mdpy, &root) < 0) {
                MLOGC("failed to resize mbuffer\n");
                break;
            }
        }
    } while (1);


    XDamageDestroy(dpy, damage);
    xshm_cleanup(dpy, &shminfo, ximg);

cleanup_1:
    cursor_cache_free();
    MCloseDisplay(&mdpy);
    XCloseDisplay(dpy);

    return err;
}
