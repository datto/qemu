#include "qemu-rdpmux.h"
#include "qemu-common.h"

static const DisplayChangeListenerOps mux_display_listener_ops = {
    .dpy_name = "mux",
    .dpy_gfx_update = mux_qemu_display_update,
    .dpy_gfx_switch = mux_qemu_display_switch,
    .dpy_gfx_check_format = qemu_pixman_check_format,
    .dpy_refresh = mux_qemu_display_refresh,
    .dpy_mouse_set = mux_qemu_mouse_set,
    .dpy_cursor_define = mux_qemu_cursor_define,
};

static const InputEventCallbacks mux_display_ops = {
    .mux_receive_kb = mux_qemu_receive_kb,
    .mux_receive_mouse = mux_qemu_receive_mouse,
};

static QemuOptsList qemu_mux_opts = {
    .name = "mux",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_mux_opts.head),
    .desc = {
        {
            .name = "dbus-path",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "dbus-object",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static QemuMuxDisplay *display;

MuxInfo *qmp_query_mux(Error **errp)
{
    QemuOpts *opts = QTAILQ_FIRST(&qemu_mux_opts.head);
    MuxInfo *info;
    const char *obj;
    const char *path;

    info = g_malloc0(sizeof(*info));

    if (!display) {
        info->enabled = false;
        return info;
    }

    info->enabled = true;
    obj = qemu_opt_get(opts, "dbus-object");
    path = qemu_opt_get(opts, "dbus-path");

    info->obj = strdup(obj);
    info->path = strdup(path);

    return info;
}


void mux_qemu_display_update(DisplayChangeListener *dcl,
        int x, int y, int w, int h)
{
    mux_display_update(x, y, w, h);
}

void mux_qemu_display_switch(DisplayChangeListener *dcl,
        DisplaySurface *ds)
{
    display->surface = ds->image;
    mux_display_switch(ds->image);
}

void mux_qemu_display_refresh(DisplayChangeListener *dcl)
{
    mux_display_refresh();
}

void mux_qemu_mouse_set(DisplayChangeListener *dcl, int x, int y, int on)
{

}

void mux_qemu_cursor_define(DisplayChangeListener *dcl, QEMUCursor *c)
{

}

static void mux_qemu_mouse_move(uint32_t x, uint32_t y)
{
    QemuConsole *con = display->dcl.con;
    qemu_input_queue_abs(con, INPUT_AXIS_X, x,
            pixman_image_get_width(display->surface));
    qemu_input_queue_abs(con, INPUT_AXIS_Y, y,
            pixman_image_get_height(display->surface));
    qemu_input_event_sync();
}

static void mux_qemu_mouse_buttons(uint32_t x, uint32_t y, uint32_t flags)
{
    QemuConsole *con = display->dcl.con;
    switch (flags) {
    case 0x9000: /* left mouse down */
        qemu_input_queue_btn(con, INPUT_BUTTON_LEFT, 1);
        break;
    case 0x1000: /* left mouse up */
        qemu_input_queue_btn(con, INPUT_BUTTON_LEFT, 0);
        break;
    case 0xA000: /* right mouse down */
        qemu_input_queue_btn(con, INPUT_BUTTON_RIGHT, 1);
        break;
    case 0x2000: /* right mouse up */
        qemu_input_queue_btn(con, INPUT_BUTTON_RIGHT, 0);
    case 0xC000: /* middle mouse down */
        qemu_input_queue_btn(con, INPUT_BUTTON_MIDDLE, 1);
        break;
    case 0x4000: /* middle mouse up */
        qemu_input_queue_btn(con, INPUT_BUTTON_MIDDLE, 0);
        break;
    default:
        printf("ERROR: Invalid mouse button input: 0x%X\n", flags);
        break;
    }
    qemu_input_event_sync();
}

void mux_qemu_receive_mouse(uint32_t mouse_x, uint32_t mouse_y, uint32_t flags)
{
    if (flags == 0x800) {
        mux_qemu_mouse_move(mouse_x, mouse_y);
    } else {
        mux_qemu_mouse_buttons(mouse_x, mouse_y, flags);
    }
}

void mux_qemu_receive_kb(uint32_t keycode, uint32_t flags)
{
    if (flags == 0x4000 || flags == 0x4100) {
        if (keycode == 0x5b || keycode == 0x5c) {
            qemu_input_event_send_key_qcode(NULL, Q_KEY_CODE_META_L, true);
        } else {
            qemu_input_event_send_key_number(display->dcl.con, keycode, true);
            qemu_input_event_send_key_delay(0);
        }
    } else if (flags == 0x8000 || flags == 0x8100) {
        if (keycode == 0x5b || keycode == 0x5c) {
            qemu_input_event_send_key_qcode(NULL, Q_KEY_CODE_META_L, false);
        } else {
            qemu_input_event_send_key_number(display->dcl.con, keycode, false);
            qemu_input_event_send_key_delay(0);
        }
    } else {
        printf("ERROR: Unknown kb msg 0x%X\n", keycode);
    }
}

static void *mux_qemu_update_loop(void *arg)
{
    mux_display_buffer_update_loop(arg);
    return NULL;
}

static void *mux_qemu_in_loop(void *arg)
{
    mux_mainloop(arg);
    return NULL;
}

static void *mux_qemu_out_loop(void *arg)
{
    mux_out_loop(arg);
    return NULL;
}

void mux_qemu_init(void)
{
    QemuThread threads[3];
    int id = g_random_int_range(0, INT_MAX);
    QemuConsole *con;
    int i;
    display = g_malloc0(sizeof(QemuMuxDisplay));

    display->s = mux_init_display_struct();
    mux_register_event_callbacks(mux_display_ops);

    for (i = 0; ; i++) {
        con = qemu_console_lookup_by_index(i);
        if (con && qemu_console_is_graphic(con)) {
            break;
        }
    }

    display->dcl.ops = &mux_display_listener_ops;
    display->dcl.con = con;

    char *path = g_malloc0(sizeof(char) * 4096);

    /* get opts */
    QemuOpts *opts = QTAILQ_FIRST(&qemu_mux_opts.head);
    const char *dbus_obj_path = qemu_opt_get(opts, "dbus-path");
    const char *dbus_obj_name = qemu_opt_get(opts, "dbus-object");

    if (mux_get_socket_path(dbus_obj_name, dbus_obj_path, &path, id) != true) {
        printf("ERROR: cannot get socket path, bailing!\n");
        goto socket_path_cleanup;
    }

    if (mux_connect(path) != true) {
        printf("ERROR: connecting to socket failed\n");
        goto socket_path_cleanup;
    }

    register_displaychangelistener(&display->dcl);

    qemu_thread_create(&threads[0], "mux_qemu_in_loop", mux_qemu_in_loop,
            NULL, QEMU_THREAD_DETACHED);
    qemu_thread_create(&threads[1], "mux_qemu_out_loop", mux_qemu_out_loop,
            NULL, QEMU_THREAD_DETACHED);
    qemu_thread_create(&threads[2], "buffer_update_loop", mux_qemu_update_loop,
            NULL, QEMU_THREAD_DETACHED);

socket_path_cleanup:
    g_free(path);
    return;
}

static void qemu_mux_register_config(void)
{
    qemu_add_opts(&qemu_mux_opts);
}
machine_init(qemu_mux_register_config);
