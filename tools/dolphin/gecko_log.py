# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Connects to Dolphin's emulated USB Gecko (TCP 55020, or the next ports up
to 55023: Dolphin moves on when the first is still taken, as right after
another run) and appends what the DOL prints to a log file, echoing it to
stdout. Retries until Dolphin is up.

The callers start this before the Dolphin they want to log. A port that
already accepts a connection when this starts belongs to an older Dolphin
still running, so it is skipped: otherwise the log would hold that run's
output while the new Dolphin, moved up to the next port, goes unread."""
import socket
import sys
import time

PORTS = range(55020, 55024)


def listening_ports():
    found = set()
    for port in PORTS:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
            found.add(port)
        except OSError:
            pass
    return found


path = sys.argv[1] if len(sys.argv) > 1 else "gecko.log"
deadline = time.time() + float(sys.argv[2]) if len(sys.argv) > 2 else None
stale_ports = listening_ports()
with open(path, "ab", buffering=0) as log:
    while True:
        s = None
        for port in PORTS:
            if port in stale_ports:
                continue
            try:
                s = socket.create_connection(("127.0.0.1", port), timeout=2)
                break
            except OSError:
                s = None
        if s is None:
            if deadline and time.time() > deadline:
                sys.exit(0)
            time.sleep(0.5)
            continue
        s.settimeout(1.0)
        try:
            while True:
                try:
                    data = s.recv(4096)
                except socket.timeout:
                    if deadline and time.time() > deadline:
                        sys.exit(0)
                    continue
                if not data:
                    break
                log.write(data)
                sys.stdout.write(data.decode("latin-1"))
                sys.stdout.flush()
        finally:
            s.close()
        if deadline and time.time() > deadline:
            sys.exit(0)
        time.sleep(0.5)
