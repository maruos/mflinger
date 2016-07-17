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
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>

#include "mlib.h"
#include "mlib-protocol.h"
#include "mlog.h"

//
// Private
//
static int buffer_size(MBuffer *buf) {
    return buf->stride * buf->height * 4;
}

static int recvfd(const int sock_fd, void *data, const int data_len) {
    struct msghdr msgh = {0};
    struct cmsghdr *cmsg;
    struct iovec iov;
    char control[CMSG_SPACE(sizeof(int))];      /* single int fd */
    int n, buf_fd;

    /* we read data_len bytes from the socket into data */
    iov.iov_base = data;
    iov.iov_len = data_len;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;

    /* set up the control (ancillary) buffer */
    msgh.msg_control = control;
    msgh.msg_controllen = sizeof(control);

    n = recvmsg(sock_fd, &msgh, MSG_WAITALL);
    if (n < 0) {
        MLOGE("recvmsg error: %s\n", strerror(errno));
        return -1;
    }

    /* TODO check n to ensure proper response!!! */

    if (msgh.msg_flags) {
        if (msgh.msg_flags & MSG_CTRUNC) {
            MLOGE("insufficient buffer space for ancillary data\n");
            return -1;
        }
    }

    /* 
     * loop through the control data to pull the fd
     * (there should only be one control message)
     */
    for (cmsg = CMSG_FIRSTHDR(&msgh); cmsg != NULL;
            cmsg = CMSG_NXTHDR(&msgh, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET &&
                cmsg->cmsg_type == SCM_RIGHTS) {
            buf_fd = *(int *) CMSG_DATA(cmsg);
            return buf_fd;
        }
    }

    return -1;
}

//
// Public
//
int MOpenDisplay(MDisplay *dpy) {
    int sock_fd, len;
    struct sockaddr_un remote;

    /* create the socket shell */
    if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        MLOGE("error opening socket: %s\n", strerror(errno));
        return -1;
    }

    /* Unix domain socket */
    remote.sun_family = AF_UNIX;

    /* set up abstract namespace path */
    strcpy(remote.sun_path + 1, M_SOCK_PATH);
    remote.sun_path[0] = '\0'; // abstract namespace indicator
    len = 1 + strlen(remote.sun_path + 1) + sizeof(remote.sun_family);

    /* try to connect! */
    if (connect(sock_fd, (struct sockaddr *)&remote, len) == -1) {
        MLOGE("error connecting socket: %s\n", strerror(errno));
        return -1;
    }

    dpy->sock_fd = sock_fd;
    return 0;
}

int MCloseDisplay(MDisplay *dpy) {
    if (close(dpy->sock_fd) < 0) {
        MLOGE("error closing socket: %s\n", strerror(errno));
        return -1;
    }

    dpy->sock_fd = -1;
    return 0;
}

int MGetDisplayInfo(MDisplay *dpy, MDisplayInfo *dpy_info) {
    struct {
        MRequestHeader header;
        MGetDisplayInfoRequest request;
    } packet;
    packet.header.op = M_GET_DISPLAY_INFO;

    if (write(dpy->sock_fd, &packet, sizeof(packet)) < 0) {
        MLOGE("error sending get display info request: %s\n",
            strerror(errno));
        return -1;
    }

    MGetDisplayInfoResponse response;
    if (read(dpy->sock_fd, &response, sizeof(response)) < 0) {
        MLOGE("error receiving get display info response: %s\n",
            strerror(errno));
        return -1;
    }

    dpy_info->width = response.width;
    dpy_info->height = response.height;
    return 0;
}

int MCreateBuffer(MDisplay *dpy, MBuffer *buf) {
    struct {
        MRequestHeader header;
        MCreateBufferRequest request;
    } packet;
    packet.header.op = M_CREATE_BUFFER;
    packet.request.width = buf->width;
    packet.request.height = buf->height;

    /* send create buffer request to server */
    if (write(dpy->sock_fd, &packet, sizeof(packet)) < 0) {
        MLOGE("error sending create buffer request: %s\n",
            strerror(errno));
        return -1;
    }

    /* wait for response... */
    MCreateBufferResponse response;
    if (read(dpy->sock_fd, &response, sizeof(response)) < 0) {
        MLOGE("error receiving create buffer response: %s\n", 
            strerror(errno));
    }

    buf->__id = response.id;
    return response.result ? -1 : 0;
}

int MUpdateBuffer(MDisplay *dpy, MBuffer *buf,
     uint32_t xpos, uint32_t ypos) {
    struct {
        MRequestHeader header;
        MUpdateBufferRequest request;
    } packet;
    packet.header.op = M_UPDATE_BUFFER;
    packet.request.id = buf->__id;
    packet.request.xpos = xpos;
    packet.request.ypos = ypos;

    if (write(dpy->sock_fd, &packet, sizeof(packet)) < 0) {
        MLOGE("error sending update buffer request: %s\n",
            strerror(errno));
        return -1;
    }

    /* TODO response? */

    return 0;
}

int MLockBuffer(MDisplay *dpy, MBuffer *buf) {
    int buf_fd;
    struct {
        MRequestHeader header;
        MLockBufferRequest request;
    } packet;
    packet.header.op = M_LOCK_BUFFER;
    packet.request.id = buf->__id;

    /* send lock buffer request to server */
    if (write(dpy->sock_fd, &packet, sizeof(packet)) < 0) {
        MLOGE("error sending lock buffer request: %s\n",
             strerror(errno));
        return -1;
    }

    /* receive the buffer */
    MLockBufferResponse response;
    buf_fd = recvfd(dpy->sock_fd, &response, sizeof(response));
    if (buf_fd < 0) {
        MLOGE("error receiving buffer fd: %s\n",
             strerror(errno));
        return -1;
    }

    if (buf->width != response.buffer.width ||
        buf->height != response.buffer.height) {
        MLOGW("locked buffer dim mismatch...watch out!\n");
    }
    buf->stride = response.buffer.stride;
    buf->__fd = buf_fd;

    /*
     * mmap into client memory for software r/w
     * 
     * NOTE: we need to be careful since we do not know
     * the offset for sure...let's cross our fingers and
     * guess no offset!
     */
    int offset = 0;
    void *vaddr = mmap(0, buffer_size(buf), PROT_READ|PROT_WRITE,
                 MAP_SHARED, buf_fd, offset);
    if (vaddr == MAP_FAILED) {
        MLOGE("error mmaping buffer: %s\n", strerror(errno));
        close(buf->__fd);
        buf->__fd = -1;
        return -1;
    }
    buf->bits = vaddr;

    return 0;
}

int MUnlockBuffer(MDisplay *dpy, MBuffer *buf) {
    int err;
    struct {
        MRequestHeader header;
        MUnlockBufferRequest request;
    } packet;
    packet.header.op = M_UNLOCK_AND_POST_BUFFER;
    packet.request.id = buf->__id;

    /* send unlock buffer request to server */
    err = write(dpy->sock_fd, &packet, sizeof(packet));
    if (err < 0) {
        MLOGE("error sending unlock buffer request: %s\n",
             strerror(errno));
    }

    /* munmap the stale buffer */
    if (munmap(buf->bits, buffer_size(buf)) < 0) {
        MLOGE("error munmapping buffer: %s\n", strerror(errno));
    }

    /*
     * close the buffer fd or risk flooding the
     * system with new fds on each lock/unlock cycle! 
     */
    close(buf->__fd);
    buf->__fd= -1;
    return err;
}
