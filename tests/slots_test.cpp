/*
 * slots_test - consistency of the generated framework tables (HvSlots.hpp, HvMsrs.hpp).
 *
 * These tables are pure data mirrored from the framework's own slot cache and MSR cache; the
 * front end trusts them blindly (tReadSlot, the dirty-slot loop, the exit write-back), so a
 * regeneration mistake would corrupt the register protocol silently. Nothing else keeps them
 * honest, so this test checks:
 *   - kSlotField and kFieldSlot are exact inverses (both directions, which also catches duplicates)
 *   - slot 0 (the flags/ctrl word) has no field
 *   - the slot anchors Frontend.cpp hardcodes (kSlotRsp/kSlotRip/kSlotRflags = 75/76/77)
 *     really are GUEST_RSP/GUEST_RIP/GUEST_RFLAGS
 *   - every field of the batched-run exit report reaches a client slot (< 93), except CR2
 *     and TPR, which are registers of the page, not VMCS slots (Frontend handles them apart)
 *   - kMsrOfIdx holds no duplicates, only plausible MSR addresses, and the MSRs that
 *     hvf-smoke enables natively are all present exactly once
 */
#include "../kext/Source/HvSlots.hpp"
#include "../kext/Source/HvMsrs.hpp"
#include "../include/AMDHVShared.h"

#include <cstdio>
#include <cstdint>
#include <set>

using namespace amdhv::hv;

static int failures;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (cond) {                                       \
            printf("  ok: ");                             \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        } else {                                          \
            printf("  FAIL (line %d): ", __LINE__);       \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
            failures++;                                   \
        }                                                 \
    } while (0)

/* Field encodings by name, straight from the generated list. */
#define F(name, enc) kEnc_##name = enc,
enum : uint32_t {
#include "../kext/Source/VmcsFields.def"
};
#undef F

/* Must match kCacheSlots in Frontend.cpp: slots 0..92 live in the client's rw slice. */
constexpr unsigned kClientSlots = 93;

static const uint32_t kRunFields[AMDHV_RUN_NFIELDS] = {
#define AMDHV_X(enc) enc,
    AMDHV_RUN_FIELDS_LIST(AMDHV_X)
#undef AMDHV_X
};

int main(void)
{
    /* slot -> field and field -> slot must be exact inverses. */
    for (unsigned s = 0; s <= kMaxSlot; s++) {
        const uint32_t f = kSlotField[s];
        if (!f)
            continue;
        CHECK((f & 1) == 0 && (f >> 1) < kFieldSlotCount, "slot %u: encoding %#x is not an even field encoding", s, f);
        if ((f & 1) == 0 && (f >> 1) < kFieldSlotCount)
            CHECK(kFieldSlot[f >> 1] == s, "slot %u (field %#x): kFieldSlot says %u", s, f, kFieldSlot[f >> 1]);
    }
    for (unsigned i = 0; i < kFieldSlotCount; i++) {
        const unsigned s = kFieldSlot[i];
        if (!s)
            continue;
        CHECK(s <= kMaxSlot, "field %#x: slot %u beyond kSlotField", i * 2, s);
        if (s <= kMaxSlot)
            CHECK(kSlotField[s] == i * 2, "field %#x: kSlotField says %#x", i * 2, kSlotField[s]);
    }
    CHECK(kSlotField[0] == 0, "slot 0 is the flags/ctrl word, no field");

    /* The anchors Frontend.cpp hardcodes when it reads/writes the register slots. */
    CHECK(kSlotField[75] == kEnc_VMCS_GUEST_RSP, "slot 75 (kSlotRsp) is GUEST_RSP");
    CHECK(kSlotField[76] == kEnc_VMCS_GUEST_RIP, "slot 76 (kSlotRip) is GUEST_RIP");
    CHECK(kSlotField[77] == kEnc_VMCS_GUEST_RFLAGS, "slot 77 (kSlotRflags) is GUEST_RFLAGS");

    /* Every exit-report field must reach the client, except the two page registers. */
    for (unsigned i = 0; i < AMDHV_RUN_NFIELDS; i++) {
        const uint32_t enc = kRunFields[i];
        if (enc == 0xF000 || enc == 0xF004)
            continue;
        const unsigned slot = (enc >> 1) < kFieldSlotCount ? kFieldSlot[enc >> 1] : 0;
        CHECK(slot >= 1 && slot < kClientSlots, "run field %#x has client slot %u", enc, slot);
    }

    /* The MSR cache table: no duplicates, plausible addresses, the native MSRs present. */
    CHECK(kMsrOfIdx[0] == 0, "MSR cache index 0 is empty");
    std::set<uint32_t> seen;
    for (unsigned i = 1; i <= kMsrCacheMax; i++) {
        const uint32_t m = kMsrOfIdx[i];
        if (!m)
            continue;
        CHECK(m <= 0x1FFF || (m >= 0x40000000 && m <= 0x400000FF) || (m >= 0xC0000000 && m <= 0xC0001FFF),
              "idx %u: MSR %#x is not a plausible address", i, m);
        CHECK(seen.insert(m).second, "MSR %#x is unique", m);
    }
    CHECK(seen.size() >= 100, "%zu distinct MSRs cached (the framework caches most guest MSRs)", seen.size());
    /* What hvf-smoke enables natively and is NOT one of the slot-type MSRs (144..150, handled
     * separately by the front end: 0x10, 0x174, 0x175, 0x176, 0x1D9, 0xC0000100, 0xC0000101). */
    for (uint32_t m : {0xC0000081ull, 0xC0000082ull, 0xC0000083ull, 0xC0000084ull, 0xC0000102ull})
        CHECK(seen.count(m) == 1, "native MSR %#x is in the cache table", m);

    if (failures) {
        printf("slots_test: %d check(s) failed\n", failures);
        return 1;
    }
    printf("slots_test: OK\n");
    return 0;
}
