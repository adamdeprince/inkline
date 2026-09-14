#ifndef RMT_CORE_H
#define RMT_CORE_H

#include <ghostty/vt.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RmtCore RmtCore;

typedef struct {
    size_t memory_bytes;       /* Total libghostty allocation budget. */
    size_t image_bytes;        /* Image storage budget for each screen. */
    size_t scrollback_bytes;
    size_t scrollback_lines;   /* Physical rows, excluding the live screen. */
    bool allow_file_images;   /* Existing files only; default false. */
} RmtCoreOptions;

RmtCoreOptions rmt_core_defaults(void);
RmtCore *rmt_core_new(uint16_t cols, uint16_t rows, const RmtCoreOptions *options);
void rmt_core_free(RmtCore *core);
/* Borrowed handle for input encoding, terminal effects and renderer state. */
GhosttyTerminal rmt_core_terminal(RmtCore *core);
const GhosttyAllocator *rmt_core_allocator(RmtCore *core);
size_t rmt_core_memory_used(RmtCore *core);
bool rmt_core_under_pressure(RmtCore *core);
/* Call between stream writes and after resize, on the terminal's owner thread.
 * Reclaims off-screen graphics under pressure without touching the VT parser. */
void rmt_core_maintain(RmtCore *core, bool memory_pressure);

#ifdef __cplusplus
}
#endif
#endif
