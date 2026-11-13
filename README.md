# AMDHV

QEMU (and anything else that uses Apple's Hypervisor.framework) on a **Hackintosh with an AMD Ryzen**.

On an Intel Mac, QEMU with `-accel hvf` runs on Apple's Hypervisor.framework, which is built on Intel's VT-x.
On a Ryzen it reports "not supported" (`sysctl kern.hv_support` is 0) and QEMU falls back to TCG. AMDHV is a
kernel extension that registers itself with the kernel **in the place of Apple's AppleHV** (native mode), so
the **unmodified Hypervisor.framework** talks to it: no injected library, no re-signed app files, no wrappers.
The hardware work is done by AMD-V (SVM): VMRUN, VMCB, nested page tables.

It is a deliberately small rewrite of the RyzenHV project: same proven core (the VMCS-on-VMCB
translation, the nested page tables, the run loop with its FPU/DR/TSC-AUX state handling and the per-CPU SVM
self-heal), but **only the native mode** — the dyld shim, the Parallels/Docker client patches and the app-specific
CPUID fixups are gone. ~7.3k lines of kext instead of ~13k plus a 3.9k-line shim.

> **This is an experimental project.** Test it on a USB stick with its own copy of your OpenCore EFI first, and
> always keep a working EFI **without** the kext. A mistake in kernel code is a panic.

## What you need

- **SVM Mode switched on in the BIOS.**
- Xcode command line tools (`xcode-select --install`) to build.
- OpenCore (the kext is loaded unsigned via Kernel → Add).
- QEMU 7+ (`brew install qemu`), re-signed once with the hypervisor entitlement (below).

## Build

```bash
make test                # pure-logic unit tests, run anywhere (VMCS↔VMCB translation, NPT, MSR maps, FPU)
make kext tools          # build/AMDHV.kext, build/amd-info, build/hvf-smoke
```

Note: the kext Makefile does not track header dependencies — after changing a `.hpp`, `rm -rf build/kext-obj`
first (or `make clean`) so every user of the header is recompiled.

## Install

1. Put the kext into your OpenCore EFI (use a **test EFI** first). This also adds the boot-args and keeps a
   backup of `config.plist`:
   ```bash
   ./scripts/install-to-efi.sh /Volumes/EFI
   ```
2. Boot from that EFI. Check:
   ```bash
   sudo build/amd-info          # CPU / SVM summary, SVM must be "active on" every CPU
   sudo build/amd-info selftest # verifies EFER.SVME / VM_HSAVE_PA on every CPU
   sysctl kern.hv_support       # must print 1 (the kext sets it once its traps are registered)
   ```
3. Acceptance test of the public API — the same calls QEMU makes, in the same order:
   ```bash
   sudo build/hvf-smoke
   ```
4. Boot a real guest through QEMU with **no shim and no patched QEMU**:
   ```bash
   ./scripts/sign-qemu.sh                       # ad-hoc re-sign QEMU with com.apple.security.hypervisor
   ./scripts/qemu-test.sh                       # boots Alpine's netboot kernel on the serial console
   ```
5. Your own VM:
   ```bash
   qemu-system-x86_64 -accel hvf -cpu host -smp 4 -m 8G -drive file=win10.qcow2,if=virtio ...
   ```

## Boot-args

For QEMU, the minimal set — exactly what `scripts/install-to-efi.sh` adds — is:

```
amdhv_enable_svm=1 amdhv_hv=1
```

Everything else below is default-on (the boot-arg only turns it **off**) or diagnostics-only. Do not add more
to `boot-args` unless you are troubleshooting.

| boot-arg | effect |
|---|---|
| `amdhv_enable_svm=1` | **required** — turn SVM on (EFER.SVME + VM_HSAVE_PA on every CPU); without it the kext only probes and VM creation fails |
| `amdhv_hv=1` | **required** — register the Hypervisor.framework trap tables; without it `hv_vm_create` is answered by nothing |
| `amdhv_hv_sysctl` | flip `kern.hv_support` to 1 after registering — **on by default**; only give `amdhv_hv_sysctl=0` to leave the sysctl alone (`amd-info hv sysctl 1` turns it back on at run time) |
| `amdhv_tpr_shadow` | offer the VMX "use TPR shadow" control to guests — **on by default** (Windows boots much faster with it); only give `amdhv_tpr_shadow=0` if a guest misbehaves with CR8 |
| `amdhv_selftest=1` | diagnostics — verify EFER/VM_HSAVE_PA on every CPU after enabling and after wake (also `sudo amd-info selftest` any time) |
| `amdhv_loglevel=N` / `amdhv_iolog=N` | diagnostics — kernel log levels (also `amd-info level N` at run time) |
| `-amdhv_off` | **emergency only** — kill switch: the kext loads but does nothing; never leave it in a working config |

No AMFI boot-arg is needed or wanted: AMDHV never touches the client process, so AMFI stays fully intact
(unlike RyzenHV's shim mode, which needed `amfi_get_out_of_my_way=0x1`).

Run-time control (all also reachable through `amd-info hv ...`): `mode 0|1|2` (unregister/register), `sysctl 0|1`,
`tpr 0|1` (for VMs created from then on), `rescan` (pick up CPUs that came online late).

## Coming from RyzenHV

AMDHV is a rewrite, not a drop-in update — do not run both kexts, and do not keep the old boot-args:

1. Remove `RyzenHV.kext` from `EFI/OC/Kexts` and its Kernel → Add entry in `config.plist`. Both kexts call
   `EFER.SVME` on every core, and the kernel accepts only one Hypervisor.framework registration.
2. Remove the boot-args RyzenHV needed; none of them help AMDHV: `rhv_enable_svm`, `rhv_hv`, `rhv_hv_sysctl`,
   `rhv_tpr_shadow`, `rhv_allow_users`, `rhv_clientpatch`, `rhv_csrelax`, `rhv_vcpuhint`, `rhv_stage`,
   `rhv_loglevel`, `rhv_selftest`, `-rhv_off`, and above all `amfi_get_out_of_my_way=0x1` (AMDHV needs no AMFI
   relaxation at all).
3. Install AMDHV with `./scripts/install-to-efi.sh` (it keeps a `config.plist.amdhv-backup`).

## Diagnostics

```
amd-info                  CPU / SVM
amd-info log [-f]         kernel ring log (-f: follow)
amd-info stats            event counters
amd-info hv               native mode state; "hv sysctl 0|1", "hv tpr 0|1", "hv rescan"
sudo amd-info selftest    check EFER/VM_HSAVE_PA on every core
```

## Layout of the sources

```
kext/                     the kext: SVM, VMCB, VMRUN, nested page tables, the native Hypervisor.framework front end
include/                  the kext ⇄ tools ABI (diagnostics), the log ring, the VMCS field rules
tools/amd-info            the CLI above
tools/hvf-smoke           a small VMM written only against the public Hypervisor.framework API (the acceptance test)
tests/                    unit tests of the pure logic, run by `make test`
scripts/                  EFI install, QEMU signing, the QEMU boot test, the symbol check
```

## Limits

- One VM program at a time is the safest; 16 vCPUs per VM is the current maximum.
- Sleep and wake of the Mac with a running VM is untested; shut the VMs down before you restart or sleep.
- The TSC write through `hv_vcpu_write_msr(0x10)` is not applied (use `-cpu host`'s default TSC handling;
  `hv_vm_sync_tsc` works).
- `hv_vcpu_run_until`'s deadline is ignored (QEMU uses `hv_vcpu_run` and the Mac-side loop).
- No entitlement check on VM creation in native mode: any local process that calls Hypervisor.framework can
  create a VM. Do not use it on a machine where untrusted local code runs.
- Ported from RyzenHV, which was tested on a Ryzen 7 5800H with macOS Sequoia 15.8.1; the rewrite itself needs
  re-testing on hardware — that is what `hvf-smoke` and `qemu-test.sh` are for.

## License

MIT, see `LICENSE`. Ported from RyzenHV (MIT); keep that copyright notice when you redistribute.
