#!/bin/bash
#
# Ad-hoc re-signs a QEMU binary (or any program that uses Hypervisor.framework) with the
# com.apple.security.hypervisor entitlement. Without it macOS refuses hv_vm_create unless
# the program runs as root.
#
#   ./scripts/sign-qemu.sh [path-to-qemu-system-x86_64]
#   (default: the Homebrew one, $(brew --prefix)/bin/qemu-system-x86_64)
#
# Run it again after every `brew upgrade qemu` (the upgrade replaces the binary).

set -eu

QEMU="${1:-$(brew --prefix 2>/dev/null)/bin/qemu-system-x86_64}"
[ -x "$QEMU" ] || { echo "no executable at $QEMU" >&2; exit 2; }

ENT="$(mktemp)"
trap 'rm -f "$ENT"' EXIT
cat > "$ENT" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>com.apple.security.hypervisor</key><true/></dict></plist>
EOF

codesign --force --sign - --entitlements "$ENT" "$QEMU"
codesign --verify --verbose=1 "$QEMU" >/dev/null && echo "signed: $QEMU (ad hoc, com.apple.security.hypervisor)"
