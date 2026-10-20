#!/usr/bin/env python3
"""Combined software soak (simulated sensors/CAN/file NOR, real loopback TCP)."""
import argparse
import importlib.util
import json
import pathlib
import socket
import subprocess
import threading
import time

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('vm_protocol', root / 'tools/monitor_server.py')
protocol = importlib.util.module_from_spec(spec)
spec.loader.exec_module(protocol)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', default='build/vehicle-monitor')
parser.add_argument('--duration', type=int, default=60)
parser.add_argument('--output', required=True)
parser.add_argument('--no-gui', action='store_true')
args = parser.parse_args()
if not 2 <= args.duration <= 86400:
    parser.error('duration must be 2..86400 seconds')
out = pathlib.Path(args.output).resolve()
out.mkdir(parents=True, exist_ok=False)
exe = pathlib.Path(args.binary).resolve()
stop = threading.Event()
counts = {'heartbeat': 0, 'status': 0, 'connections': 0}
server = socket.socket()
server.bind(('127.0.0.1', 0))
server.listen(4)
server.settimeout(0.2)
port = server.getsockname()[1]

def serve():
    try:
        while not stop.is_set():
            try:
                connection, _ = server.accept()
            except socket.timeout:
                continue
            counts['connections'] += 1
            with connection:
                connection.settimeout(0.2)
                decoder = protocol.Decoder()
                while not stop.is_set():
                    try:
                        data = connection.recv(8192)
                        if not data:
                            break
                        for command, sequence, payload in decoder.feed(data):
                            if command in (0x01, 0x10):
                                counts['heartbeat' if command == 1 else 'status'] += 1
                                connection.sendall(protocol.encode(0x7e, sequence, bytes([command, 0])))
                    except socket.timeout:
                        continue
                    except ConnectionError:
                        break
    finally:
        server.close()

worker = threading.Thread(target=serve, daemon=True)
worker.start()
command = [str(exe), '--simulate', '--can-simulate', '--duration', str(args.duration),
           '--tcp', '127.0.0.1:' + str(port), '--metrics-file', str(out / 'metrics.jsonl'),
           '--log-file', str(out / 'events.nor'), '--log-capacity', '128', '--sample-period-ms', '100']
if not args.no_gui:
    command += ['--gui-headless', '--gui-capture', str(out / 'final.ppm')]
start = time.monotonic()
with (out / 'application.log').open('wb') as log:
    try:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=args.duration + 15)
    finally:
        stop.set()
        worker.join(timeout=3)
elapsed = time.monotonic() - start
rows = [json.loads(line) for line in (out / 'metrics.jsonl').read_text().splitlines()]
exporter = exe.parent / 'vehicle-log-export'
export = subprocess.run([str(exporter), str(out / 'events.nor'), '128', str(out / 'events.jsonl')], capture_output=True, text=True, timeout=10)
records = [json.loads(line) for line in (out / 'events.jsonl').read_text().splitlines()] if export.returncode == 0 else []
steady = [row for row in rows if row['elapsed_ms'] >= min(5000, args.duration * 1000 // 3)]
rss = [row['rss_kb'] for row in steady]
summary = {
    'simulation': True, 'hardware_acceptance': False, 'elapsed_seconds': elapsed,
    'requested_seconds': args.duration, 'exit_code': result.returncode,
    'software_version': rows[-1]['software_version'], 'metrics_samples': len(rows),
    'all_health_checks_passed': all(row['healthy'] for row in rows),
    'rss_min_kb': min(rss), 'rss_max_kb': max(rss), 'rss_growth_kb': rss[-1] - rss[0],
    'cpu_percent_one_core': (rows[-1]['cpu_seconds'] - rows[0]['cpu_seconds']) / max(elapsed, 1) * 100,
    'log_pending_max': max(row['log_pending'] for row in rows),
    'critical_dropped': rows[-1]['critical_dropped'], 'samples_dropped': rows[-1]['samples_dropped'],
    'tcp': counts, 'retained_records': len(records),
    'first_sequence': records[0]['sequence'] if records else None,
    'last_sequence': records[-1]['sequence'] if records else None,
    'export_exit_code': export.returncode,
}
summary['passed'] = (result.returncode == 0 and summary['all_health_checks_passed'] and
                     summary['critical_dropped'] == 0 and summary['samples_dropped'] == 0 and
                     counts['status'] >= max(1, args.duration // 2) and export.returncode == 0 and bool(records))
(out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
raise SystemExit(0 if summary['passed'] else 1)
