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

// Utility functions to read and write parts of messages
/*
 * Write an unsigned 32 bit integer to the buffer.
*/
static void buf_write_u32(char *buf, uint64_t *buf_size, uint64_t buf_cap,
						  uint32_t x) 
{
	assert(*buf_size + sizeof(x) <= buf_cap);
	// Ensures the position in the buffer where x will be written is properly
	// aligned to sizeof(x)
	assert(((size_t)buf + *buf_size) % sizeof(x) == 0);

	*(uint32_t *)(buf + *buf_size) = x;
	*buf_size += sizeof(x);
}

/*
 * Write an unsigned 16 bit integer to the buffer.
*/
static void buf_write_u16(char *buf, uint64_t *buf_size, uint64_t buf_cap,
						  uint16_t x) 
{
	assert(*buf_size + sizeof(x) <= buf_cap);
	assert(((size_t)buf + *buf_size) % sizeof(x) == 0);

	*(uint16_t *)(buf + *buf_size) = x;
	*buf_size += sizeof(x);
}

/*
 * Write a string to the buffer.
*/
static void buf_write_string(char *buf, uint64_t *buf_size, uint64_t buf_cap,
							 char *src, uint32_t src_len)
{
	assert(*buf_size + src_len <= buf_cap);

	buf_write_u32(buf, buf_size, buf_cap, src_len);
	memcpy(buf + *buf_size, src, roundup_4(src_len));
	*buf_size += roundup_4(src_len);
}

/*
 * Read an unsigned 32 bit integer from the buffer.
*/
static uint32_t buf_read_u32(char **buf, uint64_t *buf_size)
{
	assert(*buf_size >= sizeof(uint32_t));
	// Ensures the buffer pointer is properly aligned for reading 
	// a 32-bit value.
	assert((size_t)*buf % sizeof(uint32_t) == 0);
	// *buf is cast to a uint32_t * to treat it as a pointer 
	// to a 32 bit integer
	uint32_t res = *(uint32_t *)(*buf);
	// Advance the buffer pointer
	*buf += sizeof(res);
	*buf_size -= sizeof(res);

	return res;
}

/*
 * Read an unsigned 16 bit integer from the buffer.
*/
static uint16_t buf_read_u16(char **buf, uint64_t *buf_size)
{
	assert(*buf_size >= sizeof(uint16_t));
	assert((size_t)*buf % sizeof(uint16_t) == 0);

	uint16_t res = *(uint16_t *)(*buf);
	*buf += sizeof(res);
	*buf_size -= sizeof(res);

	return res;
}

/*
 * Read n bytes from a buffer and copy them to a destination (dst)
*/
static void buf_read_n(char **buf, uint64_t *buf_size, char *dst, uint64_t n)
{
	assert(*buf_size >= n);

	memcpy(dst, *buf, n);

	*buf += n;
	*buf_size -= n;
}

/*
 * Requests the registry from the display server over a
 * file descriptor (fd).
 * Returns a newly assigned ID for the registry object.
*/
static uint32_t wayland_wl_display_get_registry(int fd)
{
	uint64_t msg_size = 0;
	char msg[128] = "";

	// Says "This request is being made on the object XYZ"
	buf_write_u32(msg, &msg_size, sizeof(msg), wayland_display_object_id);
	buf_write_u16(msg, &msg_size, sizeof(msg), 
			   wayland_wl_display_get_registry_opcode);
	
	// Inform the server
	uint16_t msg_announced_size = 
		wayland_header_size + sizeof(wayland_current_id);
	assert(roundup_4(msg_announced_size) == msg_announced_size);
	buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);
	
	// Newly created registry object
	wayland_current_id++;
	buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);
	
	// Send the constructed message
	if ((int64_t)msg_size != send(fd, msg, msg_size, MSG_DONTWAIT)) {
		exit(errno);
	}
	
	// Log message showing the details of this request
	printf("-> wl_display@%u.get_registry: wl_registry=%u\n",
		wayland_display_object_id,
		wayland_current_id);

	return wayland_current_id;
}

typedef enum state_state_t state_state_t;

enum state_state_t {
	STATE_NONE,
	STATE_SURFACE_ACKED_CONFIGURE,
	STATE_SURFACE_ATTACHED,
};

typedef struct state_t state_t;
struct state_t {
	uint32_t wl_registry;
	uint32_t wl_shm;
	uint32_t wl_shm_pool;
	uint32_t wl_buffer;
	uint32_t xdg_wm_base;
	uint32_t xdg_surface;
	uint32_t wl_compositor;
	uint32_t wl_surface;
	uint32_t xdg_toplevel;
	uint32_t stride;
	uint32_t w;
	uint32_t h;
	uint32_t shm_pool_size;
	uint32_t shm_fd;
	uint32_t *shm_pool_data;

	state_state_t state;
};

static void create_shared_memory_file(unint64_t size, state_t *state) {
	char name[255] = "/";
	for (uint64_t i = 1; i < cstring_len(name); ++i) {
		name[i] = ((double)rand()) / (double)RAND_MAX * 26 + 'a';
	}

	// shm_open(3) -> create a POSIX shared mem obj
	// RDWR -> read-write
	// CREAT -> if the file DNE, create it
	// EXCL -> returns an error if the shared mem obj with this name alr exists
	// XXX we could use memfd_create(2) as it is linux specific
	int fd = shm_open(name, O_RDWR | O_EXCL | O_CREAT, 0600);
	if (fd == -1) {
		exit(errno);
	}

	assert(shm_unlink(name) != -1);

	if (ftruncate(fd, size) == 1) {
		exit(errno);
	}

	state->shm_pool_data = 
		mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	assert(state->shm_pool_data != NULL);
	state->shm_fd = fd;
}

int main() {
	state_t state = {
		.wl_registry = wayland_wl_display_get_registry(fd),
		.w = 117,
		.h = 150,
		.stride = 117 * color_channels,
	};

	// Single buffering
	state.shm_pool_size = state.h * state.stride;
}
