/*
 * AMDHVFields.h - which VMCS encodings the kernel accepts and which of them it will not let a client write.
 *
 * This mirrors the kext's vmcsRead/vmcsWrite rules (kext/Source/Vm.cpp) for tools and tests: it uses the same
 * generated field list as the kext (kext/Source/VmcsFields.def); tests/vmcs_state_test.cpp checks the accepted
 * and refused encodings against the kext's own tables, so they cannot drift apart unnoticed.
 */
#ifndef AMDHV_FIELDS_H
#define AMDHV_FIELDS_H

#include <stdint.h>

enum { AMDHV_FIELD_OK = 0, AMDHV_FIELD_INVALID = 1, AMDHV_FIELD_READ_ONLY = 2, AMDHV_FIELD_BAD_VALUE = 3 };

static const uint32_t kAmdhvVmcsEncodings[] = {
#define F(name, enc) enc,
#include "../kext/Source/VmcsFields.def"
#undef F
};

static inline int amdhv_vmcs_field_known(uint32_t enc)
{
    unsigned lo = 0, hi = (unsigned)(sizeof(kAmdhvVmcsEncodings) / sizeof(kAmdhvVmcsEncodings[0]));
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (kAmdhvVmcsEncodings[mid] == enc)
            return 1;
        if (kAmdhvVmcsEncodings[mid] < enc)
            lo = mid + 1;
        else
            hi = mid;
    }
    return 0;
}

/* Fields of our own for state Intel's VMCS does not have (VmxMap.hpp): CR2, DR6, TPR, DR0-DR3. */
static inline int amdhv_vmcs_field_ext(uint32_t enc)
{
    return enc == 0xF000 || enc == 0xF002 || enc == 0xF004 || enc == 0xF010 || enc == 0xF012 || enc == 0xF014 ||
           enc == 0xF016;
}

/* The exit information fields, written by the kext on every exit. */
static inline int amdhv_vmcs_field_read_only(uint32_t enc)
{
    switch (enc) {
    case 0x4400: case 0x4402: case 0x4404: case 0x4406: case 0x4408: case 0x440a: case 0x440c:
    case 0x6400: case 0x2400: case 0x640a:
        return 1;
    default:
        return 0;
    }
}

/* What a write of `value` to `enc` would do: AMDHV_FIELD_OK, or the reason it is refused. */
static inline int amdhv_vmcs_write_status(uint32_t enc, uint64_t value)
{
    if (!amdhv_vmcs_field_ext(enc) && !amdhv_vmcs_field_known(enc))
        return AMDHV_FIELD_INVALID;
    if (amdhv_vmcs_field_read_only(enc))
        return AMDHV_FIELD_READ_ONLY;
    /* MOV to DR0-DR3 faults on a non-canonical address and the kext loads them in kernel mode. */
    if (enc >= 0xF010 && enc <= 0xF016 && (uint64_t)(((int64_t)(value << 16)) >> 16) != value)
        return AMDHV_FIELD_BAD_VALUE;
    return AMDHV_FIELD_OK;
}

/* Can the kernel read `enc`? */
static inline int amdhv_vmcs_read_status(uint32_t enc)
{
    return amdhv_vmcs_field_ext(enc) || amdhv_vmcs_field_known(enc) ? AMDHV_FIELD_OK : AMDHV_FIELD_INVALID;
}

#endif /* AMDHV_FIELDS_H */
