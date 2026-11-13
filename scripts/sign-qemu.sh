#!/bin/bash
#
# Ad-hoc re-signs a QEMU binary (or any program that uses Hypervisor.framework) with the
# com.apple.security.hypervisor entitlement. Without it macOS refuses hv_vm_create unless
# the program runs as root.
#
#   ./scripts/sign-qemu.sh [path-to-qemu-system-x86_64]
#   (default: the MacPorts one, /opt/local/bin/qemu-system-x86_64, then the Homebrew one)
#
# Run it again after every upgrade of QEMU (the upgrade replaces the binary).

set -eu

QEMU="${1:-}"
if [ -z "$QEMU" ]; then
    for c in /opt/local/bin/qemu-system-x86_64 "$(brew --prefix 2>/dev/null)/bin/qemu-system-x86_64"; do
        [ -x "$c" ] && QEMU="$c" && break
    done
fi
[ -n "$QEMU" ] && [ -x "$QEMU" ] || { echo "no qemu found (MacPorts: sudo port install qemu)" >&2; exit 2; }

ENT="$(mktemp)"
trap 'rm -f "$ENT"' EXIT
cat > "$ENT" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>com.apple.security.hypervisor</key><true/></dict></plist>
EOF

codesign --force --sign - --entitlements "$ENT" "$QEMU"
codesign --verify --verbose=1 "$QEMU" >/dev/null && echo "signed: $QEMU (ad hoc, com.apple.security.hypervisor)"
