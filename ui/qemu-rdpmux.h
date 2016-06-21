#ifndef _QEMU_RDPMUX_H
#define _QEMU_RDPMUX_H

#include <rdpmux.h>

#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "ui/console.h"
#include "qemu/config-file.h"
#include "qemu/thread.h"
#include "ui/input.h"

typedef struct mux_qemu_display {
    MuxDisplay *s;
    DisplayChangeListener dcl;
    pixman_image_t *surface;
    Notifier exit_notifier;
} QemuMuxDisplay;

void mux_qemu_display_update(DisplayChangeListener *dcl,
        int x, int y, int w, int h);
void mux_qemu_display_switch(DisplayChangeListener *dcl,
        DisplaySurface *ds);
void mux_qemu_display_copy(DisplayChangeListener *dcl,
        int src_x, int src_y, int dest_x, int dest_y, int w, int h);
void mux_qemu_display_refresh(DisplayChangeListener *dcl);
void mux_qemu_mouse_set(DisplayChangeListener *dcl, int x, int y, int on);
void mux_qemu_cursor_define(DisplayChangeListener *dcl, QEMUCursor *c);
void mux_qemu_receive_mouse(uint32_t mouse_x, uint32_t mouse_y,
        uint32_t flags);
void mux_qemu_receive_kb(uint32_t keycode, uint32_t flags);
void mux_qemu_init(void);

#endif
