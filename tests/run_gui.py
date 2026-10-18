#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run GTK tests on a private X server, never on the user's desktop."""

import argparse
import os
from pathlib import Path
import selectors
import signal
import socket
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--screenshots", type=Path)
    args = parser.parse_args()
    xvfb = shutil.which(os.environ.get("XVFB", "Xvfb"))
    dbus = shutil.which("dbus-run-session")
    if not xvfb or not dbus:
        parser.exit(2, "GUI tests need Xvfb and dbus-run-session. "
                    "Install xvfb and dbus-daemon, or set XVFB to an Xvfb executable.\n")

    repo = Path(__file__).resolve().parent.parent
    command = [dbus, "--", str(repo / "test-gui")]
    if args.screenshots:
        output = args.screenshots.resolve()
        output.mkdir(parents=True, exist_ok=True)
        command += ["--screenshots", str(output)]

    with tempfile.TemporaryDirectory(prefix="sysribbon-gui-") as directory:
        scratch = Path(directory)
        env = os.environ.copy()
        # Avoid the desktop's settings, session bus, X authority and accessibility bus.
        for name in ("DBUS_SESSION_BUS_ADDRESS", "SESSION_MANAGER", "XAUTHORITY",
                     "DBUS_STARTER_ADDRESS", "DBUS_STARTER_BUS_TYPE",
                     "GTK_MODULES", "GTK3_MODULES"):
            env.pop(name, None)
        for name in ("CONFIG", "CACHE", "DATA", "RUNTIME"):
            target = scratch / name.lower()
            target.mkdir(mode=0o700)
            env[f"XDG_{name}_HOME" if name != "RUNTIME" else "XDG_RUNTIME_DIR"] = str(target)
        env.update(GDK_BACKEND="x11", GDK_SCALE="1", GDK_DPI_SCALE="1",
                   GTK_THEME="Adwaita:dark", LC_ALL="C.UTF-8", NO_AT_BRIDGE="1",
                   GIO_USE_VFS="local")
        env["XDG_CONFIG_DIRS"] = env["XDG_CONFIG_HOME"]
        env["SYSRIBBON_GUI_TEST_ROOT"] = directory

        # Give sandbox failures a useful error before Xvfb tries thousands of displays.
        try:
            probe_path = scratch / "socket-probe"
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
                probe.bind(str(probe_path))
                probe.listen(1)
            probe_path.unlink()
        except OSError as error:
            print(f"GUI test runner: local Unix sockets are unavailable ({error}). "
                  "Xvfb and the private D-Bus session require permission to open them.",
                  file=sys.stderr)
            return 1

        read_fd, write_fd = os.pipe()
        server = None
        try:
            with (scratch / "xvfb.log").open("w+") as log:
                server = subprocess.Popen(
                    [xvfb, "-displayfd", str(write_fd), "-screen", "0", "1280x1024x24",
                     "-dpi", "96", "-nolisten", "tcp", "-noreset"],
                    pass_fds=(write_fd,), stdout=log, stderr=log, env=env)
                os.close(write_fd)
                write_fd = None
                with selectors.DefaultSelector() as selector:
                    selector.register(read_fd, selectors.EVENT_READ)
                    if not selector.select(timeout=10):
                        raise RuntimeError("Xvfb did not become ready within 10 seconds")
                display = os.read(read_fd, 64).decode("ascii").strip()
                if not display.isdecimal():
                    raise RuntimeError("Xvfb exited without allocating a display")
                env["DISPLAY"] = f":{display}"
                process = subprocess.Popen(command, cwd=repo, env=env, start_new_session=True)
                try:
                    return process.wait(timeout=90)
                except subprocess.TimeoutExpired as error:
                    raise RuntimeError("GUI tests exceeded their 90-second limit") from error
                finally:
                    # Clean up the private bus and any children, including on assertion failure.
                    try:
                        os.killpg(process.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    if process.poll() is None:
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            os.killpg(process.pid, signal.SIGKILL)
                            process.wait()
        except (OSError, RuntimeError) as error:
            print(f"GUI test runner: {error}", file=sys.stderr)
            print((scratch / "xvfb.log").read_text()[-4000:], file=sys.stderr)
            return 1
        finally:
            os.close(read_fd)
            if write_fd is not None:
                os.close(write_fd)
            if server is not None:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()


def terminate(signum, _frame):
    # Unwind through the child-process and temporary-directory cleanup blocks.
    raise SystemExit(128 + signum)


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, terminate)
    sys.exit(main())
