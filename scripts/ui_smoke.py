#!/usr/bin/env python3
"""End-to-end UI smoke test for YAWN's command channel (YAWN_CMD).

Launches the real application with the command channel enabled, drives
it over a single persistent TCP connection, and asserts state via the
`get` verbs. Exits 0 when every check passes, 1 otherwise (the app is
always terminated). Designed to run under `xvfb-run` on headless CI.

Usage: python3 scripts/ui_smoke.py [path/to/YAWN] [--port N]
"""
import os
import socket
import subprocess
import sys
import tempfile
import time

FAILURES = []


def check(name, ok, detail=""):
    status = "PASS" if ok else "FAIL"
    line = f"{status} {name}"
    if detail:
        line += f"  ({detail})"
    print(line, flush=True)
    if not ok:
        FAILURES.append(name)


class Channel:
    """One persistent connection to the YAWN_CMD server. Synchronous
    request/response per command (deferred verbs like shot/wait simply
    ack later — we always read exactly one line per command)."""

    def __init__(self, port, timeout=15.0):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        self.buf = b""

    def cmd(self, line):
        self.sock.sendall(line.encode() + b"\n")
        while b"\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError(f"app closed the channel on {line!r}")
            self.buf += chunk
        ack, self.buf = self.buf.split(b"\n", 1)
        return ack.decode(errors="replace").strip()

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main() -> int:
    args = sys.argv[1:]
    binary = args[0] if args and not args[0].startswith("-") else "build/bin/YAWN"
    port = free_port()
    if "--port" in args:
        port = int(args[args.index("--port") + 1])

    env = dict(os.environ, YAWN_CMD=str(port))
    proc = subprocess.Popen([binary], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        # Wait for the command channel (app startup takes a few seconds).
        ch = None
        deadline = time.time() + 30
        while time.time() < deadline:
            if proc.poll() is not None:
                print(f"FAIL app exited during startup (code {proc.returncode})")
                return 1
            try:
                ch = Channel(port)
                break
            except OSError:
                time.sleep(0.25)
        if ch is None:
            print("FAIL command channel never came up")
            return 1

        try:
            check("ping", ch.cmd("ping") == "OK pong")

            # Default project shape.
            check("default tracks", ch.cmd("get tracks") == "OK 5")
            check("default view", ch.cmd("get view") == "OK session")
            check("transport stopped", ch.cmd("get playing") == "OK 0")
            bpm = ch.cmd("get bpm")
            check("default bpm", bpm.startswith("OK 120"), bpm)

            # addtrack + queries.
            check("addtrack midi", ch.cmd("addtrack midi") == "OK addtrack")
            check("tracks after add", ch.cmd("get tracks") == "OK 6")
            check("new track type", ch.cmd("get tracktype 5") == "OK midi")
            check("new track instrument",
                  ch.cmd("get instrument 5") == "OK Subtractive Synth")

            # setparam/get param roundtrip.
            check("setparam", ch.cmd("setparam 5 0 0.25") == "OK setparam")
            got = ch.cmd("get param 5 0")
            ok = got.startswith("OK ") and abs(float(got[3:]) - 0.25) < 1e-4
            check("param roundtrip", ok, got)
            check("bad param errors", ch.cmd("get param 99 0").startswith("ERR"))

            # Undo / redo through the real app path.
            check("undo stack non-empty", ch.cmd("get undo").startswith("OK 1"))
            check("undo", ch.cmd("undo").startswith("OK undo"))
            check("tracks after undo", ch.cmd("get tracks") == "OK 5")
            check("redo", ch.cmd("redo").startswith("OK redo"))
            check("tracks after redo", ch.cmd("get tracks") == "OK 6")

            # View switch.
            check("view arrangement",
                  ch.cmd("view arrangement") == "OK view")
            check("view query", ch.cmd("get view") == "OK arrangement")
            ch.cmd("view session")

            # Frame-sync verb.
            check("wait", ch.cmd("wait 2") == "OK wait")

            # Screenshot to a fresh nested path (parent dirs are created).
            shot_path = os.path.join(tempfile.mkdtemp(), "deep", "ui.png")
            check("shot ack", ch.cmd(f"shot {shot_path}") == "OK shot")
            with open(shot_path, "rb") as f:
                magic = f.read(8)
            ok = (magic == b"\x89PNG\r\n\x1a\n"
                  and os.path.getsize(shot_path) > 10_000)
            check("shot file is a real PNG", ok,
                  f"{os.path.getsize(shot_path)} bytes")

            # Modal open/close via dialog verb + Escape.
            check("dialog preferences", ch.cmd("dialog preferences") == "OK dialog")
            check("modal open", ch.cmd("get modal") == "OK 1")
            ch.cmd("key Escape")
            check("modal closed by Escape", ch.cmd("get modal") == "OK 0")

            # Clean slate.
            check("new project", ch.cmd("new") == "OK new")
            check("tracks after new", ch.cmd("get tracks") == "OK 5")
            check("undo stack cleared", ch.cmd("get undo").startswith("OK 0"))

            check("quit", ch.cmd("quit 0") == "OK quit")
        finally:
            ch.close()

        rc = proc.wait(timeout=15)
        check("clean exit", rc == 0, f"exit code {rc}")
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()

    print()
    if FAILURES:
        print(f"SMOKE FAILED — {len(FAILURES)} failing check(s): "
              + ", ".join(FAILURES))
        return 1
    print("SMOKE OK — all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
