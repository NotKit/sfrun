"""PyOtherSide backend for the sfrun control panel.

Thin wrapper over the `sfrun` CLI: long-running operations are streamed to QML
line-by-line via pyotherside.send(event, opid, payload); queries return values
directly. Keeping the GUI a front-end over the same CLI the user runs by hand
means there is one code path to reason about.
"""
import glob
import os
import subprocess
import threading

import pyotherside

APPDIR = os.path.dirname(os.path.abspath(__file__))
SFRUN = os.path.join(APPDIR, "sfrun")
DATA_DIR = os.path.expanduser("~/.local/share/sfrun.thekit")
RPM_DIRS = [os.path.expanduser("~/Downloads"),
            os.path.expanduser("~"),
            os.path.join(DATA_DIR, "cache")]


def _argv(args):
    return ["python3", SFRUN] + list(args)


def _stream(opid, args):
    """Run an sfrun subcommand, forwarding each output line to QML. Download
    progress lines ('  57 / 187 MiB (30%)') are also surfaced as 'progress'."""
    rc = 1
    try:
        p = subprocess.Popen(_argv(args), stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, bufsize=1)
        for line in p.stdout:
            line = line.rstrip("\n")
            pyotherside.send("line", opid, line)
            if "MiB (" in line and "%)" in line:
                try:
                    pyotherside.send("progress", opid,
                                     int(line.split("(")[1].split("%")[0]))
                except (IndexError, ValueError):
                    pass
        rc = p.wait()
    except Exception as e:                       # noqa: BLE001 - report to UI
        pyotherside.send("line", opid, "error: %s" % e)
    pyotherside.send("done", opid, rc)


def _run_op(opid, args):
    threading.Thread(target=_stream, args=(opid, list(args)), daemon=True).start()


# --- streaming operations (UI shows a live log + progress) ---

def bootstrap(version):
    _run_op("bootstrap", ["bootstrap", version or "latest"])


def setup():
    _run_op("setup", ["setup"])


def install_rpm(path):
    _run_op("install", ["install-rpm", path])


def make_desktop(appid):
    _run_op("desktop", ["make-desktop", appid])


# --- fire-and-forget ---

def run_app(appid):
    subprocess.Popen(_argv(["run", appid]),
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return True


# --- synchronous queries (return a value to the QML callback) ---

def status():
    out = subprocess.run(_argv(["doctor"]), capture_output=True,
                         text=True).stdout
    rows = []
    for line in out.splitlines():
        if line.startswith("[OK ]"):
            rows.append({"ok": True, "label": line[5:].strip()})
        elif line.startswith("[FAIL]"):
            rows.append({"ok": False, "label": line[6:].strip()})
    return rows


def rootfs_ready():
    return any(r["ok"] and "guest rootfs" in r["label"] for r in status())


def list_apps():
    out = subprocess.run(_argv(["list-apps"]), capture_output=True,
                         text=True).stdout
    apps = []
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) == 3:
            apps.append({"id": parts[0], "name": parts[1],
                         "launcher": parts[2] == "1"})
    return apps


def list_rpms():
    seen, out = set(), []
    for d in RPM_DIRS:
        for f in sorted(glob.glob(os.path.join(d, "*.rpm"))):
            rp = os.path.realpath(f)
            if rp not in seen:
                seen.add(rp)
                out.append({"path": f, "name": os.path.basename(f)})
    return out
