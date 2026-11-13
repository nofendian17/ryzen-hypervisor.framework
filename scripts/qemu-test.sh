#!/bin/bash
#
# The end-to-end test: boots a small Linux (Alpine's netboot kernel + initramfs) in stock
# QEMU with -accel hvf, NO shim, NO DYLD_INSERT_LIBRARIES - everything goes through the
# AMDHV kext's native Hypervisor.framework registration.
#
#   ./scripts/qemu-test.sh [path-to-qemu-system-x86_64]
#
# Before: sysctl kern.hv_support must print 1 (the kext sets it), and QEMU needs the
# hypervisor entitlement: run scripts/sign-qemu.sh on the binary first (or run this with sudo).
#
# Success looks like Linux kernel messages on the serial console and, at the end, "initramfs
# emergency recovery shell launched" and a "~ #" prompt (no root disk, no network, so Alpine's
# init gives up there). Leave QEMU with Ctrl-A then X.
#
# QEMU and the guest files live in ~/amdhv-qemu:
#   qemu-system-x86_64       (MacPorts: sudo port install qemu)
#   guest/vmlinuz-virt       Alpine netboot kernel
#   guest/initramfs-virt     Alpine netboot initramfs
# (download both from an Alpine "netboot" release, virt flavor)

set -eu
W="${AMDHV_QEMU_DIR:-$HOME/amdhv-qemu}"
QEMU="${1:-$W/qemu-system-x86_64}"
command -v "$QEMU" >/dev/null 2>&1 || [ -x "$QEMU" ] || { echo "no qemu at $QEMU (MacPorts: sudo port install qemu)" >&2; exit 1; }
[ -f "$W/guest/vmlinuz-virt" ] || { echo "no $W/guest/vmlinuz-virt - see the comment at the top of this script" >&2; exit 1; }

exec "$QEMU" -accel hvf -m 512 -smp 1 -nographic -no-reboot \
    -kernel "$W/guest/vmlinuz-virt" -initrd "$W/guest/initramfs-virt" \
    -append "console=ttyS0" ${AMDHV_QEMU_EXTRA:-}
