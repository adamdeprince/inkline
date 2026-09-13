#include "rmt/core.h"

#include <png.h>
#include <pthread.h>
#include <limits.h>
#include <stdlib.h>

#define MIB ((size_t)1024 * 1024)
#define MAX_IMAGE_PIXELS (8 * MIB)

struct RmtCore {
    GhosttyTerminal terminal;
    GhosttyAllocator allocator;
    pthread_mutex_t lock;
    size_t used;
    size_t limit;
};

static void *budget_alloc(void *ctx, size_t len, uint8_t alignment, uintptr_t return_address) {
    (void)return_address;
    /* The pinned engine's C bridge passes Zig's log2 alignment, despite
     * allocator.h describing a byte alignment. See src/lib/allocator.zig. */
    if (alignment >= sizeof(size_t) * CHAR_BIT) return NULL;
    const size_t requested_align = (size_t)1 << alignment;
    const size_t align = requested_align < sizeof(void *) ? sizeof(void *) : requested_align;
    RmtCore *core = ctx;
    pthread_mutex_lock(&core->lock);
    if (len > core->limit - core->used) {
        pthread_mutex_unlock(&core->lock);
        return NULL;
    }
    core->used += len;
    pthread_mutex_unlock(&core->lock);
    void *memory = NULL;
    if (posix_memalign(&memory, align, len) != 0) {
        pthread_mutex_lock(&core->lock);
        core->used -= len;
        pthread_mutex_unlock(&core->lock);
    }
    return memory;
}

static bool budget_resize(void *ctx, void *memory, size_t len, uint8_t alignment,
                          size_t new_len, uintptr_t return_address) {
    (void)ctx; (void)memory; (void)len; (void)alignment; (void)new_len; (void)return_address;
    /* Let the engine allocate/copy/free so the budget includes both buffers. */
    return false;
}

static void *budget_remap(void *ctx, void *memory, size_t len, uint8_t alignment,
                         size_t new_len, uintptr_t return_address) {
    (void)ctx; (void)memory; (void)len; (void)alignment; (void)new_len; (void)return_address;
    return NULL;
}

static void budget_free(void *ctx, void *memory, size_t len, uint8_t alignment,
                        uintptr_t return_address) {
    (void)alignment; (void)return_address;
    RmtCore *core = ctx;
    free(memory);
    pthread_mutex_lock(&core->lock);
    core->used -= len;
    pthread_mutex_unlock(&core->lock);
}

static const GhosttyAllocatorVtable budget_vtable = {
    .alloc = budget_alloc, .resize = budget_resize, .remap = budget_remap, .free = budget_free
};

static bool decode_png(void *userdata, const GhosttyAllocator *allocator,
                       const uint8_t *data, size_t len, GhosttySysImage *out) {
    (void)userdata;
    if (len > 32 * MIB) return false;
    png_image image = {.version = PNG_IMAGE_VERSION};
    if (!png_image_begin_read_from_memory(&image, data, len)) {
        png_image_free(&image);
        return false;
    }
    if (!image.width || !image.height || image.width > 4096 || image.height > 4096 ||
        (uint64_t)image.width * image.height > MAX_IMAGE_PIXELS) {
        png_image_free(&image);
        return false;
    }
    image.format = PNG_FORMAT_RGBA;
    const size_t bytes = (size_t)image.width * image.height * 4;
    uint8_t *pixels = ghostty_alloc(allocator, bytes);
    if (!pixels || !png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
        if (pixels) ghostty_free(allocator, pixels, bytes);
        png_image_free(&image);
        return false;
    }
    out->width = image.width;
    out->height = image.height;
    out->data = pixels;
    out->data_len = bytes;
    png_image_free(&image);
    return true;
}

static pthread_once_t png_once = PTHREAD_ONCE_INIT;
static void install_png_decoder(void) {
    ghostty_sys_set(GHOSTTY_SYS_OPT_DECODE_PNG, (const void *)decode_png);
}

RmtCoreOptions rmt_core_defaults(void) {
    return (RmtCoreOptions){
        .memory_bytes = 128 * MIB, .image_bytes = 32 * MIB,
        .scrollback_bytes = 8 * MIB, .allow_file_images = false
    };
}

RmtCore *rmt_core_new(uint16_t cols, uint16_t rows, const RmtCoreOptions *options) {
    const RmtCoreOptions settings = options ? *options : rmt_core_defaults();
    if (!settings.memory_bytes || !cols || !rows) return NULL;
    RmtCore *core = calloc(1, sizeof(*core));
    if (!core) return NULL;
    if (pthread_mutex_init(&core->lock, NULL) != 0) {
        free(core);
        return NULL;
    }
    core->limit = settings.memory_bytes;
    core->allocator = (GhosttyAllocator){.ctx = core, .vtable = &budget_vtable};
    pthread_once(&png_once, install_png_decoder);
    if (ghostty_terminal_new(&core->allocator, &core->terminal, cols, rows) != GHOSTTY_SUCCESS)
        goto fail;
    const bool shared_memory = true;
    const uint64_t image_bytes = settings.image_bytes;
    const size_t apc_bytes = 64 * 1024;
    if (ghostty_terminal_set(core->terminal, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_STORAGE_LIMIT,
                             &image_bytes) != GHOSTTY_SUCCESS ||
        ghostty_terminal_set(core->terminal, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_FILE,
                             &settings.allow_file_images) != GHOSTTY_SUCCESS ||
        ghostty_terminal_set(core->terminal, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_TEMP_FILE,
                             NULL) != GHOSTTY_SUCCESS ||
        ghostty_terminal_set(core->terminal, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_SHARED_MEM,
                             &shared_memory) != GHOSTTY_SUCCESS ||
        ghostty_terminal_set(core->terminal, GHOSTTY_TERMINAL_OPT_APC_MAX_BYTES_KITTY,
                             &apc_bytes) != GHOSTTY_SUCCESS ||
        ghostty_terminal_set(core->terminal, GHOSTTY_TERMINAL_OPT_SCROLLBACK_MAX_BYTES,
                             &settings.scrollback_bytes) != GHOSTTY_SUCCESS)
        goto fail;
    return core;
fail:
    rmt_core_free(core);
    return NULL;
}

void rmt_core_free(RmtCore *core) {
    if (!core) return;
    ghostty_terminal_free(core->terminal);
    pthread_mutex_destroy(&core->lock);
    free(core);
}

GhosttyTerminal rmt_core_terminal(RmtCore *core) { return core ? core->terminal : NULL; }
const GhosttyAllocator *rmt_core_allocator(RmtCore *core) { return core ? &core->allocator : NULL; }

size_t rmt_core_memory_used(RmtCore *core) {
    pthread_mutex_lock(&core->lock);
    size_t used = core->used;
    pthread_mutex_unlock(&core->lock);
    return used;
}
