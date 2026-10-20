#!/bin/sh
# Install a locally prepared, checksum-verified package. Never enables boot services.
set -eu
umask 077
[ "$#" -eq 2 ] || { printf 'usage: install.sh EXTRACTED_PACKAGE APPLICATION_ROOT\n' >&2; exit 1; }
VM_PACKAGE=$(CDPATH= cd -- "$1" && pwd)
mkdir -p "$2"
VM_ROOT=$(CDPATH= cd -- "$2" && pwd)
[ "$VM_ROOT" != / ] || exit 1
(cd "$VM_PACKAGE" && sha256sum -c SHA256SUMS >/dev/null)
read -r VM_RELEASE < "$VM_PACKAGE/release-id"
case "$VM_RELEASE" in ''|*[!a-zA-Z0-9+_.-]*) printf 'invalid release id\n' >&2; exit 1;; esac
if [ -x "$VM_ROOT/vmctl" ] && "$VM_ROOT/vmctl" status >/dev/null 2>&1; then printf 'stop the application before install\n' >&2; exit 1; fi
mkdir -p "$VM_ROOT/releases" "$VM_ROOT/etc" "$VM_ROOT/var"
[ ! -e "$VM_ROOT/releases/$VM_RELEASE" ] || { printf 'release already installed\n' >&2; exit 1; }
VM_STAGE="$VM_ROOT/releases/.staging-$VM_RELEASE-$$"
trap 'rm -rf "$VM_STAGE"' EXIT HUP INT TERM
mkdir "$VM_STAGE"
cp -R "$VM_PACKAGE/." "$VM_STAGE/"
mv "$VM_STAGE" "$VM_ROOT/releases/$VM_RELEASE"
if [ ! -e "$VM_ROOT/etc/vehicle.conf" ]; then cp "$VM_PACKAGE/etc/vehicle-monitor/vehicle.conf" "$VM_ROOT/etc/vehicle.conf"; fi
if [ ! -e "$VM_ROOT/etc/arguments" ]; then cp "$VM_PACKAGE/deploy/simulation.args" "$VM_ROOT/etc/arguments"; fi
cp "$VM_PACKAGE/deploy/vmctl" "$VM_ROOT/vmctl"
chmod 700 "$VM_ROOT/vmctl"
if [ -L "$VM_ROOT/current" ]; then ln -sfn "$(readlink "$VM_ROOT/current")" "$VM_ROOT/previous"; fi
ln -sfn "releases/$VM_RELEASE" "$VM_ROOT/current"
printf 'installed %s (simulation disabled until explicit opt-in)\n' "$VM_RELEASE"
