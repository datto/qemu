#ifndef _QEMU_DISPLAY_SHIM_H
#define _QEMU_DISPLAY_SHIM_H

#include <shim.h>

#include "qemu-common.h"
#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "ui/console.h"
#include "qemu/config-file.h"
#include "qemu/thread.h"
#include "ui/input.h"

typedef struct shim_qemu_display {
    ShimDisplay *s;
    DisplayChangeListener dcl;
    pixman_image_t *surface;
} ShimQemuDisplay;

void shim_qemu_display_update(DisplayChangeListener *dcl,
        int x, int y, int w, int h);
void shim_qemu_display_switch(DisplayChangeListener *dcl,
        DisplaySurface *ds);
void shim_qemu_display_refresh(DisplayChangeListener *dcl);
void shim_qemu_mouse_set(DisplayChangeListener *dcl, int x, int y, int on);
void shim_qemu_cursor_define(DisplayChangeListener *dcl, QEMUCursor *c);
void shim_qemu_receive_mouse(uint32_t mouse_x, uint32_t mouse_y, uint32_t flags);
void shim_qemu_receive_kb(uint32_t keycode, uint32_t flags);
void shim_qemu_init(void);

#endif
