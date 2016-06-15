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

#define BUF_SIZE (1 << 8)

/*
 * Empirically, these appear to be the max dims
 * that X returns for cursor images.
 */
#define CURSOR_WIDTH  (24)
#define CURSOR_HEIGHT (24)

int copy_ximg_rows_to_buffer_mlocked(MBuffer *buf, XImage *ximg,
         uint32_t row_start, uint32_t row_end) {
    /* sanity checks */
    // if (buf->width < ximg->width ||
    //     buf->height < ximg->height) {
    //     fprintf(stderr, "output (%dx%d) insufficient for input (%dx%d)\n",
    //          buf->width, buf->height, ximg->width, ximg->height);
    //     return -1;
    // }

    /* TODO ximg->xoffset? */
    uint32_t buf_bytes_per_line = buf->stride * 4;
    uint32_t ximg_bytes_per_pixel = ximg->bits_per_pixel / 8;
    uint32_t y;
    // printf("[DEBUG] bytes pp = %d\n", bytes_per_pixel);
    // printf("[DEBUG] copying over XImage to Android buffer...\n");

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
    //printf("[DEBUG] painting cursor...\n");
/*    for (y = cursor->y; y < cursor->y + cursor->height; ++y) {
        memcpy(buf + (y * 1152 * 4) + (cursor->x * 4),
         cursor_buf + ((y - cursor->y) * cursor->width * 4),
         cursor->width * 4);
    }
*/

    // fprintf(stderr, "[DEBUG] cursor dims = %d x %d\n",
    //      cursor->width, cursor->height);

    int err;
    err = MLockBuffer(mdpy, buf);
    if (err < 0) {
        fprintf(stderr, "MLockBuffer failed!\n");
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

            // printf("[DEBUG] (%d, %d) -> (%d, %d, %d, %d)\n",
            //     cursor->x + x, cursor->y + y, pixel[0], pixel[1], pixel[2], pixel[3]);

            /* copy only if opaque pixel */
            if (pixel[3] == 255) {
                uint32_t *buf_pixel = buf->bits + (y * buf->stride + x) * 4;
                memcpy(buf_pixel, pixel, 4);
            }
        }
    }

    err = MUnlockBuffer(mdpy, buf);
    if (err < 0) {
        fprintf(stderr, "MUnlockBuffer failed!\n");
        return -1;
    }

    return 0;
}

int render_root(Display *dpy, MDisplay *mdpy,
        MBuffer *buf, XImage *ximg) {
    int err;

    err = MLockBuffer(mdpy, buf);
    if (err < 0) {
        fprintf(stderr, "MLockBuffer failed!\n");
        return -1;
    }

    // printf("[DEBUG] buf.width = %d\n", buf.width);
    // printf("[DEBUG] buf.height = %d\n", buf.height);
    // printf("[DEBUG] buf.stride = %d\n", buf.stride);
    // printf("[DEBUG] buf_fd = %d\n", buf_fd);

    Status status;
    status = XShmGetImage(dpy,
        DefaultRootWindow(dpy),
        ximg,
        0, 0,
        AllPlanes);
    if(!status) {
        fprintf(stderr, "error calling XShmGetImage\n");
    }

    copy_ximg_to_buffer_mlocked(buf, ximg);

    // fillBufferRGBA8888((uint8_t *)buf.bits, 0, 0, buf.width, buf.height, r, g, b);

    err = MUnlockBuffer(mdpy, buf);
    if (err < 0) {
        fprintf(stderr, "MUnlockBuffer failed!\n");
        return -1;
    }

    return 0;
}

int update_cursor(Display *dpy,
        MDisplay *mdpy, MBuffer *cursor,
        int root_x, int root_y) {
    // fprintf(stderr, "[DEBUG] cursor coords: (%d, %d)\n",
    //      root_x, root_y);

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
            fprintf(stderr, "error calling MUpdateBuffer\n");
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
        fprintf(stderr, "[ct] error calling XOpenDisplay\n");
        return (void *)-1;
    }

    /* check for XInputExtension (XI*) */
    int xi_opcode, event, error;
    if (!XQueryExtension(dpy, "XInputExtension",
            &xi_opcode, &event, &error)) {
        fprintf(stderr, "XInputExtension unavailable!\n");
        XCloseDisplay(dpy);
        return (void *)-1;
    }

    /* check for XI2 version */
    int ret;
    int major = XI_2_Major, minor = XI_2_Minor;
    ret = XIQueryVersion(dpy, &major, &minor);
    if (ret == BadRequest) {
        fprintf(stderr, "No matching XI2 support. (%d.%d only)\n",
            major, minor);
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
                // fprintf(stderr, "[DEBUG] cursor coords: (%d, %d)\n",
                //      (int)xiev->root_x, (int)xiev->root_y);
                update_cursor(dpy, args->mdpy, args->cursor,
                    (int)xiev->root_x, (int)xiev->root_y);
            }
            XFreeEventData(dpy, cookie);
        } else {
            fprintf(stderr, "[ct] warning: unknown event %d\n", ev.type);
        }
    } while (1);

    XCloseDisplay(dpy);
    return NULL;
}

int cleanup_shm(const void *shmaddr, const int shmid) {
    if (shmdt(shmaddr) < 0) {
        fprintf(stderr, "error detaching shm: %s\n", strerror(errno));
        return -1;
    }

    if (shmctl(shmid, IPC_RMID, 0) < 0) {
        fprintf(stderr, "error destroying shm: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

XImage *init_xshm(Display *dpy, XShmSegmentInfo *shminfo, int screen) {
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
        fprintf(stderr, "error creating XShm Ximage\n");
        return NULL;
    }

    //
    // create a shared memory segment to store actual image data
    //
    shminfo->shmid = shmget(IPC_PRIVATE,
             ximg->bytes_per_line * ximg->height, IPC_CREAT|0777);
    if (shminfo->shmid < 0) {
        fprintf(stderr, "error creating shm segment: %s\n", strerror(errno));
        return NULL;
    }

    shminfo->shmaddr = ximg->data = shmat(shminfo->shmid, NULL, 0);
    if (shminfo->shmaddr < 0) {
        fprintf(stderr, "error attaching shm segment: %s\n", strerror(errno));
        cleanup_shm(shminfo->shmaddr, shminfo->shmid);
        return NULL;
    }

    shminfo->readOnly = False;

    //
    // inform server of shm
    //
    if (!XShmAttach(dpy, shminfo)) {
        fprintf(stderr, "error calling XShmAttach\n");
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
        fprintf(stderr, "found supported mode: %dx%d\n", mode.width, mode.height);
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
    // fprintf(stderr, "PPI = %f\n", ppi);
    int mwidth = (25.4 * mode->width) / ppi;
    int mheight = (25.4 * mode->height) / ppi;

    fprintf(stderr, "info: setting screen size to %dx%d %dmmx%dmm\n",
         mode->width, mode->height,
         mwidth, mheight);

    XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, screenr, screenr->crtcs[0]);

    if (XRRSetCrtcConfig(dpy, screenr, screenr->crtcs[0], CurrentTime,
            0, 0, mode->id, RR_Rotate_0,
            crtc->outputs, crtc->noutput) != RRSetConfigSuccess) {
        fprintf(stderr, "error setting crtc config\n");
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
        fprintf(stderr, "waiting for ScreenChangeNotify events...\n");
        XIfEvent(dpy, &ev, x_screenchangenotify_predicate, (void *)xrandr_event_base);
        fprintf(stderr, "got event: %d\n", ev.type);
        if (ev.type == xrandr_event_base + RRScreenChangeNotify) {
            XRRScreenChangeNotifyEvent *screen_change = (XRRScreenChangeNotifyEvent *)&ev;
            fprintf(stderr, "[t=%lu]: screen size changed to %dx%d %dmmx%dmm\n",
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
                    fprintf(stderr, "error updating xrandr configuration\n");
                    return -1;
                }
                return 0;
            }
        }
    }

    return -1;
}

static int sync_resolution(Display *dpy, MDisplay *mdpy, const int xrandr_event_base) {
    if (dpy == NULL || mdpy == NULL) {
        return -2;
    }

    MDisplayInfo dinfo = { 0 };
    if (MGetDisplayInfo(mdpy, &dinfo) < 0) {
        fprintf(stderr, "warning: couldn't get mdisplay info, using default mode\n");
        return -1;
    }

    int valid_display = dinfo.width > 0 && dinfo.height > 0;
    fprintf(stderr, "[DEBUG] mwidth = %d, mheight = %d\n", dinfo.width, dinfo.height);
    if (!valid_display) {
        return -1;
    }

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
            fprintf(stderr, "error updating xrandr configuration\n");
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

    int screen = DefaultScreen(dpy);
    int sync_needed = XDisplayWidth(dpy, screen) != dinfo.width ||
                        XDisplayHeight(dpy, screen) != dinfo.height;
    if (sync_needed) {
        XRRScreenResources *screenr = XRRGetScreenResources(dpy, DefaultRootWindow(dpy));
        XRRModeInfo *matching_mode = x_find_matching_mode(dpy, screenr,
                                        dinfo.width, dinfo.height);
        if (matching_mode != NULL) {
            if (x_set_mode(dpy, screenr, matching_mode) == 0) {
                if (x_sync_mode(dpy, screenr, matching_mode, xrandr_event_base) < 0) {
                    fprintf(stderr, "error: failed to sync mode\n");
                }
            } else {
                fprintf(stderr, "error: failed to set mode\n");
            }
        } else {
            fprintf(stderr, "warning: couldn't find matching mode, using default mode\n");
        }

        XRRFreeScreenResources(screenr);
    }

    /*
     * We are done so let other clients be informed of
     * the screen changes and resume normal processing.
     */
    XUngrabServer(dpy);

    return 0;
}

int main(void) {
    Display *dpy;
    MDisplay mdpy;
    int err = 0;

    /* TODO Ctrl-C handler to cleanup shm */

    /* must be first Xlib call for multi-threaded programs */
    if (!XInitThreads()) {
        fprintf(stderr, "error calling XInitThreads\n");
        return -1;
    }

    /* connect to the X server using the DISPLAY environment variable */
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "error calling XOpenDisplay\n");
        return -1;
    }

    //
    // check for necessary eXtensions
    //
    int xfixes_event_base, error;
    if (!XFixesQueryExtension(dpy, &xfixes_event_base, &error)) {
        fprintf(stderr, "Xfixes extension unavailable!\n");
        XCloseDisplay(dpy);
        return -1;
    }

    if (!XShmQueryExtension(dpy)) {
        fprintf(stderr, "XShm extension unavailable!\n");
        XCloseDisplay(dpy);
        return -1;
    }

    int xdamage_event_base;
    if (!XDamageQueryExtension(dpy, &xdamage_event_base, &error)) {
        fprintf(stderr, "XDamage extension unavailable!\n");
        XCloseDisplay(dpy);
        return -1;
    }

    int xrandr_event_base;
    if (!XRRQueryExtension(dpy, &xrandr_event_base, &error)) {
        fprintf(stderr, "Xrandr extension unavailable!\n");
        XCloseDisplay(dpy);
        return -1;
    }

    /* connect to maru display server */
    if (MOpenDisplay(&mdpy) < 0) {
        fprintf(stderr, "error calling MOpenDisplay\n");
        XCloseDisplay(dpy);
        return -1;
    }

    int screen = DefaultScreen(dpy);

    fprintf(stderr, "intial screen config: %dx%d %dmmx%dmm\n",
         XDisplayWidth(dpy, screen), XDisplayHeight(dpy, screen),
         XDisplayWidthMM(dpy, screen), XDisplayHeightMM(dpy, screen));

    XRRSelectInput(dpy, DefaultRootWindow(dpy), RRScreenChangeNotifyMask);
    if (sync_resolution(dpy, &mdpy, xrandr_event_base) < 0) {
        fprintf(stderr, "warning: couldn't sync resolution, using default mode\n");
    }

    //
    // Create necessary buffers
    //
    MBuffer root = { 0 };
    root.width = XDisplayWidth(dpy, screen);
    root.height = XDisplayHeight(dpy, screen);
    if (MCreateBuffer(&mdpy, &root) < 0) {
        fprintf(stderr, "Error creating root buffer\n");
        err = -1;
        goto cleanup_1;
    }
    // printf("[DEBUG] root.__id = %d\n", root.__id);

    /* cursor buffer */
    XFixesCursorImage *xcursor = XFixesGetCursorImage(dpy);
    cursor_cache_add(xcursor);
    cursor_cache_set_cur(xcursor);

    MBuffer cursor;
    cursor.width = CURSOR_WIDTH;
    cursor.height = CURSOR_HEIGHT;
    if (MCreateBuffer(&mdpy, &cursor) < 0) {
        fprintf(stderr, "Error creating cursor buffer\n");
        err = -1;
        goto cleanup_1;
    }
    // printf("[DEBUG] cursor.__id = %d\n", cursor.__id);

    /* render cursor sprite */
    if (copy_xcursor_to_buffer(&mdpy, &cursor, xcursor) < 0) {
        fprintf(stderr, "failed to render cursor sprite\n");
    }

    /* place the cursor at the right starting position */
    update_cursor(dpy, &mdpy, &cursor, xcursor->x, xcursor->y);

    //
    // set up XShm
    //
    XShmSegmentInfo shminfo;
    XImage *ximg = init_xshm(dpy, &shminfo, screen);

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

            // fprintf(stderr, "[DEBUG] dmg>more = %d\n", dmg->more);
            // fprintf(stderr, "[DEBUG] dmg->area pos (%d, %d)\n",
            //      dmg->area.x, dmg->area.y);
            // fprintf(stderr, "[DEBUG] dmg->area dims %dx%d\n", 
            //      dmg->area.width, dmg->area.height);

            /* TODO opt: only render damaged areas */
            render_root(dpy, &mdpy, &root, ximg);
        } else if (ev.type == xfixes_event_base + XFixesCursorNotify) {
            // fprintf(stderr, "[DEBUG] XFixesCursorNotifyEvent!\n");
            XFixesCursorNotifyEvent *cev = (XFixesCursorNotifyEvent *)&ev;
            // fprintf(stderr, "[DEBUG] cursor_serial: %lu\n", cev->cursor_serial);

            /* first, check if we have the new cursor in our cache... */
            XFixesCursorImage *xcursor = cursor_cache_get(cev->cursor_serial);

            /* ...if not, make the server request */
            if (xcursor == NULL) {
                xcursor = XFixesGetCursorImage(dpy);
                cursor_cache_add(xcursor);
            }

            /* render the new cursor */
            if (copy_xcursor_to_buffer(&mdpy, &cursor, xcursor) < 0) {
                fprintf(stderr, "failed to render cursor sprite\n");
            }

            cursor_cache_set_cur(xcursor);
        } else if (ev.type == xrandr_event_base + RRScreenChangeNotify) {
            /*
             * xfsettingsd applies xrandr config on startup based
             * on the last setting selected in Settings > Display.
             */
            XRRScreenChangeNotifyEvent *rev = (XRRScreenChangeNotifyEvent *)&ev;
            fprintf(stderr, "warning: [t=%lu] screen size changed to %dx%d %dmmx%dmm in main evloop\n",
                rev->timestamp,
                rev->width, rev->height,
                rev->mwidth, rev->mheight);

            if (XRRUpdateConfiguration(&ev) == 0) {
                fprintf(stderr, "error updating xrandr configuration\n");
            }

            if (sync_resolution(dpy, &mdpy, xrandr_event_base) < 0) {
                fprintf(stderr, "warning: couldn't re-sync resolution\n");
            }
        }
    } while (1);


    XDamageDestroy(dpy, damage);

    if (!XShmDetach(dpy, &shminfo)) {
        fprintf(stderr, "error detaching shm from X server\n");
    }
    XDestroyImage(ximg);
    cleanup_shm(shminfo.shmaddr, shminfo.shmid);

cleanup_1:
    cursor_cache_free();
    MCloseDisplay(&mdpy);
    XCloseDisplay(dpy);

    return err;
}
