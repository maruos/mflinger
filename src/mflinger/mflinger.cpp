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

#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>

#include <binder/IBinder.h>
#include <ui/DisplayInfo.h>
#include <ui/Rect.h>
#include <gui/Surface.h>
#include <gui/ISurfaceComposer.h>
// #include <gui/ISurfaceComposerClient.h> createSurface() flags
#include <gui/SurfaceComposerClient.h>

#include <android/native_window.h> // ANativeWindow_Buffer full def

#include <cutils/log.h>
#include <utils/Errors.h>

#include "mlib.h"
#include "mlib-protocol.h"

#define DEBUG (0)

using namespace android;

/*
 * There is no clean way to get the current layer stack of
 * a display, so we have to use hardcoded Android constants here.
 *
 * On the Android side, DisplayManagerService is the sole entity
 * that assigns layerstacks to displays. Current policy is that
 * display IDs themselves are the layerstack values.
 *
 * To have a stable layerstack for maru, we have DMS reserve
 * the display ID android.view.Display.DEFAULT_EXTERNAL_DISPLAY.
 *
 * These must match android.view.Display.DEFAULT_DISPLAY and
 * android.view.Display.DEFAULT_EXTERNAL_DISPLAY!
 */
static const int DEFAULT_DISPLAY = 0;
static const int DEFAULT_EXTERNAL_DISPLAY = 1;

/*
 * Currently we only support a single client with
 * two surfaces that are usually:
 *      1. root window surface
 *      2. cursor sprite surface
 */
static const int MAX_SURFACES = 2;

struct mflinger_state {
    sp<SurfaceComposerClient> compositor;       /* SurfaceFlinger connection */
    sp<SurfaceControl> surfaces[MAX_SURFACES];  /* surfaces alloc'd for clients */
    int num_surfaces;                           /* num of surfaces currently managed */
    int layerstack;                             /* selects display for surfaces */
};

static int32_t buffer_id_to_index(int32_t id) {
    return id - 1;
}

static int is_valid_idx(struct mflinger_state *state, int32_t idx) {
    return (0 <= idx && idx < state->num_surfaces);
}

static int32_t get_layer(int32_t surface_idx) {
    /*
     * Assign some really large number to make
     * sure maru surfaces are the topmost layers.
     *
     * This is useful for debugging and showing on
     * the default display over Android layers.
     */
    return 0x7ffffff0 + surface_idx;
}

static int assign_layerstack() {
    return DEFAULT_EXTERNAL_DISPLAY;
}

static int getDisplayInfo(const int sockfd) {
    /* no request args */

    DisplayInfo dinfo_ext;
    status_t check;

    /* undefined display marker */
    dinfo_ext.w = dinfo_ext.h = 0;

    sp<IBinder> dpy_ext = SurfaceComposerClient::getBuiltInDisplay(
            ISurfaceComposer::eDisplayIdHdmi);
    check = SurfaceComposerClient::getDisplayInfo(dpy_ext, &dinfo_ext);
    if (NO_ERROR != check) {
        ALOGW("getDisplayInfo() for eDisplayIdHdmi failed!");
    }

    ALOGD_IF(DEBUG, "HDMI DisplayInfo dump");
    ALOGD_IF(DEBUG, "     display w x h = %d x %d", dinfo_ext.w, dinfo_ext.h);
    ALOGD_IF(DEBUG, "     display orientation = %d", dinfo_ext.orientation);

    MGetDisplayInfoResponse response;
    response.width = dinfo_ext.w;
    response.height = dinfo_ext.h;

    if (write(sockfd, &response, sizeof(response)) < 0) {
        ALOGE("[getDisplayInfo] Failed to write response: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static int createSurface(struct mflinger_state *state,
            uint32_t w, uint32_t h) {

    if (state->num_surfaces >= MAX_SURFACES) {
        return -1;
    }

    /* lazy init the layerstack when the first surface is created */
    if (state->layerstack < 0) {
        state->layerstack = assign_layerstack();
    }

    String8 name = String8::format("maru %d", state->num_surfaces);
    sp<SurfaceControl> surface = state->compositor->createSurface(
                                name,
                                w, h,
                                PIXEL_FORMAT_BGRA_8888,
                                0);
    if (surface == NULL || !surface->isValid()) {
        ALOGE("compositor->createSurface() failed!");
        return -1;
    }

    //
    // Display the surface on the screen
    //
    status_t ret = NO_ERROR;
    SurfaceComposerClient::openGlobalTransaction();

    ret |= surface->setLayer(get_layer(state->num_surfaces));
    ret |= surface->setLayerStack(state->layerstack);
    ret |= surface->show();

    SurfaceComposerClient::closeGlobalTransaction(true);

    if (NO_ERROR != ret) {
        ALOGE("compositor transaction failed!");
        return -1;
    }

    state->surfaces[(state->num_surfaces)++] = surface;

    return 0;
}

static int createBuffer(const int sockfd, struct mflinger_state *state) {
    int n;
    MCreateBufferRequest request;
    n = read(sockfd, &request, sizeof(request));
    ALOGD_IF(DEBUG, "[C] n: %d", n);
    ALOGD_IF(DEBUG, "[C] requested dims = (%lux%lu)", 
        (unsigned long)request.width, (unsigned long)request.height);

    ALOGD_IF(DEBUG, "[C] 1 -- num_surfaces = %d", state->num_surfaces);

    n = createSurface(state,
         request.width, request.height);

    ALOGD_IF(DEBUG, "[C] 2 -- num_surfaces = %d", state->num_surfaces);

    MCreateBufferResponse response;
    response.id = n ? -1 : state->num_surfaces;
    response.result = n ? -1 : 0;

    if (write(sockfd, &response, sizeof(response)) < 0) {
        ALOGE("[C] Failed to write response: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static int updateBuffer(const int sockfd, struct mflinger_state *state) {
    int n;
    MUpdateBufferRequest request;
    n = read(sockfd, &request, sizeof(request));
    ALOGD_IF(DEBUG, "[updateBuffer] n: %d", n);
    ALOGD_IF(DEBUG, "[updateBuffer] requested id = %d", request.id);
    ALOGD_IF(DEBUG, "[updateBuffer] requested pos = (%d, %d)",
        request.xpos, request.ypos);

    int32_t idx = buffer_id_to_index(request.id);
    if (!is_valid_idx(state, idx)) {
        ALOGW("ignoring update request for invalid surface id: %d\n", idx);
        return -1;
    }

    sp<SurfaceControl> sc = state->surfaces[idx];

    status_t ret = NO_ERROR;
    SurfaceComposerClient::openGlobalTransaction();
    ret |= sc->setPosition(request.xpos, request.ypos);
    SurfaceComposerClient::closeGlobalTransaction();

    if (NO_ERROR != ret) {
        ALOGE("compositor transaction failed!");
        return -1;
    }

    return 0;
}

static int resizeBuffer(const int sockfd, struct mflinger_state *state) {
    int n;
    MResizeBufferRequest request;
    n = read(sockfd, &request, sizeof(request));
    ALOGD_IF(DEBUG, "[resizeBuffer] requested width = %d", request.width);
    ALOGD_IF(DEBUG, "[resizeBuffer] requested height = %d", request.height);

    int32_t idx = buffer_id_to_index(request.id);
    if (!is_valid_idx(state, idx)) {
        ALOGW("ignoring resize request for invalid surface id: %d\n", idx);
        return -1;
    }

    sp<SurfaceControl> sc = state->surfaces[idx];

    status_t ret = NO_ERROR;
    SurfaceComposerClient::openGlobalTransaction();
    ret |= sc->setSize(request.width, request.height);
    SurfaceComposerClient::closeGlobalTransaction();

    MResizeBufferResponse response;
    response.result = 0;
    if (NO_ERROR != ret) {
        ALOGE("compositor resize transaction failed!");
        response.result = -1;
    }

    if (write(sockfd, &response, sizeof(response)) < 0) {
        ALOGE("Failed to write resizeBuffer response: %s",
                strerror(errno));
        return -1;
    }

    return 0;
}

static int sendfd(const int sockfd,
            void *data, const int data_len,
            const int fd) {
    struct msghdr msg = {0}; // 0 initializer
    struct cmsghdr *cmsg;
    //int bufferFd = 1;
    union {
        char buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } u;
    int *fdptr;

    /* 
     * >= 1 byte of nonacillary data must be sent
     * in the same sendmsg() call to pass fds
     */
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = data_len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof(u.buf);
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));

    fdptr = (int *) CMSG_DATA(cmsg);
    memcpy(fdptr, &fd, sizeof(int));

    if (sendmsg(sockfd, &msg, 0) < 0) {
        ALOGE("Failed to sendmsg: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static int lockBuffer(const int sockfd, struct mflinger_state *state) {
    int n;
    MLockBufferRequest request;
    n = read(sockfd, &request, sizeof(request));
    ALOGD_IF(DEBUG, "[L] n: %d", n);
    ALOGD_IF(DEBUG, "[L] requested id = %d", request.id);
    int32_t idx = buffer_id_to_index(request.id);

    MLockBufferResponse response;
    response.result = -1;

    if (0 <= idx && idx < state->num_surfaces) {
        sp<SurfaceControl> sc = state->surfaces[idx];
        sp<Surface> s = sc->getSurface();

        ANativeWindow_Buffer outBuffer;
        buffer_handle_t handle;
        status_t err = s->lockWithHandle(&outBuffer, &handle, NULL);
        if (err != 0) {
            ALOGE("failed to lock buffer");
        } else if (handle->numFds < 1) {
            ALOGE("buffer handle does not have any fds");
        } else {
            /* all is well */
            response.buffer.width = outBuffer.width;
            response.buffer.height = outBuffer.height;
            response.buffer.stride = outBuffer.stride;
            response.buffer.bits = NULL;
            response.result = 0;

            return sendfd(sockfd, (void *)&response,
                sizeof(response), handle->data[0]);
        }
    } else {
        ALOGE("Invalid buffer id: %d\n", request.id);
    }    

    if (write(sockfd, &response, sizeof(response)) < 0) {
        ALOGE("[L] Failed to write response: %s", strerror(errno));
    }
    return -1;
}

static int unlockAndPostBuffer(const int sockfd,
            struct mflinger_state *state) {
    int n;
    MUnlockBufferRequest request;
    n = read(sockfd, &request, sizeof(request));
    ALOGD_IF(DEBUG, "[U] n: %d", n);
    ALOGD_IF(DEBUG, "[U] requested id = %d", request.id);
    int32_t idx = buffer_id_to_index(request.id);

    if (0 <= idx && idx < state->num_surfaces) {
        sp<SurfaceControl> sc = state->surfaces[idx];
        sp<Surface> s = sc->getSurface();

        return s->unlockAndPost();
    } else {
        ALOGE("Invalid buffer id: %d\n", request.id);
    }

    /* TODO return failure to client? */

    return -1;
}

static void purge_surfaces(struct mflinger_state *state) {
    for (; state->num_surfaces > 0; --state->num_surfaces) {
        /*
         * these are strong pointers so setting them
         * to NULL will trigger dtor()
         */
        state->surfaces[state->num_surfaces - 1] = NULL;
    }
}

static void reset_state(struct mflinger_state *state) {
    purge_surfaces(state);

    /* look for new displays for the next client */
    state->layerstack = -1;
}

static void serve(const int sockfd, struct mflinger_state *state) {
    int cfd;
    socklen_t t;
    struct sockaddr_un remote;

    ALOGD_IF(DEBUG, "Listening for client requests...");

    t = sizeof(remote);
    cfd = accept(sockfd, (struct sockaddr *)&remote, &t);
    if (cfd < 0) {
        ALOGE("Failed to accept client: %s", strerror(errno));
        return;
    }

    do {
        int n;
        uint32_t buf;
        n = read(cfd, &buf, sizeof(buf));

        if (n < 0) {
            ALOGE("Failed to read from socket: %s", strerror(errno));

            // client hung up with unread data
            if (errno == -ECONNRESET) {
                break;
            }
        } else if (n == 0) {
            ALOGE("Client closed connection.");
            break;
        }

        ALOGD_IF(DEBUG, "n: %d", n);
        ALOGD_IF(DEBUG, "buf: %d", buf);
        switch (buf) {
            case M_GET_DISPLAY_INFO:
                ALOGD_IF(DEBUG, "Get display info request!");
                getDisplayInfo(cfd);
                break;

            case M_CREATE_BUFFER:
                ALOGD_IF(DEBUG, "Create buffer request!");
                createBuffer(cfd, state);
                break;

            case M_UPDATE_BUFFER:
                ALOGD_IF(DEBUG, "Update buffer request!");
                updateBuffer(cfd, state);
                break;

            case M_RESIZE_BUFFER:
                ALOGD_IF(DEBUG, "Resize buffer request!");
                resizeBuffer(cfd, state);
                break;

            case M_LOCK_BUFFER:
                ALOGD_IF(DEBUG, "Lock buffer request!");
                lockBuffer(cfd, state);
                break;

            case M_UNLOCK_AND_POST_BUFFER:
                ALOGD_IF(DEBUG, "Unlock and post buffer request!");
                unlockAndPostBuffer(cfd, state);
                break;

            default:
                ALOGW("Unrecognized request");
                /*
                 * WATCH OUT! Using write() AND sendmsg() at the
                 * same time to send a reply can result in mixed up
                 * order on the client-side when calling recvmsg() 
                 * and parsing the main data buffer.
                 * Basically, don't mix calls to write() and writev().
                 */
                // if (write(cfd, ACK, strlen(ACK) + 1) < 0) {
                //     ALOGE("Failed to write socket ACK: %s", strerror(errno));
                // }
                break;
        }
    } while (1);

    reset_state(state);
    close(cfd);
}

int main() {

    struct mflinger_state state;
    state.num_surfaces = 0;
    state.layerstack = -1;

    //
    // Establish a connection with SurfaceFlinger
    //
    state.compositor = new SurfaceComposerClient;
    status_t check = state.compositor->initCheck();
    ALOGD_IF(DEBUG, "compositor->initCheck() = %d", check);
    if (NO_ERROR != check) {
        ALOGE("compositor->initCheck() failed!");
        return -1;
    }

    //
    // Connect to bridge socket
    //
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        ALOGE("Failed to create socket: %s", strerror(errno));
        return -1;
    }

    int len;
    struct sockaddr_un local;

    local.sun_family = AF_UNIX;

    /* add a leading null byte to indicate abstract socket namespace */
    local.sun_path[0] = '\0';
    strcpy(local.sun_path + 1, M_SOCK_PATH);
    len = 1 + strlen(local.sun_path + 1) + sizeof(local.sun_family);

    /* unlink just in case...but abstract names should be auto destroyed */
    unlink(local.sun_path);
    int err = bind(sockfd, (struct sockaddr *)&local, len);
    if (err < 0) {
        ALOGE("Failed to bind socket: %s", strerror(errno));
        return -1;
    }

    err = listen(sockfd, 1);
    if (err < 0) {
        ALOGE("Failed to listen on socket: %s", strerror(errno));
        return -1;
    }

    //
    // Serve loop
    //
    ALOGI("At your service!");
    for (;;) {
        serve(sockfd, &state);
    }


    //
    // Cleanup
    //
    purge_surfaces(&state);
    state.compositor = NULL;

    close(sockfd);
    return 0;
}
