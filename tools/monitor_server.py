#!/usr/bin/env python3
"""Standard-library host endpoint for the VM v1 bench protocol."""
import argparse
import socket
import struct
import time
import zlib

MAX_PAYLOAD = 4096

def encode(command, sequence, payload=b''):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError('payload too large')
    body = struct.pack('!2sBBII', b'VM', 1, command, sequence, len(payload)) + payload
    return body + struct.pack('!I', zlib.crc32(body) & 0xffffffff)

class Decoder:
    def __init__(self):
        self.pending = bytearray()

    def feed(self, data):
        result = []
        for byte in data:
            self.pending.append(byte)
            while len(self.pending) >= 12:
                if self.pending[:3] != b'VM\x01':
                    del self.pending[0]
                    continue
                _, _, command, sequence, length = struct.unpack('!2sBBII', self.pending[:12])
                if length > MAX_PAYLOAD:
                    del self.pending[0]
                    continue
                total = length + 16
                if len(self.pending) < total:
                    break
                frame = bytes(self.pending[:total])
                if zlib.crc32(frame[:-4]) & 0xffffffff != struct.unpack('!I', frame[-4:])[0]:
                    del self.pending[0]
                    continue
                result.append((command, sequence, frame[12:-4]))
                del self.pending[:total]
        return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=9000)
    parser.add_argument('--duration', type=float, default=60)
    parser.add_argument('--set', dest='setting', help='Explicitly send one key=value update after connect')
    args = parser.parse_args()
    deadline = time.monotonic() + args.duration
    with socket.socket() as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.bind, args.port))
        server.listen(1)
        server.settimeout(0.5)
        print('Listening on {}:{}'.format(args.bind, args.port), flush=True)
        while time.monotonic() < deadline:
            try:
                connection, address = server.accept()
            except socket.timeout:
                continue
            with connection:
                connection.settimeout(0.5)
                decoder = Decoder()
                print('Connected', address, flush=True)
                connection.sendall(encode(0x20, 0x80000001))
                if args.setting:
                    connection.sendall(encode(0x21, 0x80000002, args.setting.encode('ascii')))
                try:
                    while time.monotonic() < deadline:
                        try:
                            data = connection.recv(8192)
                        except socket.timeout:
                            continue
                        if not data:
                            break
                        for command, sequence, payload in decoder.feed(data):
                            if command in (0x01, 0x10):
                                connection.sendall(encode(0x7e, sequence, bytes([command, 0])))
                            if command != 0x01:
                                print('command=0x{:02x} sequence={} {}'.format(
                                    command, sequence, payload.decode('utf-8', errors='replace')), flush=True)
                except (ConnectionError, OSError) as error:
                    print('Disconnected:', error, flush=True)

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        pass
