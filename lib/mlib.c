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

    // printf("[DEBUG] *** sizeof(data) = %d\n", sizeof(data));

    /* we read data_len bytes from the socket into data */
    iov.iov_base = data;
    iov.iov_len = data_len;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;

    /* set up the control (ancillary) buffer */
    msgh.msg_control = control;
    msgh.msg_controllen = sizeof(control);

    // printf("[DEBUG] Before recvmsg call\n");
    n = recvmsg(sock_fd, &msgh, MSG_WAITALL);
    if (n < 0) {
        fprintf(stderr, "recvmsg error: %s\n", strerror(errno));
        return -1;
    }

    /* TODO check n to ensure proper response!!! */

    if (msgh.msg_flags) {
        if (msgh.msg_flags & MSG_CTRUNC) {
            fprintf(stderr, "insufficient buffer space for ancillary data\n");
            return -1;
        }
    }

    // for (n = 0; n < 10; ++n) {
    //     // printf("[DEBUG] data[%d] 0x%02hhX\n", n, data[n]);
    //     printf("[DEBUG] data[%d] 0x%02hhX\n", n, ((char *)&data)[n]);
    // }

    // printf("[DEBUG] Before cmsg loop\n");
    /* 
     * loop through the control data to pull the fd
     * (there should only be one control message)
     */
    for (cmsg = CMSG_FIRSTHDR(&msgh); cmsg != NULL;
            cmsg = CMSG_NXTHDR(&msgh, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET &&
                cmsg->cmsg_type == SCM_RIGHTS) {
            buf_fd = *(int *) CMSG_DATA(cmsg);
            // printf("[DEBUG] Got buf_fd = %d!\n", buf_fd);
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
        fprintf(stderr, "error opening socket: %s\n",
             strerror(errno));
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
        fprintf(stderr, "error connecting socket: %s\n",
             strerror(errno));
        return -1;
    }

    dpy->sock_fd = sock_fd;
    return 0;
}

int MCloseDisplay(MDisplay *dpy) {
    if (close(dpy->sock_fd) < 0) {
        fprintf(stderr, "error closing socket: %s\n",
             strerror(errno));
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
        fprintf(stderr, "error sending get display info request: %s\n",
            strerror(errno));
        return -1;
    }

    MGetDisplayInfoResponse response;
    if (read(dpy->sock_fd, &response, sizeof(response)) < 0) {
        fprintf(stderr, "error receiving get display info response: %s\n",
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

    // fprintf(stderr, "[DEBUG] packet size: %d\n", sizeof(packet));

    /* send create buffer request to server */
    if (write(dpy->sock_fd, &packet, sizeof(packet)) < 0) {
        fprintf(stderr, "error sending create buffer request: %s\n",
            strerror(errno));
        return -1;
    }

    /* wait for response... */
    MCreateBufferResponse response;
    if (read(dpy->sock_fd, &response, sizeof(response)) < 0) {
        fprintf(stderr, "error receiving create buffer response: %s\n", 
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
        fprintf(stderr, "error sending update buffer request: %s\n",
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

    // fprintf(stderr, "[DEBUG] packet size: %d\n", sizeof(packet));

    /* send lock buffer request to server */
    if (write(dpy->sock_fd, &packet, sizeof(packet)) < 0) {
        fprintf(stderr, "error sending lock buffer request: %s\n",
             strerror(errno));
        return -1;
    }

    /* receive the buffer */
    MLockBufferResponse response;
    buf_fd = recvfd(dpy->sock_fd, &response, sizeof(response));
    if (buf_fd < 0) {
        fprintf(stderr, "error receiving buffer fd: %s\n",
             strerror(errno));
        return -1;
    }

    if (buf->width != response.buffer.width ||
        buf->height != response.buffer.height) {
        fprintf(stderr, "locked buffer dim mismatch...watch out!\n");
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
    // printf("[DEBUG] mmap()'ing!\n");
    int offset = 0;
    void *vaddr = mmap(0, buffer_size(buf), PROT_READ|PROT_WRITE,
                 MAP_SHARED, buf_fd, offset);
    if (vaddr == MAP_FAILED) {
        fprintf(stderr, "error mmaping buffer: %s\n", strerror(errno));
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
        fprintf(stderr, "error sending unlock buffer request: %s\n",
             strerror(errno));
    }

    /* munmap the stale buffer */
    if (munmap(buf->bits, buffer_size(buf)) < 0) {
        fprintf(stderr, "error munmapping buffer: %s\n", strerror(errno));
    }

    /*
     * close the buffer fd or risk flooding the
     * system with new fds on each lock/unlock cycle! 
     */
    close(buf->__fd);
    buf->__fd= -1;
    return err;
}
