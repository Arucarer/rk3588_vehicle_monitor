#!/usr/bin/env python3
"""Loopback TCP and explicitly simulated CAN: no physical CAN claims."""
import importlib.util
import pathlib
import signal
import re
import socket
import subprocess
import sys
import tempfile
import time

module_path = pathlib.Path(__file__).resolve().parents[1] / 'tools' / 'monitor_server.py'
spec = importlib.util.spec_from_file_location('monitor_server', module_path)
protocol = importlib.util.module_from_spec(spec)
spec.loader.exec_module(protocol)
monitor = str(pathlib.Path(sys.argv[1]).resolve())

with tempfile.TemporaryDirectory() as directory:
    config = pathlib.Path(directory) / 'vehicle.conf'
    config.write_text('overspeed_kmh=80\n')
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', 0))
        listener.listen(4)
        listener.settimeout(5)
        port = listener.getsockname()[1]
        process = subprocess.Popen([monitor, '--simulate', '--duration', '25', '--can-simulate',
                                    '--tcp', '127.0.0.1:' + str(port),
                                    '--config', str(config), '--allow-remote-set'],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        connection = None
        try:
            connection, _ = listener.accept()
            connection.settimeout(0.1)
            decoder = protocol.Decoder()
            responses = {}
            reports = []
            def pump(until, timeout=5, acknowledge=True):
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    if until():
                        return
                    try:
                        data = connection.recv(8192)
                    except socket.timeout:
                        continue
                    if not data:
                        if until():
                            return
                        raise EOFError('client disconnected')
                    for command, sequence, payload in decoder.feed(data):
                        if command in (0x01, 0x10):
                            if command == 0x10:
                                reports.append(payload)
                            if acknowledge:
                                connection.sendall(protocol.encode(0x7e, sequence, bytes([command, 0])))
                        else:
                            responses[sequence] = (command, payload)
                assert until(), ('timeout', responses, reports[-1:] if reports else [])

            first = protocol.encode(0x20, 1001)
            connection.sendall(first[:3])
            connection.sendall(first[3:11])
            connection.sendall(first[11:])
            bad = bytearray(protocol.encode(0x20, 1100))
            bad[-1] ^= 1
            illegal_length = b'VM\x01\x20' + (1101).to_bytes(4, 'big') + (5000).to_bytes(4, 'big')
            connection.sendall(bytes(bad) + illegal_length +
                               protocol.encode(0x99, 1002) +
                               protocol.encode(0x20, 1003, b'bad') +
                               protocol.encode(0x21, 1004, b'overspeed_kmh=50') +
                               protocol.encode(0x21, 1005, b'overspeed_kmh=999') +
                               protocol.encode(0x20, 1006) + protocol.encode(0x10, 1007))
            pump(lambda: all(key in responses for key in range(1001, 1008)))
            assert responses[1001][0] == 0x7e
            assert responses[1002] == (0x7f, b'\x99\x01')
            assert responses[1003] == (0x7f, b'\x20\x02')
            assert responses[1004][0] == 0x7e and responses[1004][1][:2] == b'\x21\x00'
            assert responses[1005] == (0x7f, b'\x21\x02')
            assert b'overspeed_kmh=50' in responses[1006][1]
            assert b'simulation=1' in responses[1007][1]
            assert 1100 not in responses and 1101 not in responses
            assert 'overspeed_kmh=50\n' in config.read_text()
            expected = responses.pop(1004)
            connection.sendall(protocol.encode(0x21, 1004, b'overspeed_kmh=50'))
            pump(lambda: 1004 in responses)
            assert responses[1004] == expected
            responses.pop(1004)
            connection.sendall(protocol.encode(0x20, 1004))
            pump(lambda: 1004 in responses)
            assert responses[1004] == (0x7f, b'\x20\x05')
            # Explicit peer close: reconnect to a fresh session without losing persisted config.
            connection.close()
            connection, _ = listener.accept()
            connection.settimeout(0.1)
            decoder = protocol.Decoder()
            responses.clear()
            connection.sendall(protocol.encode(0x20, 1001))
            pump(lambda: 1001 in responses)
            assert b'overspeed_kmh=50' in responses[1001][1]
            # Wrong command in ACK must not release a pending report.
            connection.sendall(protocol.encode(0x7e, 0, b'\x20\x00'))
            # A connected peer that never ACKs must also be detected and reconnected.
            closed = False
            deadline = time.monotonic() + 6
            while time.monotonic() < deadline:
                try:
                    if not connection.recv(8192):
                        closed = True
                        break
                except socket.timeout:
                    continue
            assert closed, 'missing ACK did not trigger disconnect'
            connection.close()
            connection, _ = listener.accept()
            connection.settimeout(0.1)
            decoder = protocol.Decoder()
            reports.clear()
            pump(lambda: any(b'digital.node.2=0,valid=1' in report for report in reports))
            pump(lambda: any(b'digital.node.2=1,valid=1' in report for report in reports), timeout=5)
            assert all(b'digital.node.1=0,valid=1' not in report for report in reports)
            # Runtime threshold takes effect and is advertised with current parameters.
            assert any(b'overspeed_kmh=50' in report for report in reports)
            process.send_signal(signal.SIGTERM)
            stdout, stderr = process.communicate(timeout=4)
            assert process.returncode == 0, (stdout, stderr)
            assert b'fault=node.2 state=2' in stdout, stdout
            assert b'fault=node.2 state=3' in stdout, stdout
            assert b'fault=node.1 state=2' not in stdout, stdout
            assert b'fault=speed state=1' in stdout, stdout
            connections = re.search(rb'tcp_connections=(\d+)', stdout)
            assert connections and int(connections.group(1)) >= 3, stdout
        finally:
            if connection is not None:
                connection.close()
            if process.poll() is None:
                process.kill()
                process.communicate()
    # Writes remain denied unless explicitly enabled, even with an existing config path.
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(4)
        process = subprocess.Popen([monitor, '--simulate', '--duration', '3',
                                    '--tcp', '127.0.0.1:' + str(listener.getsockname()[1]),
                                    '--config', str(config)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(3)
                connection.sendall(protocol.encode(0x21, 9001, b'overspeed_kmh=70'))
                decoder = protocol.Decoder()
                denied = False
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline and not denied:
                    for command, sequence, payload in decoder.feed(connection.recv(8192)):
                        if sequence == 9001:
                            denied = command == 0x7f and payload == b'\x21\x03'
                assert denied
            process.send_signal(signal.SIGTERM)
            process.communicate(timeout=4)
            assert process.returncode == 0
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
print('TCP fragmentation/CRC/commands/persistence/reconnect and dual-node simulation passed')
