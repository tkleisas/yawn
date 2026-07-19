#!/usr/bin/env python3
"""Reference client for YAWN's UI command channel (YAWN_CMD=<port>).

Usage: python3 scripts/ui_probe.py <port> <command...>
Sends one command line to 127.0.0.1:<port>, prints the ack line, and
exits 0 when it starts with "OK", 1 otherwise.

  python3 scripts/ui_probe.py 8765 ping
  python3 scripts/ui_probe.py 8765 addinstrument 2 "FM Synth"
"""
import socket
import sys


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    port = int(sys.argv[1])
    line = " ".join(sys.argv[2:])
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
            sock.sendall(line.encode() + b"\n")
            ack = b""
            while not ack.endswith(b"\n"):
                chunk = sock.recv(4096)
                if not chunk:
                    break
                ack += chunk
    except OSError as exc:
        print(f"ERR {exc}")
        return 1
    text = ack.decode(errors="replace").strip()
    print(text)
    return 0 if text.startswith("OK") else 1


if __name__ == "__main__":
    sys.exit(main())
