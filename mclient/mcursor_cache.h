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
