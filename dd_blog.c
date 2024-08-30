/**
  * Author: kpowkitty
  * Credit: Drew DeVault https://drewdevault.com/2018/02/17/Writing-a-Wayland-compositor-1.html
  * Date: August 2024
  * Status: Unfinished (see readme)
  */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "wayland-server-protocol.h"
#include "wayland-server.h"
#include "wlr/backend.h"
#include "wlr/render/wlr_renderer.h"
#include "wlr/types/wlr_output.h"
#include "wlr/render/drm_format_set.h"
#include "drm_fourcc.h"

/**
* Holds the compositor's state.
*/
struct my_server 
{
    // Needs wl_display to start.
    // Obtains a reference to the display; this establishes a connection
    // to the Wayland server. 
    // wl_display provides the wl_registry, which enumerates the
    // globals on the server.
    struct wl_display *wl_display;
    struct wl_event_loop *wl_event_loop;
    
    // Abstracts the low-level input & output implementations
    // AKA Communication with mice, keyboard, display
    struct wlr_backend *backend;
    struct wlr_session **session;

    struct wlr_renderer *renderer;

    struct wl_listener new_output;

    struct wl_list outputs;
};

/**
 * Stores any state we have for this output that is specific to our
 * compositor's needs.
 */
struct my_output 
{
    struct wlr_output *wlr_output;
    struct my_server *server;
    struct timespec last_frame;

    struct wl_listener destroy;
    // Listens for the frame signal from wlroots, necessary for rendering.
    struct wl_listener frame;
    
    // Add it to the server's lists of outputs
    struct wl_list link;
};

static void output_destroy_notify(struct wl_listener *listener, void *data)
{
    struct my_output *output = wl_container_of(listener, output, destroy); 
    wl_list_remove(&output->link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->frame.link);
    free(output);
}

static int output_frame_notify(struct wl_listener *listener, void *data)
{
    struct my_output *output = wl_container_of(listener, output, frame);
    struct wlr_output *wlr_output = data;
    struct my_server *server = output->server;

    // Need to obtain wlr_renderer in order to render.
    struct wlr_renderer *renderer = ;
    if (!renderer) {
        fprintf(stderr, "Failed to get renderer\n");
        return -1;
    }

    // Obtain allocator from backend
    struct wlr_allocator *allocator = wlr_allocator_init(server->backend, 
                                                               renderer);
    if (!allocator) {
        fprintf(stderr, "Failed to get allocator\n");
        return -1;
    }

    // Define format
    const struct wlr_drm_format format = { 
        .format = DRM_FORMAT_ARGB8888 
    };

    // Create swapchain
    struct wlr_swapchain *swapchain = wlr_swapchain_create(allocator, 
                                                           wlr_output->width,
                                                           wlr_output->height,
                                                           &format);
    if (!swapchain) {
        fprintf(stderr, "Failed to create swapchain\n");
        return;
    }
    
    // Acquire buffer from swapchain
    struct wlr_buffer *buffer = wlr_swapchain_acquire(swapchain);
    if (!buffer) {
        fprintf(stderr, "Failed to acquire buffer\n");
        wlr_swapchain_destroy(swapchain);
        return;
    }

    // Start rendering
    if (!wlr_renderer_begin_with_buffer(renderer, buffer)) {
        fprintf(stderr, "Failed to begin render pass\n");
        wlr_buffer_unlock(buffer);
        wlr_swapchain_destroy(swapchain);
        return;
    }

    // Clear to red
    float color[4] = {1.0, 0, 0, 1.0};
    wlr_renderer_clear(renderer, color);

    // End rendering
    wlr_renderer_end(renderer);

    // Attach the buffer to the output
    wlr_output_attach_buffer(wlr_output, buffer);

    // Commit output
    wlr_output_commit(wlr_output);

    // Cleanup
    wlr_swapchain_destroy(swapchain);
}

/**
* Deals with the incoming wlr_output.
* @param Pointer to the listener that was signaled.
* @param Pointer to the wlr_output which was created.
*/
static void new_output_notify(struct wl_listener *listener, void *data)
{
    // wl_container_of uses "offsetof-based magic" --offsetof grabs the offset
    // in bytes of a structure member from the beginning of the structure
    // as size_t
    struct my_server *server = wl_container_of(listener, server, new_output);
    // Take our void data and cast it to the actual data type
    struct wlr_output *wlr_output = data;

    // Next we set the output mode.
    // Output modes specify a size and refresh rate supported by the output.

    // Chooses the last available output mode (highest) & applies it.
    // This is necessary in order to render.
    if (!wl_list_empty(&wlr_output->modes)) {
        struct wlr_output_mode *mode = wl_container_of(wlr_output->modes.prev,
                                                       mode, link);
        wlr_output_set_mode(wlr_output, mode);
    }

    struct my_output *output = calloc(1, sizeof(struct my_output));
    clock_gettime(CLOCK_MONOTONIC, &output->last_frame);
    output->server = server;
    output->wlr_output = wlr_output;
    wl_list_insert(&server->outputs, &output->link);

    output->destroy.notify = output_destroy_notify;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    // Whenever an output is ready for a new frame, output_frame_notify 
    // will be called.
    output->frame.notify = output_frame_notify;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
}

int main(int argc, char **argv) 
{
    struct my_server server;

    server.wl_display = wl_display_create();
    assert(server.wl_display);
    server.wl_event_loop = wl_display_get_event_loop(server.wl_display);
    assert(server.wl_event_loop);
    
    // Helper function for choosing the most appropriate backend based on
    // user's environment.
    server.backend = wlr_backend_autocreate(server.wl_display, server.session);
    assert(server.backend);

    wl_list_init(&server.outputs);

    // After adding wl_listener and wl_list in our struct code, which they
    // signal when new outputs are added, we need to do this in order for
    // our side to grab the signal.
    server.new_output.notify = new_output_notify;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);
    
    // Start the backend and enter the Wayland event loop.
    if (!wlr_backend_start(server.backend)) {
        fprintf(stderr, "Failed to start backend\n");
        wl_display_destroy(server.wl_display);
        return 1;
    }

    wl_display_run(server.wl_display);
    wl_display_destroy(server.wl_display);
    return 0;
}
