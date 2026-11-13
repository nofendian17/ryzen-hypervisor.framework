#!/bin/bash
#
# Copies build/AMDHV.kext into a mounted OpenCore EFI and adds the boot-args.
# Keeps a backup of config.plist the first time it edits it.
#
#   ./scripts/install-to-efi.sh /Volumes/EFI [--no-sysctl]
#
# Boot-args added: amdhv_enable_svm=1 amdhv_hv=1
#   amdhv_enable_svm=1  turn SVM on at boot (required)
#   amdhv_hv=1          register as the Hypervisor.framework provider (required for QEMU)
#   kern.hv_support is flipped to 1 by the kext itself once the traps are registered.
#
# The kext is loaded unsigned via OpenCore (Kernel -> Add). This script only adds it
# if the entry is not there yet; device properties/MinKernel are left alone.

set -eu

EFI="${1:-}"
[ -n "$EFI" ] || { echo "usage: $0 <mounted EFI volume> (the volume with EFI/OC in it)" >&2; exit 2; }
OC="$EFI/EFI/OC"
[ -d "$OC" ] || { echo "no EFI/OC under $EFI - is this the OpenCore EFI volume?" >&2; exit 2; }

HERE="$(cd "$(dirname "$0")/.." && pwd)"
KEXT="$HERE/build/AMDHV.kext"
[ -d "$KEXT" ] || { echo "build the kext first: make kext" >&2; exit 1; }

DEST="$OC/Kexts/AMDHV.kext"
mkdir -p "$OC/Kexts"
rm -rf "$DEST"
ditto "$KEXT" "$DEST"
echo "installed: $DEST"

PLIST="$OC/config.plist"
[ -f "$PLIST" ] || { echo "no config.plist under $OC" >&2; exit 1; }
[ -f "$PLIST.amdhv-backup" ] || cp "$PLIST" "$PLIST.amdhv-backup"

/usr/libexec/PlistBuddy -c "Print :Kernel:Add" "$PLIST" >/dev/null 2>&1 || {
    echo "config.plist has no Kernel -> Add array, add the kext entry by hand:" >&2
    echo "  BundlePath: AMDHV.kext  ExecutablePath: AMDHV.kext/Contents/MacOS/AMDHV  PlistPath: AMDHV.kext/Contents/Info.plist  Enabled: true" >&2
    exit 1
}

# Kernel -> Add entry (skip when it is already there).
if ! /usr/libexec/PlistBuddy -c "Print :Kernel:Add" "$PLIST" | grep -q "AMDHV.kext"; then
    N=$(/usr/libexec/PlistBuddy -c "Print :Kernel:Add" "$PLIST" | grep -c "BundlePath" || true)
    /usr/libexec/PlistBuddy -c "Add :Kernel:Add:$N dict" "$PLIST"
    /usr/libexec/PlistBuddy -c "Add :Kernel:Add:$N:BundlePath string AMDHV.kext" "$PLIST"
    /usr/libexec/PlistBuddy -c "Add :Kernel:Add:$N:Comment string AMDHV - Hypervisor.framework on AMD (QEMU)" "$PLIST"
    /usr/libexec/PlistBuddy -c "Add :Kernel:Add:$N:Enabled bool true" "$PLIST"
    /usr/libexec/PlistBuddy -c "Add :Kernel:Add:$N:ExecutablePath string AMDHV.kext/Contents/MacOS/AMDHV" "$PLIST"
    /usr/libexec/PlistBuddy -c "Add :Kernel:Add:$N:PlistPath string AMDHV.kext/Contents/Info.plist" "$PLIST"
    echo "added Kernel -> Add entry for AMDHV.kext"
else
    echo "Kernel -> Add already has AMDHV.kext"
fi

# Boot-args (append what is missing).
ARGS="amdhv_enable_svm=1 amdhv_hv=1"
CUR=$(/usr/libexec/PlistBuddy -c "Print :NVRAM:Add:7C436110-AB2A-4BBB-A880-FE41995C9F82:boot-args" "$PLIST" 2>/dev/null || echo "")
ADD=""
for a in $ARGS; do
    case " $CUR " in *" $a "*) ;; *) ADD="$ADD $a" ;; esac
done
if [ -n "$ADD" ]; then
    /usr/libexec/PlistBuddy -c "Set :NVRAM:Add:7C436110-AB2A-4BBB-A880-FE41995C9F82:boot-args $CUR$ADD" "$PLIST"
    echo "boot-args now:$CUR$ADD"
else
    echo "boot-args already complete"
fi

echo
echo "Next: reboot from this EFI, then check with build/amd-info (SVM active on every CPU)"
echo "and 'sysctl kern.hv_support' (must print 1). Keep a working EFI WITHOUT this kext."
