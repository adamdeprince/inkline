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
    bool allow_file_images;   /* Existing files only; default false. */
} RmtCoreOptions;

RmtCoreOptions rmt_core_defaults(void);
RmtCore *rmt_core_new(uint16_t cols, uint16_t rows, const RmtCoreOptions *options);
void rmt_core_free(RmtCore *core);
/* Borrowed handle for input encoding, terminal effects and renderer state. */
GhosttyTerminal rmt_core_terminal(RmtCore *core);
const GhosttyAllocator *rmt_core_allocator(RmtCore *core);
size_t rmt_core_memory_used(RmtCore *core);

#ifdef __cplusplus
}
#endif
#endif
