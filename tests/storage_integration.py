#!/usr/bin/env python3
"""File NOR is software simulation, never a substitute for physical power-cut tests."""
import json
import pathlib
import subprocess
import sys
import tempfile

monitor, exporter = [str(pathlib.Path(p).resolve()) for p in sys.argv[1:]]
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    medium = root / 'events.nor'
    def collect(duration, destination):
        result = subprocess.run([monitor, '--simulate', '--duration', str(duration),
                                 '--log-file', str(medium), '--log-capacity', '64',
                                 '--sample-period-ms', '100'], capture_output=True, timeout=10)
        assert result.returncode == 0, (result.stdout, result.stderr)
        result = subprocess.run([exporter, str(medium), '64', str(destination)],
                                capture_output=True, timeout=5)
        assert result.returncode == 0, (result.stdout, result.stderr)
        return [json.loads(line) for line in destination.read_text().splitlines()]
    first = collect(4, root / 'first.jsonl')
    assert any(r['type'] == 2 and 'fault=speed' in r['payload'] for r in first)
    assert any(r['type'] == 1 and 'unit=km/h' in r['payload'] for r in first)
    assert all(r['boot_id'] and r['config_version'].startswith('v1-crc32:') for r in first)
    assert all(not r['clock_synchronized'] and r['software_version'] == '0.1.0' for r in first)
    last = first[-1]['sequence']
    second = collect(0.3, root / 'second.jsonl')
    assert second[-1]['sequence'] > last
    assert len({r['sequence'] for r in second}) == len(second)
    assert len(second) <= 64
    # Existing export/source cannot be accidentally overwritten.
    before = medium.read_bytes()
    result = subprocess.run([exporter, str(medium), '64', str(medium)], capture_output=True)
    assert result.returncode != 0 and medium.read_bytes() == before
    result = subprocess.run([exporter, str(medium), '32', str(root / 'wrong.jsonl')], capture_output=True)
    assert result.returncode != 0
print('file NOR application/restart/export integration passed')
