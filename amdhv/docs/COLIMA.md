# Docker with colima on AMDHV

This gets you Docker containers on the Hackintosh: colima runs a Linux VM in QEMU, QEMU uses
`-accel hvf`, and hvf is AMDHV. Everything below was run on the reference machine
(Zen 2, macOS Sequoia, MacPorts).

**The one rule:** start colima **after** boot, when AMDHV is already up. Lima decides between
`-accel hvf` (hardware) and `-accel tcg` (slow emulation) at colima start — if `kern.hv_support`
is still 0 at that moment, the VM runs on TCG until you restart colima.

## 1. Install

```bash
sudo port install colima docker       # colima pulls in lima and qemu
which colima lima qemu-system-x86_64  # all in /opt/local/bin
```

MacPorts QEMU ships with the hypervisor entitlement built in. Check it (again after a
`port upgrade qemu`):

```bash
codesign -d --entitlements - /opt/local/bin/qemu-system-x86_64 | grep hypervisor
```

Missing? Sign it: `sudo ~/amdhv/scripts/sign-qemu.sh /opt/local/bin/qemu-system-x86_64`.

## 2. Check AMDHV is up

```bash
sysctl kern.hv_support       # must print 1 (the kext sets it at boot)
sudo ~/amdhv/build/amd-info  # SVM must be "active on" every CPU
```

If `kern.hv_support` is 0, the kext is not loaded or the boot-args are missing — see the
[README](../README.md#install) Install section first.

## 3. Start colima

```bash
colima start --vm-type qemu --cpu 4 --memory 8
```

The `vz` backend does **not** work on AMD — it goes through Apple's Virtualization.framework,
not through Hypervisor.framework. If a profile already exists, check it:

```bash
grep vmType ~/.colima/*/colima.yaml    # must say vmType: qemu
```

## 4. Verify it runs through AMDHV

```bash
ps aux | grep -o "accel=[a-z]*" | head -1        # must print accel=hvf (tcg = started too early)
sudo ~/amdhv/build/amd-info stats                # vmRuns grows while the VM runs
                                                 # vmFpuMismatches must stay 0
docker run --rm hello-world                      # end-to-end
```

## 5. Daily use

| command | what it does |
|---|---|
| `colima list` | profiles and their state |
| `colima stop` / `colima start` | the VM does not survive a reboot — start it again after booting |
| `colima restart` | re-picks the accelerator: run this if the VM is on TCG |
| `colima delete` | removes the profile (deletes the disk image too) |

## Troubleshooting

| symptom | cause and fix |
|---|---|
| `ps aux` shows `accel=tcg` | colima started before AMDHV was up: `colima restart` |
| `colima status` → "empty value" | the VM never came up (this happened with `kern.hv_support` = 0): fix the kext first, then `colima start --verbose` |
| first `colima start` fails with `kIOReturnNotReady` | the kext was still bringing late CPUs online (rescan); start again — it works on the second try |
| `hv_vm_create` permission denied | QEMU without the entitlement: see step 1 |
| `kern.hv_support` reads 0 | AMDHV not loaded: check Kernel → Add in `config.plist`, boot-args `amdhv_enable_svm=1 amdhv_hv=1`, and `sudo ~/amdhv/build/amd-info log` |
