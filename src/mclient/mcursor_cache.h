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

#ifndef M_CURSOR_CACHE_H
#define M_CURSOR_CACHE_H

int cursor_cache_add(XFixesCursorImage *xcursor);

XFixesCursorImage *
cursor_cache_get(unsigned long serial);

void cursor_cache_set_cur(XFixesCursorImage *xcursor);

XFixesCursorImage *
cursor_cache_get_cur();

void cursor_cache_set_last_pos(int x, int y);
void cursor_cache_get_last_pos(int *x_ret, int *y_ret);

void cursor_cache_free();

#endif // M_CURSOR_CACHE_H
