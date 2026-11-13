/*
 * Log.hpp - kernel-side logging for the kext.
 *
 * Messages go into a 64 KiB ring buffer that userland reads with
 * `amd-info log [-f]`, and are mirrored to IOLog (system log) up to the
 * level given by boot-arg amdhv_iolog (default INFO).
 *
 * Boot-args:
 *   amdhv_loglevel=N   ring level, 0..5 (none, error, warn, info, debug, trace)
 *   amdhv_iolog=N      IOLog mirror level, same scale
 *
 * Rules for call sites:
 *   - never log from a mp_rendezvous action or any interrupt-disabled context
 *   - use AMDHV_SYNC right BEFORE something that can panic: it always reaches
 *     IOLog immediately, so the last line before a crash is visible
 */
#ifndef AMDHV_LOG_HPP
#define AMDHV_LOG_HPP

#include <stdint.h>
#include <libkern/OSAtomic.h>
#include "../../include/AMDHVShared.h"

namespace amdhv {

/* Defined in Log.cpp. */
extern AMDHVStats gStats;

namespace log {

void     init();
void     write(uint32_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void     writeSync(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void     assertFailed(const char *expr, const char *file, int line);

/* Fills a AMDHVLogChunk from `cursor`. `out` must point to a full AMDHV_LOG_CHUNK_SIZE buffer. */
void     read(uint64_t cursor, AMDHVLogChunk *out);

void     setLevel(uint32_t level);
uint32_t level();

} // namespace log
} // namespace amdhv

/* Atomic bumps of the counters exposed by `amd-info stats`. */
#define AMDHV_STAT_INC(field) OSIncrementAtomic64((volatile SInt64 *)&amdhv::gStats.field)
#define AMDHV_STAT_ADD(field, n) OSAddAtomic64((SInt64)(n), (volatile SInt64 *)&amdhv::gStats.field)

#define AMDHV_ERR(...)   amdhv::log::write(AMDHV_LOG_ERROR, __VA_ARGS__)
#define AMDHV_WARN(...)  amdhv::log::write(AMDHV_LOG_WARN, __VA_ARGS__)
#define AMDHV_INFO(...)  amdhv::log::write(AMDHV_LOG_INFO, __VA_ARGS__)
#define AMDHV_DBG(...)   amdhv::log::write(AMDHV_LOG_DEBUG, __VA_ARGS__)
#define AMDHV_TRACE(...) amdhv::log::write(AMDHV_LOG_TRACE, __VA_ARGS__)
#define AMDHV_SYNC(...)  amdhv::log::writeSync(__VA_ARGS__)

/* Does not panic: logs, counts, and lets you see it in `amd-info stats`. */
#define AMDHV_ASSERT(cond)                                                    \
    do {                                                                    \
        if (!(cond))                                                        \
            amdhv::log::assertFailed(#cond, __FILE__, __LINE__);              \
    } while (0)

#endif /* AMDHV_LOG_HPP */
