#!/usr/bin/env python3
"""Build and package application artifacts; does not deploy or start services."""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import tarfile

root = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', default='build-rk')
parser.add_argument('--output-dir', default='out')
parser.add_argument('--label', default='software')
args = parser.parse_args()
if not re.fullmatch(r'[a-zA-Z0-9_.-]{1,30}', args.label):
    parser.error('invalid release label')
build = pathlib.Path(args.build_dir).resolve()
subprocess.run(['cmake', '--build', str(build), '-j2'], check=True)
version = json.loads((build / 'version.json').read_text())
release = version['version'] + '-' + args.label
output = pathlib.Path(args.output_dir).resolve()
output.mkdir(parents=True, exist_ok=True)
package = output / release
if package.exists() or (output / (release + '.tar.gz')).exists():
    raise SystemExit('Output exists; use another label or output directory')
subprocess.run(['cmake', '--install', str(build), '--prefix', str(package)], check=True)
shutil.copytree(root / 'deploy', package / 'deploy')
(package / 'release-id').write_text(release + '\n')
manifest = dict(version, release_id=release, simulation_only=True, hardware_acceptance=False)
(package / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
checksums = []
for path in sorted(package.rglob('*')):
    if path.is_file():
        checksums.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.relative_to(package).as_posix())
(package / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n')
with tarfile.open(output / (release + '.tar.gz'), 'w:gz') as archive:
    archive.add(package, arcname=release)
print('PACKAGE=' + str(package))
