/*
 * Host-side unit test for the pure helpers in AMDHVShared.h.
 * Builds with any C compiler, no macOS SDK needed:  make test
 */
#include <stdio.h>
#include <stdlib.h>

#include "../include/AMDRing.h"
#include "../include/AMDHVShared.h"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

static void test_ring_basic(void)
{
    char storage[16], out[32];
    AMDHVRing r;
    uint64_t next, dropped;

    amdhv_ring_init(&r, storage, sizeof(storage));
    amdhv_ring_write(&r, "hello", 5);

    uint32_t n = amdhv_ring_read(&r, 0, out, sizeof(out), &next, &dropped);
    CHECK(n == 5 && memcmp(out, "hello", 5) == 0);
    CHECK(next == 5 && dropped == 0);

    /* Nothing new: zero bytes, cursor stays. */
    n = amdhv_ring_read(&r, next, out, sizeof(out), &next, &dropped);
    CHECK(n == 0 && next == 5 && dropped == 0);
}

static void test_ring_wrap(void)
{
    char storage[8], out[32];
    AMDHVRing r;
    uint64_t next, dropped;

    amdhv_ring_init(&r, storage, sizeof(storage));
    amdhv_ring_write(&r, "AAAAAA", 6);   /* head 6 */
    amdhv_ring_write(&r, "BBBBBB", 6);   /* head 12, ring holds the last 8: "AABBBBBB" */

    /* A reader still at 0 lost the first 4 bytes. */
    uint32_t n = amdhv_ring_read(&r, 0, out, sizeof(out), &next, &dropped);
    CHECK(dropped == 4);
    CHECK(n == 8 && memcmp(out, "AABBBBBB", 8) == 0);
    CHECK(next == 12);

    /* A reader that kept up loses nothing. */
    n = amdhv_ring_read(&r, 6, out, sizeof(out), &next, &dropped);
    CHECK(dropped == 0 && n == 6 && memcmp(out, "BBBBBB", 6) == 0);
}

static void test_ring_oversized_write(void)
{
    char storage[4], out[32];
    AMDHVRing r;
    uint64_t next, dropped;

    amdhv_ring_init(&r, storage, sizeof(storage));
    amdhv_ring_write(&r, "0123456789", 10);   /* only "6789" survives */

    uint32_t n = amdhv_ring_read(&r, 0, out, sizeof(out), &next, &dropped);
    CHECK(dropped == 6);
    CHECK(n == 4 && memcmp(out, "6789", 4) == 0);
    CHECK(next == 10);
}

static void test_ring_chunked_reader(void)
{
    char storage[64], out[3];
    AMDHVRing r;
    uint64_t cursor = 0, dropped;
    char result[64];
    size_t got = 0;

    amdhv_ring_init(&r, storage, sizeof(storage));
    amdhv_ring_write(&r, "abcdefghij", 10);

    /* Reader with a 3-byte buffer reassembles the stream. */
    for (;;) {
        uint32_t n = amdhv_ring_read(&r, cursor, out, sizeof(out), &cursor, &dropped);
        if (!n)
            break;
        memcpy(result + got, out, n);
        got += n;
    }
    CHECK(got == 10 && memcmp(result, "abcdefghij", 10) == 0);

    /* A cursor from the future (stale client) is clamped, not trusted. */
    uint32_t n = amdhv_ring_read(&r, 9999, out, sizeof(out), &cursor, &dropped);
    CHECK(n == 0 && cursor == 10);
}

int main(void)
{
    /* Ryzen 7 5800H (Cezanne): CPUID Fn0000_0001 EAX = 0x00A50F00 -> family 19h, model 50h. */
    CHECK(amdhv_amd_family(0x00A50F00u) == 0x19);
    CHECK(amdhv_amd_model(0x00A50F00u) == 0x50);
    CHECK(amdhv_amd_stepping(0x00A50F00u) == 0);

    /* Zen 2 (Matisse): EAX = 0x00870F10 -> family 17h, model 71h, stepping 0. */
    CHECK(amdhv_amd_family(0x00870F10u) == 0x17);
    CHECK(amdhv_amd_model(0x00870F10u) == 0x71);

    /* Vendor string "AuthenticAMD". */
    CHECK(amdhv_is_amd_vendor(0x68747541u, 0x444D4163u, 0x69746E65u));
    /* "GenuineIntel": EBX=Genu, EDX=ineI, ECX=ntel. */
    CHECK(!amdhv_is_amd_vendor(0x756E6547u, 0x6C65746Eu, 0x49656E69u));

    /* Struct layouts are part of the kext/userland ABI. */
    CHECK(sizeof(AMDHVInfo) == 14 * sizeof(uint32_t));
    CHECK(sizeof(AMDHVStats) == 26 * sizeof(uint64_t));
    CHECK(sizeof(AMDHVRegs) == 18 * sizeof(uint64_t));
    CHECK(sizeof(AMDHVExit) == 48);
    CHECK(sizeof(AMDHVLogChunk) == AMDHV_LOG_CHUNK_SIZE);
    CHECK(kAMDHVMethodCount == 6);    /* diagnostics only: GetInfo, ReadLog, SetLogLevel, GetStats, SelfTest, HvControl */
    CHECK(kAMDHVMethodHvControl == 5);
    CHECK(sizeof(AMDHVRunIn) == 1688 && sizeof(AMDHVRunOut) == 8 + 48 + sizeof(AMDHVRegs) + 55 * 8 && sizeof(AMDHVReadMany) == 196 && sizeof(AMDHVReadManyOut) == 384);
    CHECK(AMDHV_ABI_VERSION == 1);
    CHECK(sizeof(AMDHVFpuState) == 16 + AMDHV_FPU_AREA_MAX);

    /* NPF decoding: write to a present (read-only) page; fetch from a not-present page; walk fault. */
    CHECK(amdhv_npf_present(0x3) && amdhv_npf_is_write(0x3) && !amdhv_npf_is_fetch(0x3));
    CHECK(!amdhv_npf_present(0x14) && amdhv_npf_is_fetch(0x14) && !amdhv_npf_is_write(0x14));
    CHECK(amdhv_npf_in_walk(1ull << 33) && !amdhv_npf_in_walk(1ull << 32));

    /* IOIO decoding: OUT DX,AL to port 0x3F8 as reported by SVM. */
    {
        uint64_t info1 = ((uint64_t)0x3F8 << 16) | (1u << 4);
        CHECK(amdhv_ioio_port(info1) == 0x3F8);
        CHECK(!amdhv_ioio_is_in(info1) && !amdhv_ioio_is_string(info1) && !amdhv_ioio_has_rep(info1));
        CHECK(amdhv_ioio_size(info1) == 1);
        CHECK(amdhv_ioio_is_in(info1 | 1) && amdhv_ioio_size(((uint64_t)0x60 << 16) | (1u << 6) | 1) == 4);
    }

    test_ring_basic();
    test_ring_wrap();
    test_ring_oversized_write();
    test_ring_chunked_reader();

    puts("host_test: OK");
    return 0;
}
