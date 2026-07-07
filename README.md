# sfrun

Run unmodified **Harbour-compliant** SailfishOS/AuroraOS apps on Ubuntu Touch
(Lomiri/Mir), with minimal overhead.

A Sailfish SDK target rootfs is mounted as the guest userspace inside an
unprivileged **bubblewrap** mount namespace; the host **libhybris** GL stack +
Android HAL are bound in; and the guest Qt 5.6 app talks **directly** to Lomiri's
compositor over **`wl_shell`**. A small `LD_PRELOAD` shim fills the gaps Lipstick
would otherwise provide (ambience background, orientation, missing globals).

- No container daemon (no LXC/podman/docker) — just `bwrap`.
- No nested compositor and no protocol-translating proxy.

**Status: Spike 1 PASSED on real hardware** (MediaTek/Mali, UT 24.04). A
`wl_shell`+EGL client renders on the Mali GPU through host libhybris, inside the
sandbox, direct to Lomiri. The full validated recipe + failure→fix log is in
[`spike/SPIKE1-FINDINGS.md`](spike/SPIKE1-FINDINGS.md) — read it before changing the
launcher's bind/env set.

## Layout

| Path | What |
|------|------|
| `sfrun` | Python CLI: `doctor`, `bootstrap`, `prepare-gl`, `run`, `spike`. Encodes the validated bind/env recipe. |
| `shim/sailfish_shim.c` | `LD_PRELOAD` shim: registry diagnostics + (scaffolded) opaque background and orientation. |
| `spike/sfspike.c` | Minimal `wl_shell`+EGL/GLES2 test client (Spike 1). |
| `spike/spike-run.sh` | The original hand-iterated bwrap invocation (reference). |
| `spike/SPIKE1-FINDINGS.md` | Result, device facts, the working env recipe, failure→fix log. |
| `clickable.yaml`, `click/` | Click packaging (manifest, apparmor, desktop hook, icon). |

Everything runs under a writable base dir (the UT rootfs is read-only): code
(`sfrun`, `shim/`) is located relative to the script itself and may live on a
read-only fs; data (`rootfs/`, `home/`, `hostgl/`, `glvnd/`) resolves to
`$XDG_DATA_HOME/<click package name>` (`~/.local/share/sfrun.thekit`)
when installed as a click, or to the script dir when it already holds a
`rootfs/` (dev layout), or to `~/.local/share/sfrun` otherwise. Override with
`SFRUN_BASE` etc.

## Bring-up on a device

Inside a Lomiri session, on the device:

```sh
# 1. Populate the guest rootfs from a Sailfish SDK target. With no argument the
#    latest target is downloaded from releases.sailfishos.org (md5-verified,
#    resumable, cached in <base>/cache). Also accepts a version (5.1.0.11), a
#    local .tar/.tar.7z, or a dir. .tar.7z needs 7zz/7z on PATH.
python3 sfrun bootstrap

# 2. Build the curated host-GL stack (real copies of host libhybris GL libs,
#    eglplatform plugins, android linker shims, glvnd vendor JSON). Run once.
python3 sfrun prepare-gl

# 3. Patch Silica for Lomiri (disable Lipstick cover window; paint a blurred
#    in-window ambience instead of the compositor-drawn one). Idempotent.
python3 sfrun patch-silica

# 4. Preflight.
python3 sfrun doctor

# 5. Prove the stack: wl_shell + EGL on the real GPU, in the sandbox.
SFRUN_DEBUG=1 python3 sfrun spike
```

Apps that use Silica (`libsailfishapp`) need step 3. Keep
`QT_QPA_PLATFORM=wayland` (the default) — Silica's `desktop` mode misrenders
(white-on-white, wrong rotation).

After bootstrap, install the runtime packages the build sysroot lacks (qmlscene,
SVG image plugin, theme graphics + the zoom matching your scale) and generate
launchers:

```sh
python3 sfrun prepare-rootfs --scale 1.5   # runtime pkgs + z1.5 graphics
python3 sfrun set-scale 1.5                # Silica pixel ratio (user dconf)
python3 sfrun make-desktop harbour-fernschreiber   # -> UT app drawer entry
```

Code (script, `shim/`, `spike/`) and data (`rootfs/`, `home/`, `hostgl/`,
`glvnd/` under `SFRUN_BASE`) are decoupled: `SFRUN_BASE` defaults to the script
dir if it holds a `rootfs/`, else `$XDG_DATA_HOME/sfrun` — so the script can move
to e.g. `/opt/clickable` while data stays in the user's home.

**Pass:** a colour-cycling window appears and the log shows
`GLES renderer=<your GPU>` / `OK - full path works`.

Cross-compiling the test client (on an x86_64 dev host with
`aarch64-linux-gnu-gcc`): see the build line in `spike/SPIKE1-FINDINGS.md`.

## Running apps

```sh
python3 sfrun run harbour-someapp            # runs /usr/bin/harbour-someapp
python3 sfrun run /usr/bin/harbour-someapp   # absolute path also works
SFRUN_DEBUG=1 python3 sfrun run ...          # print the bwrap argv
```

Build the shim (optional until Spike 2): `make -C shim`
(cross: `make -C shim CC=aarch64-linux-gnu-gcc`).

## Maintenance / debugging

```sh
python3 sfrun manage [cmd]   # writable fake-root shell w/ net+DNS (zypper/rpm/patch),
                             # like sdk-assistant. Project dir at /sfhome.
python3 sfrun manage zypper in /sfhome/some.rpm   # install a local RPM into the rootfs
python3 sfrun shell  [cmd]   # shell in the app sandbox (read-only, full GL/Wayland env)
```

`bootstrap`/`prepare-gl`/`patch-silica` are built on the same `manage` sandbox.

```sh
python3 sfrun set-scale 1.5   # set Silica theme pixel ratio (user dconf); omit the
                              # number to derive it from the GRID_UNIT_PX env / 14
```

Apps share a single guest home (`SFRUN_HOME`, default `<base>/home`) mounted at
`/home/<device-user>` and run as the device user — like a real device, so dconf is
shared. SVG icons/emoji need `qt5-qtsvg-plugin-imageformat-svg`
(`sfrun manage zypper in qt5-qtsvg-plugin-imageformat-svg`).

## Packaging as a click

```sh
clickable build --arch arm64      # -> sfrun.thekit_0.1.0_arm64.click
```

Needs clickable >= 8.9 (earlier versions don't know the 24.04 frameworks and
write the wrong apparmor `policy_version`). The bundled
`click-review` step still fails on the new framework (outdated review DB in the
container) plus flags `unconfined` and the intentional `/opt/click.ubuntu.com`
self-detection string in `sfrun` — the package itself is fine; install with
`clickable install` or `pkcon install-local --allow-untrusted`.

The click (framework `ubuntu-touch-24.04-1.x`, apparmor `unconfined` — bwrap
needs user namespaces) ships only code: `sfrun`, the cross-built shim, and the
hooks from `click/`. It installs under
`/opt/click.ubuntu.com/sfrun.thekit/<version>/`; `sfrun` detects that
location and keeps all data in `~/.local/share/sfrun.thekit/`.
`make-desktop` launchers point at the package's `current` symlink, so they
survive upgrades. The desktop hook is `NoDisplay` — setup stays CLI-driven
(`bootstrap`/`prepare-gl`/`prepare-rootfs`/`make-desktop` from a terminal), and
apps get their own drawer entries via `make-desktop`.

## Next steps (per the plan)

2. **Silica hello** — the SDK *build sysroot* lacks runtime pieces (`qmlscene`,
   Silica QML, `jolla-ambient` theme). Install those runtime packages into the
   rootfs (or pull from a device image), run a minimal `ApplicationWindow`+`Page`,
   and observe the ambience-background artefact.
3. **Shim hardening** — land opaque-background + orientation in `sailfish_shim.c`;
   stub any mandatory missing global the registry diagnostics reveal.
4. **Real Harbour app**, then **packaging** as a click + per-app `.desktop`
   (click scaffolding now in `clickable.yaml` + `click/`; needs on-device
   validation).

## Env knobs

`SFRUN_BASE` (project base), `SFRUN_ROOTFS`, `SFRUN_HOSTGL`, `SFRUN_GLVND`,
`SFRUN_DATA`, `SFRUN_SHIM`, `SFRUN_HOST_TRIPLET` (host libhybris dir),
`SFRUN_ANDROID_SDK` (else auto via `getprop`), `SFRUN_DEBUG`,
`SFRUN_TARGETS_URL` (SDK target download base), `SFRUN_TARGET_ARCH`.
