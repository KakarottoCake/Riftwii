# SPDX-License-Identifier: GPL-3.0-or-later
"""Connects to Dolphin's emulated USB Gecko (TCP 55020) and appends what the
DOL prints to a log file, echoing it to stdout. Retries until Dolphin is up."""
import socket
import sys
import time

path = sys.argv[1] if len(sys.argv) > 1 else "gecko.log"
deadline = time.time() + float(sys.argv[2]) if len(sys.argv) > 2 else None
with open(path, "ab", buffering=0) as log:
    while True:
        try:
            s = socket.create_connection(("127.0.0.1", 55020), timeout=2)
        except OSError:
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
