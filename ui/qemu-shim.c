#include "qemu-shim.h"

static const DisplayChangeListenerOps shim_display_listener_ops = {
    .dpy_name = "shim",
    .dpy_gfx_update = shim_qemu_display_update,
    .dpy_gfx_switch = shim_qemu_display_switch,
    .dpy_gfx_check_format = qemu_pixman_check_format,
    .dpy_refresh = shim_qemu_display_refresh,
    .dpy_mouse_set = shim_qemu_mouse_set,
    .dpy_cursor_define = shim_qemu_cursor_define,
};

static const EventCallbacks shim_display_ops = {
    .shim_receive_kb = shim_qemu_receive_kb,
    .shim_receive_mouse = shim_qemu_receive_mouse,
};

static QemuOptsList qemu_shim_opts = {
    .name = "shim",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_shim_opts.head),
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

static ShimQemuDisplay *display;

void shim_qemu_display_update(DisplayChangeListener *dcl,
        int x, int y, int w, int h)
{
    shim_display_update(x, y, w, h);
}

void shim_qemu_display_switch(DisplayChangeListener *dcl,
        DisplaySurface *ds)
{
    display->surface = ds->image;
    shim_display_switch(ds->image); 
}

void shim_qemu_display_refresh(DisplayChangeListener *dcl)
{
    shim_display_refresh();
}

void shim_qemu_mouse_set(DisplayChangeListener *dcl, int x, int y, int on)
{
    // TODO: fill in
}

void shim_qemu_cursor_define(DisplayChangeListener *dcl, QEMUCursor *c)
{
    // TODO: fill in
}

static void shim_qemu_mouse_move(uint32_t x, uint32_t y)
{
    QemuConsole *con = display->dcl.con;
    qemu_input_queue_abs(con, INPUT_AXIS_X, x,
            pixman_image_get_width(display->surface));
    qemu_input_queue_abs(con, INPUT_AXIS_Y, y,
            pixman_image_get_height(display->surface));
    qemu_input_event_sync();
}

static void shim_qemu_mouse_buttons(uint32_t x, uint32_t y, uint32_t flags)
{
    QemuConsole *con = display->dcl.con;
    switch(flags) {
        case 0x9000: // left mouse down
            qemu_input_queue_btn(con, INPUT_BUTTON_LEFT, 1);
            break;
        case 0x1000: // left mouse up
            qemu_input_queue_btn(con, INPUT_BUTTON_LEFT, 0);
            break;
        case 0xA000: // right mouse down
            qemu_input_queue_btn(con, INPUT_BUTTON_RIGHT, 1);
            break;
        case 0x2000: // right mouse up
            qemu_input_queue_btn(con, INPUT_BUTTON_RIGHT, 0);
        case 0xC000: // middle mouse down
            qemu_input_queue_btn(con, INPUT_BUTTON_MIDDLE, 1);
            break;
        case 0x4000: // middle mouse up
            qemu_input_queue_btn(con, INPUT_BUTTON_MIDDLE, 0);
            break;
        default:     // oh god why i am not good with computer
            printf("ERROR: Invalid mouse button input: 0x%X but why though\n", flags);
            break;
    }
    qemu_input_event_sync();
}

void shim_qemu_receive_mouse(uint32_t mouse_x, uint32_t mouse_y, uint32_t flags)
{
    if (flags == 0x800) {
        shim_qemu_mouse_move(mouse_x, mouse_y);
    } else {
        shim_qemu_mouse_buttons(mouse_x, mouse_y, flags);
    }
}

void shim_qemu_receive_kb(uint32_t keycode, uint32_t flags)
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
        printf("INPUT: Unknown kb msg 0x%X\n", keycode);
    }
}

static void *shim_qemu_update_loop(void *arg)
{
    shim_display_buffer_update_loop(arg);
    return NULL;
}

static void *shim_qemu_in_loop(void *arg)
{
    shim_mainloop(arg);
    return NULL;
}

static void *shim_qemu_out_loop(void *arg)
{
    shim_out_loop(arg);
    return NULL;
}

void shim_qemu_init(void)
{
    printf("QEMU-SHIM: Now starting shim code!\n");
    QemuThread threads[3];
    int id = g_random_int_range(0, INT_MAX);
    QemuConsole *con;
    int i;
    display = g_malloc0(sizeof(ShimQemuDisplay));

    display->s = shim_init_display_struct();
    shim_register_event_callbacks(shim_display_ops);

    for (i = 0; ; i++) {
        con = qemu_console_lookup_by_index(i);
        if (con && qemu_console_is_graphic(con)) {
            break;
        }
    }

    display->dcl.ops= &shim_display_listener_ops;
    display->dcl.con = con;

    char *path = g_malloc0(sizeof(char) * 4096);
    
    // get opts
    QemuOpts *opts = QTAILQ_FIRST(&qemu_shim_opts.head);
    const char *dbus_obj_path = qemu_opt_get(opts, "dbus-path");
    const char *dbus_obj_name = qemu_opt_get(opts, "dbus-object");

    printf("QEMU-SHIM: '%s' and '%s'\n", dbus_obj_path, dbus_obj_name);

    if (shim_get_socket_path(dbus_obj_name, dbus_obj_path, &path, id) != true) {
        printf("ERROR: cannot get socket path, bailing!\n");
        goto socket_path_cleanup;
    }
    
    printf("QEMU-SHIM: Socket path is %s\n", path);

    if (shim_connect(path) != true) {
        printf("ERROR: connecting to socket failed, please send help\n");
        goto socket_path_cleanup;
    }

    // important that this comes after the structs have initialized and comms
    // are set up, because as soon as this method is called, display update 
    // events start coming down the pipe.
    register_displaychangelistener(&display->dcl);

    qemu_thread_create(&threads[0], "shim_qemu_in_loop", shim_qemu_in_loop,
            NULL, QEMU_THREAD_DETACHED);
    qemu_thread_create(&threads[1], "shim_qemu_out_loop", shim_qemu_out_loop,
            NULL, QEMU_THREAD_DETACHED);
    qemu_thread_create(&threads[2], "buffer_update_loop", shim_qemu_update_loop,
            NULL, QEMU_THREAD_DETACHED);

socket_path_cleanup:
    g_free(path);
    return;
}

static void qemu_shim_register_config(void)
{
    qemu_add_opts(&qemu_shim_opts);
}
machine_init(qemu_shim_register_config);
