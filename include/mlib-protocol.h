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

#ifndef MLIB_PROTOCOL_H
#define MLIB_PROTOCOL_H

/*
 * This defines the mflinger protocol.
 *
 * Note that some of the requests do not have responses. This
 * is a quick optimization so that the client does not need to
 * wait on a response for "streaming" style calls where a few
 * failures do not affect the end result. Cross your fingers
 * and hope for the best style.
 */

//
// Transport
//
#define M_SOCK_PATH "maru-bridge"

//
// Opcodes
//
#define M_GET_DISPLAY_INFO          (1 << 4)
#define M_CREATE_BUFFER             (1 << 5)
#define M_UPDATE_BUFFER             (1 << 6)
#define M_LOCK_BUFFER               (1 << 7)
#define M_UNLOCK_AND_POST_BUFFER    (1 << 8)
#define M_RESIZE_BUFFER             (1 << 9)

struct MRequestHeader {
    /* 
     * I experienced deep pain when micro-optimizing
     * this as a single char due to struct padding...
     *
     * Structure packing is not an option as the server
     * needs to read the op segment first to route requests.
     * And I don't want a fixed-length protocol.
     *
     * KISS = just use 4 bytes, jeez.
     */
    uint32_t op;
};
typedef struct MRequestHeader MRequestHeader;

struct MGetDisplayInfoRequest {
    // empty
};
typedef struct MGetDisplayInfoRequest MGetDisplayInfoRequest;

struct MGetDisplayInfoResponse {
    uint32_t width;
    uint32_t height;
};
typedef struct MGetDisplayInfoResponse MGetDisplayInfoResponse;

struct MCreateBufferRequest {
    uint32_t width;
    uint32_t height;
};
typedef struct MCreateBufferRequest MCreateBufferRequest;

struct MCreateBufferResponse {
    int32_t id;        /* identifier for the created buffer */
    int32_t result;    /* 0 = success, -1 = failure */
};
typedef struct MCreateBufferResponse MCreateBufferResponse;

struct MUpdateBufferRequest {
    int32_t id;
    uint32_t xpos;
    uint32_t ypos;
};
typedef struct MUpdateBufferRequest MUpdateBufferRequest;

struct MUpdateBufferResponse {
    int32_t result;
};
typedef struct MUpdateBufferResponse MUpdateBufferResponse;

struct MResizeBufferRequest {
    int32_t id;
    uint32_t width;
    uint32_t height;
};
typedef struct MResizeBufferRequest MResizeBufferRequest;

struct MResizeBufferResponse {
    int32_t result;
};
typedef struct MResizeBufferResponse MResizeBufferResponse;

struct MLockBufferRequest {
    int32_t id;
};
typedef struct MLockBufferRequest MLockBufferRequest;

struct MLockBufferResponse {
    MBuffer buffer;
    int32_t result;
};
typedef struct MLockBufferResponse MLockBufferResponse;

struct MUnlockBufferRequest {
    int32_t id;
};
typedef struct MUnlockBufferRequest MUnlockBufferRequest;

#endif // MLIB_PROTOCOL_H
