# AMDHV

Run QEMU with `-accel hvf` on a **Hackintosh with an AMD CPU**.

AMDHV is a kernel extension. At boot it registers with macOS **in place of Apple's AppleHV**, so the
unmodified Hypervisor.framework talks to it. The hardware work is done by AMD-V (SVM).
It is a simplified rewrite of the RyzenHV project: native mode only, no shim, no client patches.

> **Experimental.** Test on a USB stick with its own OpenCore EFI first. Keep a working EFI **without**
> this kext. A kernel bug is a panic.

## Requirements

- "SVM Mode" **enabled in the BIOS**.
- Xcode command line tools (`xcode-select --install`).
- OpenCore.
- QEMU (MacPorts: `sudo port install qemu`).

## Build

```bash
make test          # 8 unit tests, runs anywhere
make kext tools    # build/AMDHV.kext, build/amd-info, build/hvf-smoke
```

Note: the kext Makefile does not track header changes. After editing a `.hpp`, run `rm -rf build/kext-obj` first.

## Install

1. Copy the kext into OpenCore and set the two needed boot-args (keeps a `config.plist.amdhv-backup`):
   ```bash
   ./scripts/install-to-efi.sh /Volumes/EFI
   ```
2. Reboot.
3. Check:
   ```bash
   sysctl kern.hv_support        # must print 1 (the kext sets it automatically)
   sudo build/amd-info           # SVM must be "active on" every CPU
   sudo build/amd-info selftest  # verifies SVM state on every CPU
   ```

## Run a VM

```bash
./scripts/sign-qemu.sh           # gives QEMU the hypervisor entitlement (again after every upgrade)
./scripts/qemu-test.sh           # boots a test Linux — everything is working
qemu-system-x86_64 -accel hvf -cpu host -smp 4 -m 8G -drive file=win10.qcow2,if=virtio
```

### colima / Docker

- Use the **qemu** backend: `colima start --vm-type qemu`. The `vz` backend does not work on AMD.
- Start colima **after** boot, when `kern.hv_support` is already 1 — Lima picks `-accel hvf` at start.
- MacPorts QEMU already has the hypervisor entitlement — check it again after a `port upgrade qemu`.

## Boot-args

Only two are needed — exactly what `install-to-efi.sh` adds:

```
amdhv_enable_svm=1 amdhv_hv=1
```

Everything else is on by default or diagnostics only. Do not add more unless troubleshooting.

| boot-arg | effect |
|---|---|
| `amdhv_enable_svm=1` | **required** — turns SVM on |
| `amdhv_hv=1` | **required** — registers the Hypervisor.framework trap tables |
| `amdhv_hv_sysctl=0` | optional — leaves `kern.hv_support` alone (default: set it to 1) |
| `amdhv_tpr_shadow=0` | optional — hides the TPR shadow from guests (default: offered) |
| `amdhv_selftest=1` | diagnostics — verify SVM state after enable and after wake |
| `amdhv_loglevel=N` / `amdhv_iolog=N` | diagnostics — log levels, also settable with `amd-info level N` |
| `-amdhv_off` | **emergency** — kext loads but does nothing |

## Troubleshooting

```bash
sysctl kern.hv_support          # 1? if not: see boot-args above
sudo build/amd-info stats       # vmRuns grows while a VM runs; vmFpuMismatches must stay 0
sudo build/amd-info log -f      # live kernel log
```

If a RyzenHV kext is still installed, remove it from `EFI/OC/Kexts` and its Kernel → Add entry first:
the two kexts fight over SVM and only one can register as the hypervisor provider.

## Limits

- 16 vCPUs per VM. One VM program at a time is safest.
- Sleep/wake with a running VM is untested — shut VMs down first.
- No entitlement check in native mode: any local process can create a VM. Not for machines with
  untrusted local users.
- Tested hardware: Ryzen 7 5800H and Zen 2, macOS Sequoia. Re-test on yours with `hvf-smoke` and
  `qemu-test.sh`.

## Sources

```
kext/          the kext: SVM, VMCB, VMRUN, nested page tables, the Hypervisor.framework front end
include/       kext ⇄ tools ABI, log ring, VMCS field rules
tools/         amd-info (the CLI above), hvf-smoke (public-API acceptance test)
tests/         the 8 unit tests
scripts/       EFI install, QEMU signing, QEMU boot test, symbol check
```

MIT license, see `LICENSE`. Ported from RyzenHV (MIT) — keep that copyright when redistributing.
