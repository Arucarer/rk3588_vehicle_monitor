#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys
import tempfile

exe = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    for fault in ['acquisition-stall', 'diagnosis-stall', 'network-stall', 'storage-stall', 'storage-fail', 'sensor-dropout'] + (['gui-stall'] if sys.argv[2] == '1' else []):
        metrics = root / (fault + '.jsonl')
        args = [exe, '--simulate', '--duration', '5' if fault != 'sensor-dropout' else '1.5',
                '--metrics-file', str(metrics), '--inject', fault + ':300']
        if fault.startswith('storage-'):
            args += ['--log-file', str(root / (fault + '.nor')), '--sample-period-ms', '100']
        if fault == 'network-stall':
            args += ['--tcp', '127.0.0.1:1']
        if fault == 'gui-stall':
            args += ['--gui-headless']
        result = subprocess.run(args, capture_output=True, timeout=9)
        expected = 0 if fault == 'sensor-dropout' else 2
        assert result.returncode == expected, (fault, result.returncode, result.stdout, result.stderr)
        rows = [json.loads(line) for line in metrics.read_text().splitlines()]
        assert rows and all(row['hardware_watchdog_enabled'] is False for row in rows)
        if fault.endswith('stall'):
            assert any(row['unhealthy'] for row in rows), (fault, rows, result.stderr)
            assert b'HEALTH stalled=' in result.stderr
        if fault == 'sensor-dropout':
            assert b'fault=speed state=2' in result.stdout
            assert all(row['healthy'] for row in rows), rows
            assert rows[-1]['snapshots'] > rows[0]['snapshots']
    existing = root / 'existing.jsonl'
    existing.write_text('preserve')
    result = subprocess.run([exe, '--simulate', '--duration', '2', '--metrics-file', str(existing)], capture_output=True, timeout=5)
    assert result.returncode == 2 and existing.read_text() == 'preserve'
print('health gate, seven fault modes, cooperative exit and metrics protection passed')
