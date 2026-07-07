# sfrun

Run unmodified SailfishOS apps on Ubuntu Touch (Lomiri/Mir).

## How it works

- A Sailfish SDK target rootfs is the guest userspace, run inside an unprivileged
  **bubblewrap** (`bwrap`) mount namespace. No container daemon (no LXC/podman/docker).
- The guest Qt 5.6 app talks **directly** to Lomiri's compositor over `wl_shell` —
  no nested compositor, no protocol-translating proxy.
- A small `LD_PRELOAD` shim fills the gaps Lipstick would normally provide
  (ambience background, orientation, missing globals).
- **GL:** the **SailfishOS build of libhybris** is installed *into the rootfs* and
  loads the device's own `/vendor` Android GL blobs. UT's own libhybris is a glvnd
  vendor, and glvnd returns a null `glGetString` on threads it doesn't track (e.g.
  Gecko's compositor thread), which breaks GPU-accelerated web content. The
  SailfishOS build is non-glvnd and exports the real EGL API, so WebView apps and
  the browser render on the GPU.

## Layout

| Path | What |
|------|------|
| `sfrun` | Python CLI: `doctor`, `bootstrap`, `prepare-gl`, `run`, … Encodes the validated bind/env recipe. |
| `shim/sailfish_shim.c` | `LD_PRELOAD` shim: registry diagnostics + (scaffolded) opaque background and orientation. |
| `clickable.yaml`, `click/` | Click packaging (manifest, apparmor, desktop hook, icon). |

**Code and data are decoupled** (the UT rootfs is read-only). Code (`sfrun`,
`shim/`) is found relative to the script, so it can live on a read-only fs or in
`/opt`. Data (`rootfs/`, `home/`, `cache/`) goes to `SFRUN_BASE`, which defaults
to the script dir if it already holds a `rootfs/` (dev layout), else
`~/.local/share/sfrun.thekit` when installed as a click, else `~/.local/share/sfrun`.

## Bring-up on a device

Run these inside a Lomiri session, on the device:

```sh
# 1. Fill the guest rootfs from a Sailfish SDK target. With no argument, the
#    latest is downloaded from releases.sailfishos.org (md5-checked, resumable,
#    cached). Also takes a version (5.1.0.11), a local .tar/.tar.7z, or a dir.
#    (.tar.7z needs 7zz/7z on PATH.)
python3 sfrun bootstrap

# 2. Install the SailfishOS libhybris stack into the rootfs (replaces the SDK
#    target's software GL). Run once.
python3 sfrun prepare-gl

# 3. Patch Silica for Lomiri (drop the Lipstick cover window; paint a blurred
#    in-window ambience instead). Idempotent.
python3 sfrun patch-silica

# 4. Check everything is ready.
python3 sfrun doctor
```

Apps that use Silica (`libsailfishapp`) need step 3. Keep
`QT_QPA_PLATFORM=wayland` (the default); Silica's `desktop` mode misrenders
(white-on-white, wrong rotation).

The SDK *build* sysroot lacks some runtime pieces, so after bootstrap install
them and generate launchers:

```sh
python3 sfrun prepare-rootfs --scale 1.5   # runtime pkgs + z1.5 theme graphics
python3 sfrun set-scale 1.5                # Silica pixel ratio (user dconf)
python3 sfrun make-desktop harbour-fernschreiber   # -> UT app drawer entry
```

## Installing apps

From the **SailfishOS:Chum** community repo (a zypper/OBS repo — no PackageKit or
ssu daemon needed; `sfrun` picks the newest chum release at or below the rootfs
version):

```sh
python3 sfrun chum enable                   # add + refresh the chum repo
python3 sfrun chum list                     # id / summary / installed, one per line
python3 sfrun install-pkg harbour-aenigma   # install (deps resolved)
```

Or from a local `.rpm` (deps still come from the repos):

```sh
python3 sfrun install-rpm ~/Downloads/harbour-foo.rpm
```

Installing an app auto-creates its drawer launcher. To remove an app plus its
launcher and icons:

```sh
python3 sfrun remove harbour-foo
```

The **Control panel** (below) wraps all of this in a GUI.

## Running apps

```sh
python3 sfrun run harbour-someapp            # launches per the app's .desktop Exec
python3 sfrun run /usr/bin/harbour-someapp   # absolute path also works
SFRUN_DEBUG=1 python3 sfrun run ...          # print the bwrap argv
```

`run <id>` follows the app's `.desktop` `Exec`, so both compiled apps
(`Exec=harbour-foo`) and QML-only apps (`Exec=sailfish-qml harbour-foo`) work; an
`invoker` booster prefix is stripped. Apps importing QML modules missing from the
rootfs (e.g. `Sailfish.WebView`, which needs the embedded Gecko engine) load to a
blank window — a missing dependency, not a render bug.

Apps share one guest home (`SFRUN_HOME`, default `<base>/home`) mounted at
`/home/<device-user>` and run as the device user, so dconf is shared — like a real
device.

## Maintenance / debugging

```sh
python3 sfrun manage [cmd]   # writable fake-root shell w/ net+DNS (zypper/rpm/patch).
                             #   Project dir at /sfhome. bootstrap/prepare-gl/
                             #   patch-silica are built on this.
python3 sfrun shell  [cmd]   # shell in the app sandbox (read-only, full GL/Wayland env)
python3 sfrun set-scale 1.5  # Silica theme pixel ratio (omit number to derive it)
```

Build the shim: `make -C shim`
(cross: `make -C shim CC=aarch64-linux-gnu-gcc`).

## Packaging as a click

```sh
clickable build --arch arm64      # -> sfrun.thekit_0.1.0_arm64.click
```

Needs clickable >= 8.9 (older versions don't know the 24.04 frameworks and write
the wrong apparmor `policy_version`). The click is `unconfined` because bwrap
needs user namespaces; the bundled `click-review` fails on the new framework
(outdated review DB) but the package is fine — install with `clickable install`
or `pkcon install-local --allow-untrusted`.

The click ships only code (`sfrun`, the cross-built shim, the `click/` hooks). It
installs under `/opt/click.ubuntu.com/sfrun.thekit/<version>/`; `sfrun` detects
that and keeps all data in `~/.local/share/sfrun.thekit/`. `make-desktop`
launchers point at the package's `current` symlink, so they survive upgrades.

### Control panel

The click's desktop entry launches a Lomiri/PyOtherSide QML app (`ui/Main.qml` +
`ui/backend.py`) that drives `sfrun` as a subprocess: a status/`doctor` card,
one-tap **Bootstrap** and **Setup**, a searchable **Chum store**, **Install from
.rpm**, and a **My apps** list. Everything it does is a plain `sfrun` subcommand,
so CLI and GUI stay in sync.

## Next steps

- **Shim hardening** — land opaque-background + orientation in `sailfish_shim.c`;
  stub any mandatory missing global the registry diagnostics reveal.
- **Real Harbour apps** at scale, then harden the click packaging with on-device
  validation.

## Env knobs

`SFRUN_BASE` (project base), `SFRUN_ROOTFS`, `SFRUN_HOME`, `SFRUN_SHIM`,
`SFRUN_ANDROID_SDK` (else auto via `getprop`), `SFRUN_DEBUG`,
`SFRUN_TARGETS_URL` (SDK target download base), `SFRUN_TARGET_ARCH`,
`SFRUN_LIBDIR` (guest libdir; default `lib64` for aarch64, `lib` for armhf),
`SFRUN_HALIUM_URL` (libhybris HW-adaptation OBS base),
`SFRUN_CHUM_URL` (SailfishOS:Chum OBS base).
