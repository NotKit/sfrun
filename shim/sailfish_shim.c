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

/* Resolve the real libwayland-client wl_proxy_add_listener.
 *
 * dlsym(RTLD_NEXT) alone is NOT enough: a real Qt app reaches wayland by
 * dlopen()ing the QPA platform plugin, which pulls libwayland-client into a
 * *local* link-map scope. RTLD_NEXT only searches the global scope, so it
 * returns NULL there and forwarding would call through a null pointer (crash in
 * QWaylandDisplay's registry init). The Spike 1 test client linked
 * libwayland-client directly (global scope), which is why it never hit this.
 * Fall back to dlopen'ing the library and promoting it to the global scope. */
static wl_proxy_add_listener_t resolve_real_add_listener(void) {
    wl_proxy_add_listener_t f =
        (wl_proxy_add_listener_t)dlsym(RTLD_NEXT, "wl_proxy_add_listener");
    if (f) return f;
    void *h = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
    if (!h) h = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (h) f = (wl_proxy_add_listener_t)dlsym(h, "wl_proxy_add_listener");
    return f;
}

/* Must be exported (default visibility) so the dynamic linker resolves the
 * app's calls here ahead of libwayland-client's. */
__attribute__((visibility("default")))
int wl_proxy_add_listener(struct wl_proxy *proxy,
                          void (**impl)(void), void *data) {
    static wl_proxy_add_listener_t real;
    if (!real) real = resolve_real_add_listener();
    if (!real) {
        /* Never call through a null pointer. Without the real symbol we cannot
         * register the listener; report failure and let the app handle it
         * rather than segfaulting the whole process. */
        shim_log("ERROR: cannot resolve real wl_proxy_add_listener; not forwarding");
        return -1;
    }

    /* Diagnostics (debug only): log every advertised global by wrapping the
     * registry listener's `global` callback. Heuristic: the first add_listener
     * with a non-null first slot is QWaylandDisplay's registry listener.
     * Must NOT write into the caller's array — Qt's wl_registry_listener lives
     * in .rodata, so mutating it segfaults. Forward a heap copy instead. */
    if (debug_enabled() && impl && impl[0] && !real_global_cb) {
        real_global_cb = (wl_registry_global_t)impl[0];
        void (**copy)(void) = malloc(2 * sizeof(*copy));  /* {global, global_remove} */
        if (copy) {
            copy[0] = (void (*)(void))logging_global;
            copy[1] = impl[1];
            shim_log("registry listener wrapped for diagnostics");
            /* copy outlives this call (wayland keeps the pointer); leaked on
             * purpose — one per registry, for the life of the process. */
            return real(proxy, copy, data);
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
