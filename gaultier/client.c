#define cstring_len(S) (sizeof(s) - 1)

#define roundup_4(c) (((n) + 3) & -4)

/*
 * Opening a UNIX domain socket.
 */

static int wayland_display_connect()
{
    char* xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    if(xdg_runtime_dir == NULL) {
        return EINVAL;
    }

    uint64_t xdg_runtime_dir_len = strlen(xdg_runtime_dir);

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    assert(xdg_runtime_dir_len <= cstring_len(addr.sun_path));
    uint64_t socket_path_len = 0;

    memcpy(addr.sun_path, xdg_runtime_dir, xdg_runtime_dir_len);
    socket_path_len += xdg_runtime_dir_len;

    addr.sun_path[socket_path_len++] = '/';

    char* wayland_display = getenv("WAYLAND_DISPLAY");
    if (wayland_display == NULL) {
        char wayland_display_default[] = "wayland-0";
        uint64_t wayland_display_default_len =
                                        cstring_len(wayland_display_default);

        memcpy(addr.sun_path + socket_path_len, wayland_display_default,
                                                wayland_display_default_len);
        socket_path_len += wayland_display_default_len;
    } else {
        uint64_t wayland_display_len = strlen(wayland_display);
        memcpy(addr.sun_path + socket_path_len, wayland_display,
                                                wayland_display_len);
        socket_path_len += wayland_display;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        exit(errno);
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        exit(errno);
    }

    return fd;
}
