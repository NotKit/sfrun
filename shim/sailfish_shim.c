/*
 * libsailfish-shim.so  --  LD_PRELOAD compatibility shim for running Harbour
 * Sailfish apps directly against Lomiri's Wayland compositor (no Lipstick).
 *
 * It fills, in-process, the three gaps Lipstick + Qt's custom Wayland
 * extensions would normally provide:
 *
 *   1. Registry diagnostics + missing-global safety.  Sailfish's Qt 5.6
 *      QtWayland tries to bind qt_surface_extension / qt_windowmanager and
 *      (harmlessly) skips them when absent.  We interpose the wl_registry
 *      listener so we can (a) log exactly what Lomiri advertises vs. what the
 *      app asks for, and (b) later hand back a no-op stub for any global that
 *      turns out to be *mandatory* in the Sailfish build.  This is the
 *      in-process substitute for a socket proxy.
 *
 *   2. Ambience background.  Silica's ApplicationWindow is partly translucent
 *      and expects the compositor to paint the wallpaper underneath.  Lomiri
 *      does not, so we force the top-level wl_surface opaque (and, later, can
 *      attach a solid/wallpaper background).  See mark_surface_opaque().
 *
 *   3. Orientation.  qt_surface_extension carries orientation on Sailfish.
 *      With it absent the app gets no orientation events; we expose a fixed
 *      default here and leave a hook to drive it from host sensors later.
 *
 * Status: diagnostics (#1 logging) are implemented and runnable; the opaque
 * background (#2) and orientation (#3) are scaffolded with clear TODOs to be
 * landed during spikes 2-3.
 *
 * Build: make  (see Makefile)
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* We talk to whatever libwayland-client the guest Qt links against; resolve
 * symbols lazily via dlsym(RTLD_NEXT) so we never hard-link it ourselves. */

struct wl_display;
struct wl_proxy;
struct wl_interface;

typedef void (*wl_registry_global_t)(void *data, struct wl_proxy *registry,
                                     uint32_t name, const char *interface,
                                     uint32_t version);
typedef void (*wl_registry_global_remove_t)(void *data, struct wl_proxy *registry,
                                            uint32_t name);

struct wl_registry_listener {
    wl_registry_global_t global;
    wl_registry_global_remove_t global_remove;
};

static int debug_enabled(void) {
    static int v = -1;
    if (v < 0) v = getenv("SFRUN_DEBUG") ? 1 : 0;
    return v;
}

static void shim_log(const char *fmt, ...) {
    if (!debug_enabled()) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[sailfish-shim] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

/* ------------------------------------------------------------------ *
 * #1 Registry diagnostics
 *
 * We wrap the app's own global handler so every advertised global is logged.
 * Hook point: wl_registry_add_listener (a thin inline in wayland-client.h that
 * lowers to wl_proxy_add_listener).  We interpose wl_proxy_add_listener and,
 * when the listener looks like a registry listener, wrap its `global` cb.
 * ------------------------------------------------------------------ */

static wl_registry_global_t real_global_cb;

static void logging_global(void *data, struct wl_proxy *registry,
                           uint32_t name, const char *interface,
                           uint32_t version) {
    shim_log("global advertised: %-40s v%u (name=%u)", interface, version, name);

    /* TODO(spike-3): if `interface` is one the Sailfish QPA *requires* but
     * Lomiri does not advertise, this is where we would synthesize a stub
     * proxy instead of forwarding.  Until a spike proves one is mandatory we
     * forward everything unchanged. */

    if (real_global_cb)
        real_global_cb(data, registry, name, interface, version);
}

typedef int (*wl_proxy_add_listener_t)(struct wl_proxy *proxy,
                                       void (**impl)(void), void *data);

/* Must be exported (default visibility) so the dynamic linker resolves the
 * app's calls here ahead of libwayland-client's. */
__attribute__((visibility("default")))
int wl_proxy_add_listener(struct wl_proxy *proxy,
                          void (**impl)(void), void *data) {
    static wl_proxy_add_listener_t real;
    if (!real) real = (wl_proxy_add_listener_t)dlsym(RTLD_NEXT, "wl_proxy_add_listener");

    /* Heuristic: a registry listener's first slot is the `global` callback.
     * We can't cheaply prove the proxy is a wl_registry from here, so we only
     * wrap when diagnostics are on, keeping the production path untouched. */
    if (debug_enabled() && impl && impl[0]) {
        struct wl_registry_listener *l = (struct wl_registry_listener *)impl;
        if (!real_global_cb) {
            real_global_cb = l->global;
            l->global = logging_global;
            shim_log("registry listener wrapped for diagnostics");
        }
    }
    return real(proxy, impl, data);
}

/* ------------------------------------------------------------------ *
 * #2 Ambience background  (scaffold)
 *
 * Plan: when the toplevel wl_surface is created, set an opaque region covering
 * it so Lomiri does not blend whatever is behind (Silica expects wallpaper).
 * Cleanest hook is intercepting wl_compositor_create_surface / wl_surface
 * commit, or driving it Qt-side via the scene-graph clear colour.  Decide in
 * spike 2 once we can see the artefact.
 * ------------------------------------------------------------------ */

/* static void mark_surface_opaque(struct wl_proxy *surface) { ... } */

/* ------------------------------------------------------------------ *
 * #3 Orientation  (scaffold)
 *
 * With qt_surface_extension absent the app sees no orientation.  Hook: expose a
 * fixed orientation (default portrait) and, later, push changes from host
 * sensorfw/repowerd into Qt's QScreen orientation.
 * ------------------------------------------------------------------ */

__attribute__((constructor))
static void shim_init(void) {
    shim_log("loaded (diagnostics=%d)", debug_enabled());
}
