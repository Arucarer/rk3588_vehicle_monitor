#!/usr/bin/env python3
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

source = pathlib.Path(__file__).resolve().parents[1]
build = pathlib.Path(sys.argv[1]).resolve()

def run(command, expected=0):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=30)
    assert result.returncode == expected, (command, result.returncode, result.stdout, result.stderr)
    return result

with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)
    packages = directory / 'packages'
    for label in ['first', 'second']:
        run([sys.executable, source / 'scripts/package.py', '--build-dir', build,
             '--output-dir', packages, '--label', label])
    first = next(p for p in packages.iterdir() if p.is_dir() and p.name.endswith('-first'))
    second = next(p for p in packages.iterdir() if p.is_dir() and p.name.endswith('-second'))
    manifest = json.loads((first / 'manifest.json').read_text())
    assert manifest['simulation_only'] and not manifest['hardware_acceptance']
    prefix = directory / 'application with spaces'
    install = source / 'deploy/install.sh'
    run([install, first, prefix])
    control = prefix / 'vmctl'
    run([control, 'start'], 1)
    (prefix / 'etc/enable-simulation').touch()
    (prefix / 'etc/arguments').write_text('--simulate\n--duration\n20\n')
    config = prefix / 'etc/vehicle.conf'
    config.write_text('overspeed_kmh=70\n')
    try:
        run([control, 'start'])
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            metrics = list((prefix / 'var').glob('metrics-*.jsonl'))
            if metrics and len(metrics[0].read_text().splitlines()) >= 2:
                break
            time.sleep(0.1)
        else:
            raise AssertionError((prefix / 'var/supervisor.log').read_text())
        run([control, 'status'])
        run([install, second, prefix], 1)
        run([control, 'stop'])
        run([control, 'status'], 1)
        run([install, second, prefix])
        assert (prefix / 'current').resolve().name.endswith('-second')
        assert config.read_text() == 'overspeed_kmh=70\n'
        run([control, 'rollback'])
        assert (prefix / 'current').resolve().name.endswith('-first')
        assert (prefix / 'var/events.nor').exists()
        corrupted = directory / 'corrupted'
        shutil.copytree(first, corrupted)
        with (corrupted / 'bin/vehicle-monitor').open('ab') as f:
            f.write(b'corrupt')
        run([install, corrupted, prefix], 1)
        assert (prefix / 'current').resolve().name.endswith('-first')
        (prefix / 'etc/arguments').write_text('--simulate\n--duration\n5\n--inject\nacquisition-stall:300\n')
        run([control, 'start'])
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            status = subprocess.run([control, 'status'], capture_output=True)
            if status.returncode != 0:
                break
            time.sleep(0.2)
        else:
            raise AssertionError('bounded supervisor did not stop after repeated failures')
        assert 'attempt=3/3' in (prefix / 'var/supervisor.log').read_text()
    finally:
        subprocess.run([control, 'stop'], capture_output=True, timeout=15)
print('package checksums, isolated install/start/stop/update/rollback and data preservation passed')
