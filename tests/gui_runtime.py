#!/usr/bin/env python3
import os
import pathlib
import subprocess
import sys
import tempfile
exe = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as directory:
    capture = pathlib.Path(directory) / 'live.ppm'
    result = subprocess.run([exe, '--simulate', '--can-simulate', '--duration', '1',
                             '--gui-headless', '--gui-capture', str(capture)],
                            capture_output=True, timeout=8)
    assert result.returncode == 0, (result.stdout, result.stderr)
    data = capture.read_bytes()
    assert data.startswith(b'P6\n240 320\n255\n') and len(data) > 230400
    assert len(set(data[16:])) > 16
    # SDL dummy video validates the host backend without requiring a visible desktop.
    env = dict(os.environ, SDL_VIDEODRIVER='dummy')
    result = subprocess.run([exe, '--simulate', '--duration', '0.2', '--gui'], env=env,
                            capture_output=True, timeout=8)
    # Builds without SDL reject interactive mode; headless path is always required.
    expected = 0 if len(sys.argv) > 2 and sys.argv[2] == "1" else 2
    assert result.returncode == expected, (result.stdout, result.stderr)
print('GUI runtime and capture checks passed')
