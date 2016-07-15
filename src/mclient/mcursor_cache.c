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

#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

/*
 * A "singleton" cache for dealing with the XFixes cursor API.
 *
 * This cache is required because the XFixes client extension will
 * only allocate a given cursor image ONCE the first time it is
 * requested from the server with XFixesGetCursorImage().
 * Subsequent calls to XFixesGetCursorImage() for the same cursor
 * return a null *pixels field. Instead, each cursor is tagged with
 * a serial number that can be used to cache the image.
 */

/* empirical estimate */
#define CURSOR_CACHE_SIZE (36)

static struct cursor_cache_entry {
    XFixesCursorImage *xcursor;
} cursor_cache[CURSOR_CACHE_SIZE];

static XFixesCursorImage *cur_cursor;
static int last_x = -1;
static int last_y = -1;

int cursor_cache_add(XFixesCursorImage *xcursor) {
    if (xcursor == NULL) {
        fprintf(stderr, "cannot add NULL cursor to cache\n");
        return -1;
    }

    int i;
    for (i = 0; i < CURSOR_CACHE_SIZE; ++i) {
        struct cursor_cache_entry *entry = &cursor_cache[i];
        if (entry->xcursor == NULL) {
            entry->xcursor = xcursor;
            return 0;
        } else if (entry->xcursor->cursor_serial == xcursor->cursor_serial) {
            fprintf(stderr, "cursor already in cache\n");
            return -1;
        }
    }

    if (i >= CURSOR_CACHE_SIZE) {
        fprintf(stderr, "uh-oh, cursor cache full!\n");
    }

    return -1;
}

XFixesCursorImage *cursor_cache_get(unsigned long serial) {
    int i;
    for (i = 0; i < CURSOR_CACHE_SIZE; ++i) {
        struct cursor_cache_entry *entry = &cursor_cache[i];
        if (entry->xcursor != NULL &&
                entry->xcursor->cursor_serial == serial) {
            return entry->xcursor;
        }
    }

    return NULL;
}

void cursor_cache_free() {
    int i;
    for (i = 0; i < CURSOR_CACHE_SIZE; ++i) {
        if (cursor_cache[i].xcursor != NULL) {
            XFree(cursor_cache[i].xcursor);
        }
    }
}

void cursor_cache_set_cur(XFixesCursorImage *xcursor) {
    cur_cursor = xcursor;
}

XFixesCursorImage *cursor_cache_get_cur() {
    return cur_cursor;
}

void cursor_cache_set_last_pos(int x, int y) {
    last_x = x;
    last_y = y;
}

void cursor_cache_get_last_pos(int *x_ret, int *y_ret) {
    *x_ret = last_x;
    *y_ret = last_y;
}
