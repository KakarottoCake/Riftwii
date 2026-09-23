#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A small read-only RiiFS server for testing RiftWii's network packs.

Serves ROOT (a folder holding riivolution/*.xml and the packs' files) the
way docs/RIIFS.md describes, and answers the broadcast ping so the Wii can
find it. Writes are refused. For real use, any RiiFS server works.

    python tools/dolphin/riifs_server.py ROOT [--port 1137] [--verbose]
"""
import argparse
import os
import socket
import socketserver
import struct
import threading

OPT_HANDSHAKE, OPT_FILE, OPT_PATH, OPT_MODE, OPT_LENGTH = 0x00, 0x01, 0x02, 0x03, 0x04
S_IFDIR, S_IFREG = 0x4000, 0x8000
NAME_BYTES = 1024


def local_path(root, path):
    parts = [p for p in path.replace('\\', '/').split('/') if p not in ('', '.')]
    if '..' in parts:
        return None
    return os.path.join(root, *parts)


def stat_bytes(full):
    if full and os.path.isfile(full):
        return struct.pack('>QQII', 0, os.path.getsize(full), 0, S_IFREG), 0
    if full and os.path.isdir(full):
        return struct.pack('>QQII', 0, 0, 0, S_IFREG | S_IFDIR), 0
    return struct.pack('>QQII', 0, 0, 0, 0), -1


class Connection(socketserver.BaseRequestHandler):
    def recv_exact(self, n):
        data = b''
        while len(data) < n:
            chunk = self.request.recv(n - len(data))
            if not chunk:
                raise ConnectionError
            data += chunk
        return data

    def word(self):
        return struct.unpack('>I', self.recv_exact(4))[0]

    def reply(self, value, before=b''):
        self.request.sendall(before + struct.pack('>i', value))

    def log(self, text):
        if self.server.verbose:
            print('%s: %s' % (self.client_address[0], text), flush=True)

    def handle(self):
        options, files, dirs, next_fd = {}, {}, {}, [1]
        root = self.server.root

        def opt_word(o):
            v = options.get(o, b'')
            return struct.unpack('>I', v[:4])[0] if len(v) >= 4 else None

        def new_fd():
            next_fd[0] += 1
            return next_fd[0] - 1

        try:
            while True:
                action = self.word()
                if action == 1:
                    option, length = self.word(), self.word()
                    options[option] = self.recv_exact(length) if length else b''
                    continue
                if action != 2:
                    return
                command = self.word()
                path = options.get(OPT_PATH, b'').decode('utf-8', 'replace')
                if command == 0x00:
                    self.log('handshake %r' % options.get(OPT_HANDSHAKE))
                    self.reply(4 if options.get(OPT_HANDSHAKE) == b'1.03' else -1)
                elif command == 0x01:
                    self.reply(1)
                    return
                elif command == 0x02:
                    self.reply(1)
                elif command == 0x17:
                    data, rc = stat_bytes(local_path(root, path))
                    self.log('stat %s -> %d' % (path, rc))
                    self.reply(rc, data)
                elif command == 0x10:
                    full = local_path(root, path)
                    mode = opt_word(OPT_MODE) or 0
                    if mode & 0x3 or mode & 0x200 or not full or not os.path.isfile(full):
                        self.reply(-1)  # read-only server
                    else:
                        fd = new_fd()
                        files[fd] = open(full, 'rb')
                        self.log('open %s -> %d' % (path, fd))
                        self.reply(fd)
                elif command == 0x11:
                    fd, length = opt_word(OPT_FILE), opt_word(OPT_LENGTH) or 0
                    data = files[fd].read(length) if fd in files else b''
                    self.reply(len(data), data + b'\0' * (length - len(data)))
                elif command == 0x16:
                    fd = opt_word(OPT_FILE)
                    if fd in files:
                        files.pop(fd).close()
                        self.reply(1)
                    else:
                        self.reply(0)
                elif command == 0x21:
                    full = local_path(root, path)
                    if not full or not os.path.isdir(full):
                        self.reply(-1)
                    else:
                        fd = new_fd()
                        dirs[fd] = [['.', full], ['..', os.path.dirname(full)]] + \
                            [[n, os.path.join(full, n)] for n in sorted(os.listdir(full))]
                        self.log('list %s -> %d entries' % (path, len(dirs[fd]) - 2))
                        self.reply(fd)
                elif command == 0x22:
                    self.reply(1 if dirs.pop(opt_word(OPT_FILE), None) is not None else -1)
                elif command == 0x23:
                    entries = dirs.get(opt_word(OPT_FILE))
                    if not entries:
                        self.reply(-1, b'\0' * NAME_BYTES)
                    else:
                        name = entries[0][0].encode('utf-8')[:NAME_BYTES - 1]
                        self.reply(len(name), name + b'\0' * (NAME_BYTES - len(name)))
                elif command == 0x24:
                    entries = dirs.get(opt_word(OPT_FILE))
                    if not entries:
                        self.reply(1, stat_bytes(None)[0])
                    else:
                        data, _ = stat_bytes(entries.pop(0)[1])
                        self.reply(0, data)
                else:
                    self.log('refused command 0x%02x' % command)
                    self.reply(-1)
        except (ConnectionError, OSError):
            pass
        finally:
            for f in files.values():
                f.close()


def answer_pings(port):
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp.bind(('', port))
    while True:
        data, sender = udp.recvfrom(16)
        if len(data) >= 4 and struct.unpack('>I', data[:4])[0] == 0x10:
            print('ping from %s' % sender[0], flush=True)
            udp.sendto(struct.pack('>I', port), sender)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('root')
    parser.add_argument('--port', type=int, default=1137)
    parser.add_argument('--verbose', action='store_true')
    args = parser.parse_args()
    socketserver.ThreadingTCPServer.allow_reuse_address = True
    server = socketserver.ThreadingTCPServer(('', args.port), Connection)
    server.daemon_threads = True
    server.root = os.path.abspath(args.root)
    server.verbose = args.verbose
    threading.Thread(target=answer_pings, args=(args.port,), daemon=True).start()
    print('RiiFS test server: %s on port %d' % (server.root, args.port), flush=True)
    server.serve_forever()


if __name__ == '__main__':
    main()
