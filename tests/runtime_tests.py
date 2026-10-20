#!/usr/bin/env python3
"""Host-only process and configuration integration checks."""
import pathlib
import signal
import subprocess
import sys
import tempfile
import time

exe = str(pathlib.Path(sys.argv[1]).resolve())

def run(args, expected):
    result = subprocess.run([exe] + args, capture_output=True, timeout=5)
    assert result.returncode == expected, (args, result.stdout, result.stderr)

run([], 1)
run(['--help'], 0)
run(['--version'], 0)
for value in ['1abc', 'nan', '-1', '0', '86401']:
    run(['--simulate', '--duration', value], 1)
with tempfile.TemporaryDirectory() as directory:
    config = pathlib.Path(directory) / 'vehicle.conf'
    for content in ['stale_ms=-1\n', 'unknown=2\n', 'overspeed_kmh=nan\n',
                    'overspeed_kmh=80\noverspeed_kmh=90\n']:
        config.write_text(content)
        run(['--simulate', '--config', str(config)], 1)
    config.write_text('overspeed_kmh=80\n')
    run(['--simulate', '--duration', '0.1', '--config', str(config)], 0)
process = subprocess.Popen([exe, '--simulate', '--duration', '20'],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
try:
    time.sleep(0.3)
    process.send_signal(signal.SIGTERM)
    stdout, stderr = process.communicate(timeout=3)
    assert process.returncode == 0, (stdout, stderr)
    assert b'SIMULATION stopped' in stdout
finally:
    if process.poll() is None:
        process.kill()
        process.wait()
print('runtime checks passed')

# End-to-end synthetic snapshot -> concurrent diagnosis -> transition events.
result = subprocess.run([exe, '--simulate', '--duration', '6'],
                        capture_output=True, timeout=10)
assert result.returncode == 0, result.stderr
for fault in ['speed', 'tilt', 'bus', 'liquid']:
    assert ('fault=' + fault + ' state=1').encode() in result.stdout or \
           ('fault=' + fault + ' state=2').encode() in result.stdout, (fault, result.stdout)
    assert ('fault=' + fault + ' state=3').encode() in result.stdout, (fault, result.stdout)
print('concurrent synthetic fault and recovery checks passed')
