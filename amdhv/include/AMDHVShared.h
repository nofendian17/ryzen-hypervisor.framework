/*
 * AMDHVShared.h
 *
 * ABI shared between the kext, the tools and the host-side unit tests.
 * Plain C, no kernel/userland-only includes, so both sides can use it.
 *
 * The VM work (create, map, run) does NOT go through this header: the kext
 * registers itself with xnu as the Hypervisor.framework provider (native
 * mode), so unmodified applications talk to it through the public hv_* API.
 * This header only carries the diagnostics user client, the kernel log
 * protocol and the types of the kext's internal VM protocol (kept here so
 * the pure-logic unit tests compile against the same definitions the
 * kernel uses).
 *
 * Bump AMDHV_ABI_VERSION whenever a struct or selector below changes.
 */
#ifndef AMDHV_SHARED_H
#define AMDHV_SHARED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMDHV_ABI_VERSION 1
#define AMDHV_SERVICE_NAME "com_amdhv_AMDHV"
#define AMDHV_MAX_CPUS 64
/* Largest number of vCPUs in one VM. */
#define AMDHV_MAX_VCPUS_PER_VM 16

/*
 * IOUserClient selectors. Diagnostics only; add new ones at the end.
 *
 *   GetInfo      out: struct AMDHVInfo
 *   ReadLog      in: scalar[0] = cursor            out: struct AMDHVLogChunk
 *   SetLogLevel  in: scalar[0] = level (root only)
 *   GetStats     out: struct AMDHVStats
 *   SelfTest     out: scalar[0] = cpus ok, scalar[1] = cpus checked (root only)
 *   HvControl    in: scalar[0] = cmd, scalar[1] = value
 *                out: scalar[0] = state, scalar[1] = mode
 */
enum {
    kAMDHVMethodGetInfo = 0,
    kAMDHVMethodReadLog,
    kAMDHVMethodSetLogLevel,
    kAMDHVMethodGetStats,
    kAMDHVMethodSelfTest,
    kAMDHVMethodHvControl,
    kAMDHVMethodCount
};

/* kAMDHVMethodHvControl, front end (kext/Source/Frontend.cpp). */
enum {
    kAMDHVHvGet     = 0,   /* value ignored */
    kAMDHVHvMode    = 1,   /* value 0: unregister the traps; 1: register, answer HV_UNSUPPORTED to unimplemented traps; 2: register, answer success */
    kAMDHVHvSysctl  = 2,   /* value 0/1: kern.hv_support shows 1 (needs the traps registered) */
    kAMDHVHvTpr     = 3,   /* value 0/1: offer the TPR shadow to the VMs created from now on (default 1, boot-arg amdhv_tpr_shadow) */
    kAMDHVHvSvmRescan = 4  /* debugging: look for CPUs that came online late and turn SVM on there; state[1] is how many were enabled */
};
/* state bits returned in scalarOutput[0] */
enum { kAMDHVHvStateRegistered = 1u << 0, kAMDHVHvStateSysctl = 1u << 1, kAMDHVHvStateTpr = 1u << 2 };

/* Kernel log levels. A message is kept when its level <= the current level. */
enum {
    AMDHV_LOG_NONE  = 0,
    AMDHV_LOG_ERROR = 1,
    AMDHV_LOG_WARN  = 2,
    AMDHV_LOG_INFO  = 3,
    AMDHV_LOG_DEBUG = 4,
    AMDHV_LOG_TRACE = 5
};

/* CPUID Fn8000_000A EDX: SVM feature bits (AMD APM Vol.2, 15.34). */
enum {
    AMDHV_SVM_FEAT_NP             = 1u << 0,   /* nested paging (NPT)            */
    AMDHV_SVM_FEAT_LBRV           = 1u << 1,   /* LBR virtualization             */
    AMDHV_SVM_FEAT_SVML           = 1u << 2,   /* SVM lock                       */
    AMDHV_SVM_FEAT_NRIPS          = 1u << 3,   /* next-RIP saved on #VMEXIT      */
    AMDHV_SVM_FEAT_TSCRATE        = 1u << 4,   /* TSC ratio MSR                  */
    AMDHV_SVM_FEAT_VMCB_CLEAN     = 1u << 5,   /* VMCB clean bits                */
    AMDHV_SVM_FEAT_FLUSH_BY_ASID  = 1u << 6,
    AMDHV_SVM_FEAT_DECODE_ASSISTS = 1u << 7,
    AMDHV_SVM_FEAT_PAUSE_FILTER   = 1u << 10,
    AMDHV_SVM_FEAT_PAUSE_THRESH   = 1u << 12,
    AMDHV_SVM_FEAT_AVIC           = 1u << 13,
    AMDHV_SVM_FEAT_VVMSAVE        = 1u << 15,  /* virtual VMSAVE/VMLOAD          */
    AMDHV_SVM_FEAT_VGIF           = 1u << 16   /* virtual GIF                    */
};

/* Result of kAMDHVMethodGetInfo. All fields are uint32_t on purpose. */
typedef struct AMDHVInfo {
    uint32_t abiVersion;
    uint32_t vendorIsAmd;
    uint32_t cpuFamily;          /* display family, 0x19 on Zen 3          */
    uint32_t cpuModel;           /* display model,  0x50 on Cezanne        */
    uint32_t cpuStepping;
    uint32_t svmSupported;       /* CPUID Fn8000_0001 ECX[2]               */
    uint32_t svmDisabledByBios;  /* VM_CR.SVMDIS                           */
    uint32_t svmLocked;          /* VM_CR.LOCK                             */
    uint32_t svmRevision;        /* CPUID Fn8000_000A EAX[7:0]             */
    uint32_t numAsids;           /* CPUID Fn8000_000A EBX                  */
    uint32_t svmFeatures;        /* CPUID Fn8000_000A EDX, AMDHV_SVM_FEAT_* */
    uint32_t nCpus;              /* logical CPUs probed                    */
    uint32_t nCpusSvmUsable;     /* supported && !SVMDIS                   */
    uint32_t nCpusSvmActive;     /* EFER.SVME set by us                    */
} AMDHVInfo;

/* Event counters kept by the kext, for "did that code path run at all?" questions. */
typedef struct AMDHVStats {
    uint64_t probeRuns;
    uint64_t enableCalls;
    uint64_t enableCpusOk;       /* CPUs that took the SVM enable path */
    uint64_t enableFailures;
    uint64_t disableCalls;
    uint64_t sleepEvents;
    uint64_t wakeEvents;
    uint64_t clientOpens;
    uint64_t clientCalls;
    uint64_t assertFailures;
    uint64_t selftestRuns;
    uint64_t selftestCpusOk;
    uint64_t logBytesTotal;      /* bytes ever written to the kernel log ring */
    uint64_t logReentries;       /* log writes that found the ring already locked on this CPU: best-effort, no spin */
    uint64_t logLevel;           /* current AMDHV_LOG_* level */
    uint64_t vmCreates;
    uint64_t vmRuns;             /* run calls */
    uint64_t vmExits;            /* #VMEXITs of every kind, incl. host interrupts */
    uint64_t vmInvalidExits;     /* VMEXIT_INVALID: VMRUN rejected the VMCB */
    uint64_t vmMaps;             /* successful map calls */
    uint64_t vmMapFailures;
    uint64_t vmUnmaps;
    uint64_t vmProtects;
    uint64_t vmNptFaults;        /* NPF faults handed to a client */
    uint64_t vmPagesMapped;      /* guest pages currently mapped, summed over VMs */
    uint64_t vmFpuMismatches;    /* host x87/SSE state differed after a VMRUN round trip: must stay 0 */
} AMDHVStats;

/*
 * ---------------------------------------------------------------------------
 * Types of the kext's internal VM protocol (native front end -> Vm core).
 * Not part of the diagnostics user client; public so the unit tests compile
 * against the same definitions the kernel uses.
 * ---------------------------------------------------------------------------
 */

/* Map / protect permission flags, same values as hv_memory_flags_t. Write or execute imply read. */
enum { AMDHV_MEM_READ = 1u << 0, AMDHV_MEM_WRITE = 1u << 1, AMDHV_MEM_EXEC = 1u << 2 };

/* Largest single mapping, in bytes. */
#define AMDHV_MAP_MAX_SIZE (16ull << 30)

/* VmMsr operations. */
enum {
    AMDHV_MSR_READ           = 0,
    AMDHV_MSR_WRITE          = 1,
    AMDHV_MSR_NATIVE_ENABLE  = 2,   /* the guest accesses the MSR without exits, the value lives in the vCPU */
    AMDHV_MSR_NATIVE_DISABLE = 3
};

/* Guest extended state. The area is the XSAVE standard format; `size` is the host's XSAVE size for its XCR0. */
#define AMDHV_FPU_AREA_MAX 3072
typedef struct AMDHVFpuState {
    uint64_t xcr0;
    uint32_t size;
    uint32_t reserved;
    uint8_t  area[AMDHV_FPU_AREA_MAX];
} AMDHVFpuState;

/* Guest registers. */
typedef struct AMDHVRegs {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;
} AMDHVRegs;

/* Why a run returned, in our own words. The raw SVM data is in AMDHVExit too. */
enum {
    AMDHV_EXIT_NONE = 0,
    AMDHV_EXIT_TIMESLICE,    /* nothing to handle, run again */
    AMDHV_EXIT_HLT,
    AMDHV_EXIT_IO,           /* IN/OUT, decode with amdhv_ioio_*() */
    AMDHV_EXIT_CPUID,
    AMDHV_EXIT_MSR,          /* info1: 0 = RDMSR, 1 = WRMSR; ECX = MSR */
    AMDHV_EXIT_NPF,          /* nested page fault, info2 = guest physical address */
    AMDHV_EXIT_SHUTDOWN,     /* triple fault */
    AMDHV_EXIT_INVALID,      /* VMRUN rejected the VMCB, see the kernel log */
    AMDHV_EXIT_VMMCALL,
    AMDHV_EXIT_UNHANDLED     /* anything else, look at exitCode */
};

/* Raw SVM exit codes (AMD APM Vol.2, App. C). */
#define AMDHV_SVM_EXIT_INTR     0x60ull
#define AMDHV_SVM_EXIT_NMI      0x61ull
#define AMDHV_SVM_EXIT_CPUID    0x72ull
#define AMDHV_SVM_EXIT_HLT      0x78ull
#define AMDHV_SVM_EXIT_IOIO     0x7Bull
#define AMDHV_SVM_EXIT_MSR      0x7Cull
#define AMDHV_SVM_EXIT_SHUTDOWN 0x7Full
#define AMDHV_SVM_EXIT_VMMCALL  0x81ull
#define AMDHV_SVM_EXIT_NPF      0x400ull
#define AMDHV_SVM_EXIT_INVALID  (~0ull)

typedef struct AMDHVExit {
    uint32_t reason;             /* AMDHV_EXIT_* */
    uint32_t reserved;
    uint64_t exitCode;           /* raw SVM exit code */
    uint64_t info1;              /* raw EXITINFO1 */
    uint64_t info2;              /* raw EXITINFO2 */
    uint64_t nextRip;            /* VMCB next_rip, valid for HLT/CPUID/IO/MSR */
    uint64_t rip;                /* guest RIP at the exit */
} AMDHVExit;

/* ---- The batched run protocol used by the native front end ---- */

/* AMDHVRunIn.flags */
enum {
    AMDHV_RUN_REGS    = 1u << 0,   /* apply `regs` before anything else */
    AMDHV_RUN_NO_RUN  = 1u << 1,   /* only apply the state and return it, do not run the guest */
    AMDHV_RUN_SKIP    = 1u << 2    /* resume at the next instruction */
};

#define AMDHV_RUN_MAX_WRITES 96

typedef struct AMDHVVmcsWrite {
    uint32_t field;
    uint32_t reserved;
    uint64_t value;
} AMDHVVmcsWrite;

typedef struct AMDHVRunIn {
    uint32_t     flags;
    uint32_t     nWrites;                         /* <= AMDHV_RUN_MAX_WRITES */
    AMDHVRegs    regs;
    AMDHVVmcsWrite writes[AMDHV_RUN_MAX_WRITES];
} AMDHVRunIn;

/* The VMCS fields every run returns, in this order: what a VMX client reads after each exit. */
#define AMDHV_RUN_FIELDS_LIST(X)                                                                                 \
    X(0x4402) /* exit reason */          X(0x6400) /* exit qualification */  X(0x440c) /* instruction length */    \
    X(0x4404) /* interruption info */    X(0x4406) /* interruption error */  X(0x4408) /* IDT-vectoring info */    \
    X(0x440a) /* IDT-vectoring error */  X(0x2400) /* guest physical addr */ X(0x640a) /* guest linear addr */    \
    X(0x4824) /* interruptibility */     X(0x4826) /* activity state */      X(0xF004) /* TPR (extension) */      \
    X(0x6800) /* CR0 */                  X(0x6802) /* CR3 */                 X(0x6804) /* CR4 */                  \
    X(0x2806) /* EFER */                 X(0x681a) /* DR7 */                 X(0xF000) /* CR2 (extension) */      \
    X(0x4002) /* CPU-based controls */                                                                           \
    /* The segment group: selectors, limits and GDTR/IDTR, access rights, bases. */                              \
    X(0x0800) X(0x0802) X(0x0804) X(0x0806) X(0x0808) X(0x080a) X(0x080c) X(0x080e)                              \
    X(0x4800) X(0x4802) X(0x4804) X(0x4806) X(0x4808) X(0x480a) X(0x480c) X(0x480e) X(0x4810) X(0x4812)          \
    X(0x4814) X(0x4816) X(0x4818) X(0x481a) X(0x481c) X(0x481e) X(0x4820) X(0x4822)                              \
    X(0x6806) X(0x6808) X(0x680a) X(0x680c) X(0x680e) X(0x6810) X(0x6812) X(0x6814) X(0x6816) X(0x6818)
#define AMDHV_RUN_NFIELDS 55

typedef struct AMDHVRunOut {
    uint32_t failedWrite;                         /* index of the write that failed, when the call failed */
    uint32_t reserved;
    AMDHVExit  exit;
    AMDHVRegs  regs;
    uint64_t fields[AMDHV_RUN_NFIELDS];           /* AMDHV_RUN_FIELDS_LIST order */
} AMDHVRunOut;

#define AMDHV_READMANY_MAX 48

typedef struct AMDHVReadMany {
    uint32_t count;                               /* <= AMDHV_READMANY_MAX */
    uint32_t fields[AMDHV_READMANY_MAX];
} AMDHVReadMany;

typedef struct AMDHVReadManyOut {
    uint64_t values[AMDHV_READMANY_MAX];
} AMDHVReadManyOut;

/*
 * One read of the kernel log ring. Fixed size (4096 bytes) so the IOKit
 * dispatch table can check it. `cursor` is an absolute byte offset into the
 * stream of all log bytes ever written; pass `nextCursor` back to continue.
 * `dropped` counts bytes lost because the ring wrapped before you read them.
 */
#define AMDHV_LOG_CHUNK_SIZE 4096
#define AMDHV_LOG_CHUNK_DATA (AMDHV_LOG_CHUNK_SIZE - 24)

typedef struct AMDHVLogChunk {
    uint64_t nextCursor;
    uint64_t dropped;
    uint32_t length;             /* valid bytes in data[] */
    uint32_t reserved;
    char     data[AMDHV_LOG_CHUNK_DATA];
} AMDHVLogChunk;

/* Compile-time size checks that work in both C and C++. */
typedef char amdhv_assert_log_chunk_size[(sizeof(AMDHVLogChunk) == AMDHV_LOG_CHUNK_SIZE) ? 1 : -1];
typedef char amdhv_assert_stats_size[(sizeof(AMDHVStats) == 26 * sizeof(uint64_t)) ? 1 : -1];
typedef char amdhv_assert_regs_size[(sizeof(AMDHVRegs) == 18 * sizeof(uint64_t)) ? 1 : -1];
typedef char amdhv_assert_fpu_size[(sizeof(AMDHVFpuState) == 16 + AMDHV_FPU_AREA_MAX) ? 1 : -1];
typedef char amdhv_assert_exit_size[(sizeof(AMDHVExit) == 48) ? 1 : -1];
typedef char amdhv_assert_runin_size[(sizeof(AMDHVRunIn) == 8 + sizeof(AMDHVRegs) + AMDHV_RUN_MAX_WRITES * 16) ? 1 : -1];
typedef char amdhv_assert_runout_size[(sizeof(AMDHVRunOut) == 8 + 48 + sizeof(AMDHVRegs) + AMDHV_RUN_NFIELDS * 8) ? 1 : -1];

/* IOIO exit decoding (EXITINFO1 of SVM exit 0x7B, AMD APM Vol.2, 15.10.2). */
static inline uint32_t amdhv_ioio_port(uint64_t info1)    { return (uint32_t)(info1 >> 16) & 0xFFFF; }
static inline int      amdhv_ioio_is_in(uint64_t info1)   { return (int)(info1 & 1); }
static inline int      amdhv_ioio_is_string(uint64_t info1) { return (int)((info1 >> 2) & 1); }
static inline int      amdhv_ioio_has_rep(uint64_t info1) { return (int)((info1 >> 3) & 1); }
static inline uint32_t amdhv_ioio_size(uint64_t info1)
{
    if (info1 & (1u << 4)) return 1;
    if (info1 & (1u << 5)) return 2;
    if (info1 & (1u << 6)) return 4;
    return 0;
}

/*
 * NPF exit decoding (EXITINFO1 of SVM exit 0x400, AMD APM Vol.2, 15.25.6).
 * Bits 31:0 are a #PF-style error code, info2 is the faulting guest physical address.
 */
static inline int amdhv_npf_present(uint64_t info1)  { return (int)(info1 & 1); }          /* page was mapped but rights were insufficient */
static inline int amdhv_npf_is_write(uint64_t info1) { return (int)((info1 >> 1) & 1); }
static inline int amdhv_npf_is_fetch(uint64_t info1) { return (int)((info1 >> 4) & 1); }
static inline int amdhv_npf_in_walk(uint64_t info1)  { return (int)((info1 >> 33) & 1); }  /* fault while the CPU read the guest's own page tables */

/* CPUID helpers, pure functions so they can be unit-tested on any host. */

static inline uint32_t amdhv_amd_family(uint32_t cpuid1Eax)
{
    uint32_t base = (cpuid1Eax >> 8) & 0xF;
    uint32_t ext  = (cpuid1Eax >> 20) & 0xFF;
    return base == 0xF ? base + ext : base;
}

static inline uint32_t amdhv_amd_model(uint32_t cpuid1Eax)
{
    uint32_t base  = (cpuid1Eax >> 8) & 0xF;
    uint32_t model = (cpuid1Eax >> 4) & 0xF;
    uint32_t ext   = (cpuid1Eax >> 16) & 0xF;
    return base == 0xF ? (ext << 4) | model : model;
}

static inline uint32_t amdhv_amd_stepping(uint32_t cpuid1Eax)
{
    return cpuid1Eax & 0xF;
}

/* "AuthenticAMD" as returned in EBX, EDX, ECX by CPUID leaf 0. */
static inline int amdhv_is_amd_vendor(uint32_t ebx, uint32_t ecx, uint32_t edx)
{
    return ebx == 0x68747541u && edx == 0x69746E65u && ecx == 0x444D4163u;
}

#ifdef __cplusplus
}
#endif

#endif /* AMDHV_SHARED_H */
