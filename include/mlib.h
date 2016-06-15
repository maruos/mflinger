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
