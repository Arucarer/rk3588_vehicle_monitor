#!/usr/bin/env python3
"""Send classic CAN heartbeat bench traffic. Interface must already be configured."""
import argparse
import socket
import struct
import time

def heartbeat(node, sequence, state=0):
    if not 1 <= node <= 126 or not 0 <= state <= 3:
        raise ValueError('invalid node/state')
    data = bytes([1, node, (sequence >> 8) & 255, sequence & 255, state, 0, 0, 0])
    return struct.pack('=IB3x8s', 0x700 + node, 8, data)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--interface', required=True)
    parser.add_argument('--duration', type=float, default=15)
    parser.add_argument('--period', type=float, default=1)
    parser.add_argument('--pause-node2-at', type=float, default=3)
    parser.add_argument('--resume-node2-at', type=float, default=7)
    args = parser.parse_args()
    if args.period <= 0 or args.duration <= 0:
        parser.error('period and duration must be positive')
    with socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW) as bus:
        bus.settimeout(0.5)
        bus.bind((args.interface,))
        start = time.monotonic()
        due = start
        sequence = 0
        while time.monotonic() - start < args.duration:
            now = time.monotonic()
            elapsed = now - start
            if now >= due:
                bus.send(heartbeat(1, sequence))
                if not args.pause_node2_at <= elapsed < args.resume_node2_at:
                    bus.send(heartbeat(2, sequence))
                sequence = (sequence + 1) & 0xffff
                due = now + args.period
            time.sleep(min(0.02, args.period))

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        pass
