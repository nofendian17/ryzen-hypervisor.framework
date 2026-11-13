/*
 * Vm.hpp - a virtual machine: one shared part and one object per vCPU.
 *
 * com_amdhv_AMDHVVmShared is what all vCPUs of a VM have in common: the nested page tables (guest physical ->
 * pinned memory, built by amdhv::npt::Table) and the list of the vCPUs. com_amdhv_AMDHVVm is one vCPU: it owns the
 * VMCB, the host VMCB, the I/O and MSR permission bitmaps and the guest's general purpose registers, and has its own
 * lock, so vCPUs run in parallel on different host threads.
 *
 * Guest memory is the calling process's own pages (hv_vm_map), pinned into 4 KiB NPT entries pointing into a Backing.
 *
 * A vCPU's own state is serialised by its lock: run() holds it while the guest runs, which is bounded by the time slice
 * (see Vm.cpp). The page tables have a reader/writer lock: a vCPU holds it for reading while it is inside the guest, and
 * map/unmap/protect take it for writing after kicking every vCPU out, so when they return no vCPU still runs with
 * the old mappings or permissions (every VMRUN flushes the TLB).
 */
#ifndef AMDHV_VM_HPP
#define AMDHV_VM_HPP

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLocks.h>
#include <libkern/c++/OSObject.h>

#include "../../include/AMDHVShared.h"
#include "FpuState.hpp"
#include "Npt.hpp"
#include "VmcsState.hpp"
#include "PhysMem.hpp"
#include "VMCB.hpp"

namespace amdhv {

/* Guest GPRs that VMRUN does not keep in the VMCB (RAX and RSP are in the VMCB). Layout is used by SvmRun.S. */
struct GuestGprs {
    uint64_t rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
};

} // namespace amdhv


class com_amdhv_AMDHVVm;

class com_amdhv_AMDHVVmShared : public OSObject {
    OSDeclareDefaultStructors(com_amdhv_AMDHVVmShared)

public:
    /* `owner` is the task that created it (a vCPU may only join from the same task). */
    static com_amdhv_AMDHVVmShared *create(task_t owner, IOReturn *err);
    /* Looks a VM up by id; takes a reference, nullptr when there is none or it belongs to another task. */
    static com_amdhv_AMDHVVmShared *find(uint64_t id, task_t owner);

    bool init() override;
    void free() override;

    uint64_t            id() const { return fId; }
    amdhv::npt::Table    &npt() { return fNpt; }
    IORWLock           *rw() const { return fRw; }

    /* hv_vm_map / unmap / protect. See AMDHVShared.h for the exact rules. */
    IOReturn map(task_t task, uint64_t uva, uint64_t gpa, uint64_t size, uint32_t flags);
    IOReturn unmap(uint64_t gpa, uint64_t size);
    IOReturn protect(uint64_t gpa, uint64_t size, uint32_t flags);
    /* Debugging: reads guest physical memory through the nested page tables. Fails when a page is not mapped. */
    IOReturn readPhys(uint64_t gpa, void *dst, size_t len);

    /* The vCPUs, for kicking them out of the guest. attach() fails with kIOReturnNoResources when the VM is full. */
    IOReturn attach(com_amdhv_AMDHVVm *vcpu, uint32_t *index);
    void     detach(com_amdhv_AMDHVVm *vcpu);
    void     kickAll();

    /* A pinned buffer (user or kernel) and how many NPT pages still point into it. */
    struct Backing {
        IOMemoryDescriptor *md;
        uint64_t            live;
        IODirection         dir;      /* what prepare() was called with: complete() must use the same */
    };

private:
    bool     setup(task_t owner);
    IOReturn mapDescriptorLocked(IOMemoryDescriptor *md, uint64_t gpa, uint64_t size, uint32_t perm);
    static void dropPage(Backing *b);
    static void releaseOwner(void *ctx, void *owner);

    uint64_t                  fId;
    task_t                    fOwner;
    IORWLock                 *fRw;
    IOLock                   *fRegLock;
    com_amdhv_AMDHVVm *fVcpus[AMDHV_MAX_VCPUS_PER_VM];
    amdhv::npt::Table           fNpt;
    uint64_t                  fPagesMapped;
};

class com_amdhv_AMDHVVm : public OSObject {
    OSDeclareDefaultStructors(com_amdhv_AMDHVVm)

public:
    /*
     * Fails with kIOReturnNotReady if SVM is not active on all CPUs and with
     * kIOReturnUnsupported if the CPU lacks NPT or NRIPS. With `shared` null a new VM is made (this is its first vCPU),
     * otherwise the vCPU joins that VM (kIOReturnNoResources when it is full).
     */
    static com_amdhv_AMDHVVm *create(const AMDHVInfo &info, task_t owner,
                                             com_amdhv_AMDHVVmShared *shared, IOReturn *err);

    com_amdhv_AMDHVVmShared *shared() const { return fShared; }
    uint32_t                         index() const { return fIndex; }

    bool init() override;
    void free() override;

    /* hv_vm_map / unmap / protect. See AMDHVShared.h for the exact rules. */
    IOReturn map(task_t task, uint64_t uva, uint64_t gpa, uint64_t size, uint32_t flags);
    IOReturn unmap(uint64_t gpa, uint64_t size);
    IOReturn protect(uint64_t gpa, uint64_t size, uint32_t flags);

    IOReturn getRegs(AMDHVRegs *out);
    IOReturn setRegs(const AMDHVRegs *in);
    IOReturn run(bool skipInstruction, AMDHVExit *out);
    IOReturn runEx(const AMDHVRunIn *in, AMDHVRunOut *out);            /* apply, run, report in one call */
    IOReturn vmcsReadMany(const AMDHVReadMany *in, AMDHVReadManyOut *out);

    /* The Hypervisor.framework (VMX-shaped) view of the guest. */
    IOReturn vmcsRead(uint32_t field, uint64_t *value);
    uint8_t  debugCpl();                                          /* the CPL the VMCB holds (debugging) */
    IOReturn vmcsWrite(uint32_t field, uint64_t value);

    /* Guest x87/SSE/AVX state and XCR0. */
    IOReturn msrOp(uint32_t op, uint32_t msr, uint64_t value, uint64_t *out);
    /*
     * hv_vcpu_interrupt: no lock, called while another thread sits in run(). Besides raising the flag it sends an IPI to
     * the CPU the guest runs on: the physical interrupt makes VMRUN exit at once, instead of when the host happens to
     * take its next interrupt (up to 10 ms later, too slow for a guest that expects a 250-1000 Hz timer).
     */
    void     kick();

    IOReturn getFpu(AMDHVFpuState *out);
    IOReturn setFpu(const AMDHVFpuState *in);

private:
    bool     setup();
    void     resetLocked(uint16_t cs, uint16_t ip);
    void     dumpVmcb(const char *why);

    IOLock                   *fLock;
    com_amdhv_AMDHVVmShared *fShared;
    uint32_t                  fIndex;
    amdhv::PhysMem              fVmcb;
    amdhv::PhysMem              fHostVmcb;
    amdhv::PhysMem              fIopm;     /* 12 KiB, all ones: intercept every port */
    amdhv::PhysMem              fMsrpm;    /*  8 KiB, all ones: intercept every MSR  */
    amdhv::vmx::VmcsState       fVmcs;
    amdhv::PhysMem              fHostFpu, fGuestFpu, fScratchFpu;   /* XSAVE areas, 4 KiB aligned */
    uint64_t                  fGuestXcr0;
    uint64_t                  fXcr0Supported;    /* CPUID.0xD.0 EDX:EAX */
    bool                      fHasTscAux;        /* the CPU has RDTSCP, so IA32_TSC_AUX */
    uint32_t                  fFpuSize;
    volatile uint32_t         fKick;
    volatile int              fRunCpu;           /* CPU inside VMRUN, -1 when none; for kick() */
    amdhv::GuestGprs            fGprs;
    bool                      fFirstRun;
    bool                      fHaveNextRip;
};

#endif /* AMDHV_VM_HPP */
