#include "rmt/core.h"

#include <fcntl.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef __linux__
static unsigned long long process_storage_writes(void) {
    FILE *input = fopen("/proc/self/io", "r");
    if (!input) { perror("/proc/self/io"); exit(1); }
    char line[256];
    unsigned long long bytes;
    while (fgets(line, sizeof(line), input)) {
        if (sscanf(line, "write_bytes: %llu", &bytes) == 1) {
            fclose(input);
            return bytes;
        }
    }
    fclose(input);
    fputs("write_bytes counter unavailable\n", stderr);
    exit(1);
}
#endif

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

static char replies[8192];
static size_t reply_bytes;
static char shared_name[128];
static void cleanup_shared_memory(void) {
    if (shared_name[0]) shm_unlink(shared_name);
}
static void reply(GhosttyTerminal terminal, void *userdata, const uint8_t *data, size_t len) {
    (void)terminal; (void)userdata;
    CHECK(len < sizeof(replies) - reply_bytes);
    memcpy(replies + reply_bytes, data, len);
    reply_bytes += len;
    replies[reply_bytes] = 0;
}

static void write_vt(GhosttyTerminal terminal, const char *text) {
    reply_bytes = 0;
    replies[0] = 0;
    ghostty_terminal_vt_write(terminal, (const uint8_t *)text, strlen(text));
}

static GhosttyKittyGraphicsImage image(GhosttyTerminal terminal, uint32_t id) {
    GhosttyKittyGraphics graphics = NULL;
    CHECK(ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_KITTY_GRAPHICS,
                               &graphics) == GHOSTTY_SUCCESS);
    return ghostty_kitty_graphics_image(graphics, id);
}

static void check_red(GhosttyTerminal terminal, uint32_t id) {
    GhosttyKittyGraphicsImage img = image(terminal, id);
    CHECK(img);
    const uint8_t *pixels = NULL;
    size_t len = 0;
    CHECK(ghostty_kitty_graphics_image_get(img, GHOSTTY_KITTY_IMAGE_DATA_DATA_LEN,
                                          &len) == GHOSTTY_SUCCESS);
    CHECK(ghostty_kitty_graphics_image_get(img, GHOSTTY_KITTY_IMAGE_DATA_DATA_PTR,
                                          &pixels) == GHOSTTY_SUCCESS);
    CHECK(len == 4 && pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 255);
}

static void base64(const uint8_t *data, size_t len, char *out) {
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t x = (uint32_t)data[i] << 16;
        if (i + 1 < len) x |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) x |= data[i + 2];
        out[n++] = alphabet[(x >> 18) & 63];
        out[n++] = alphabet[(x >> 12) & 63];
        out[n++] = i + 1 < len ? alphabet[(x >> 6) & 63] : '=';
        out[n++] = i + 2 < len ? alphabet[x & 63] : '=';
    }
    out[n] = 0;
}

static void load_name(GhosttyTerminal terminal, char medium, unsigned id, const char *name) {
    char encoded[1024], sequence[2048];
    CHECK(strlen(name) < 512);
    base64((const uint8_t *)name, strlen(name), encoded);
    snprintf(sequence, sizeof(sequence), "\033_Ga=T,t=%c,f=32,s=1,v=1,i=%u;%s\033\\",
             medium, id, encoded);
    write_vt(terminal, sequence);
}

int main(void) {
#ifdef __linux__
    const bool check_storage = getenv("RMT_CHECK_NO_FLASH_WRITES") != NULL;
    const unsigned long long storage_before = check_storage ? process_storage_writes() : 0;
#endif
    RmtCore *core = rmt_core_new(80, 24, NULL);
    CHECK(core);
    GhosttyTerminal terminal = rmt_core_terminal(core);
    CHECK(ghostty_terminal_resize(terminal, 80, 24, 8, 16) == GHOSTTY_SUCCESS);
    CHECK(ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY,
                               (const void *)reply) == GHOSTTY_SUCCESS);

    write_vt(terminal, "\033_Ga=T,t=d,f=32,s=1,v=1,i=1;/wAA/w==\033\\");
    check_red(terminal, 1);
    CHECK(strstr(replies, "i=1;OK"));

    /* Split both APC framing and image chunks as a PTY/mosh stream can do. */
    const char *chunked = "\033_Ga=T,t=d,f=32,s=1,v=1,i=2,m=1;/wAA\033\\";
    for (size_t i = 0; i < strlen(chunked); ++i)
        ghostty_terminal_vt_write(terminal, (const uint8_t *)chunked + i, 1);
    CHECK(!image(terminal, 2));
    write_vt(terminal, "\033_Gm=0;/w==\033\\");
    check_red(terminal, 2);

    /* Exercise actual PNG decoding entirely through memory buffers. */
    png_image png = {.version = PNG_IMAGE_VERSION, .width = 1, .height = 1, .format = PNG_FORMAT_RGBA};
    const uint8_t red[] = {255, 0, 0, 255};
    uint8_t encoded_png[1024];
    png_alloc_size_t png_bytes = sizeof(encoded_png);
    CHECK(png_image_write_to_memory(&png, encoded_png, &png_bytes, 0, red, 0, NULL));
    char payload[2048], sequence[4096];
    base64(encoded_png, png_bytes, payload);
    snprintf(sequence, sizeof(sequence), "\033_Ga=T,t=d,f=100,i=3;%s\033\\", payload);
    write_vt(terminal, sequence);
    check_red(terminal, 3);

    char path[] = "./tty-graphics-protocol-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0 && write(fd, red, sizeof(red)) == sizeof(red));
    close(fd);
    load_name(terminal, 'f', 4, path);
    CHECK(!image(terminal, 4) && strstr(replies, "i=4;") && !strstr(replies, "OK"));
    load_name(terminal, 't', 5, path);
    CHECK(!image(terminal, 5) && strstr(replies, "i=5;") && !strstr(replies, "OK"));
    CHECK(access(path, F_OK) == 0); /* Rejection must not delete the file. */

    RmtCoreOptions options = rmt_core_defaults();
    options.allow_file_images = true;
    RmtCore *file_core = rmt_core_new(80, 24, &options);
    CHECK(file_core);
    load_name(rmt_core_terminal(file_core), 'f', 6, path);
    check_red(rmt_core_terminal(file_core), 6);
    CHECK(access(path, F_OK) == 0);
    rmt_core_free(file_core);
    CHECK(unlink(path) == 0);

    snprintf(shared_name, sizeof(shared_name), "/rmt-core-test-%ld", (long)getpid());
    fd = shm_open(shared_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(atexit(cleanup_shared_memory) == 0);
    CHECK(fd >= 0 && ftruncate(fd, sizeof(red)) == 0);
    void *shared = mmap(NULL, sizeof(red), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(shared != MAP_FAILED);
    memcpy(shared, red, sizeof(red));
    CHECK(munmap(shared, sizeof(red)) == 0);
    close(fd);
    load_name(terminal, 's', 7, shared_name);
    check_red(terminal, 7);
    fd = shm_open(shared_name, O_RDONLY, 0);
    if (fd >= 0) { close(fd); shm_unlink(shared_name); }
    CHECK(fd < 0); /* Protocol requires consuming the shared-memory name. */

    const size_t baseline = rmt_core_memory_used(core);
    for (unsigned i = 0; i < 1000; ++i)
        write_vt(terminal, "\033_Ga=T,t=d,f=32,s=1,v=1,i=8,q=2,C=1;/wAA/w==\033\\");
    check_red(terminal, 8);
    CHECK(rmt_core_memory_used(core) < baseline + 1024 * 1024);
    rmt_core_free(core);

    options = rmt_core_defaults();
    options.image_bytes = 4;
    core = rmt_core_new(80, 24, &options);
    CHECK(core);
    terminal = rmt_core_terminal(core);
    write_vt(terminal, "\033_Ga=t,f=32,s=1,v=1,i=10;/wAA/w==\033\\");
    write_vt(terminal, "\033_Ga=t,f=32,s=1,v=1,i=11;/wAA/w==\033\\");
    CHECK(!image(terminal, 10));
    check_red(terminal, 11);
    rmt_core_free(core);
    /* A newer image in history is reclaimed before an older visible image. */
    options = rmt_core_defaults();
    core = rmt_core_new(80, 4, &options); CHECK(core);
    terminal = rmt_core_terminal(core);
    CHECK(ghostty_terminal_resize(terminal, 80, 4, 8, 16) == GHOSTTY_SUCCESS);
    write_vt(terminal, "\033_Ga=T,f=32,s=1,v=1,i=20,p=1,C=1;/wAA/w==\033\\");
    write_vt(terminal, "\033_Ga=T,f=32,s=1,v=1,i=21,C=1;/wAA/w==\033\\");
    for (unsigned i = 0; i < 20; ++i) write_vt(terminal, "line\r\n");
    write_vt(terminal, "\033_Ga=p,i=20,p=1,C=1\033\\");
    rmt_core_maintain(core, false); CHECK(image(terminal, 20) && image(terminal, 21));
    const size_t before_reclaim = rmt_core_memory_used(core);
    rmt_core_maintain(core, true);
    CHECK(image(terminal, 20) && !image(terminal, 21));
    CHECK(rmt_core_memory_used(core) < before_reclaim);
    /* Maintenance must not alter a partially received graphics command. */
    write_vt(terminal, "\033_Ga=T,f=32,s=1,v=1,i=22,m=1;/wAA\033\\");
    rmt_core_maintain(core, true);
    write_vt(terminal, "\033_Gm=0;/w==\033\\");
    check_red(terminal, 22);
    rmt_core_free(core);

    /* Retain exactly 500 physical history rows, including partial pages. */
    core = rmt_core_new(80, 24, NULL); CHECK(core);
    terminal = rmt_core_terminal(core);
    GhosttyPoint first_point = {.tag = GHOSTTY_POINT_TAG_ACTIVE, .value.coordinate = {0, 0}};
    GhosttyTrackedGridRef old_line = NULL;
    CHECK(ghostty_terminal_grid_ref_track(terminal, first_point, &old_line) == GHOSTTY_SUCCESS);
    for (unsigned i = 0; i < 1500; ++i) {
        write_vt(terminal, "history line\r\n");
        rmt_core_maintain(core, false);
        size_t lines = 0;
        CHECK(ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_SCROLLBACK_ROWS, &lines) == GHOSTTY_SUCCESS);
        CHECK(lines == (i < 23 ? 0 : i - 22 < 500 ? i - 22 : 500));
    }
    CHECK(!ghostty_tracked_grid_ref_has_value(old_line));
    ghostty_tracked_grid_ref_free(old_line);
    CHECK(ghostty_terminal_resize(terminal, 20, 12, 4, 8) == GHOSTTY_SUCCESS);
    rmt_core_maintain(core, false);
    size_t history_rows = 0;
    CHECK(ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_SCROLLBACK_ROWS, &history_rows) == GHOSTTY_SUCCESS);
    CHECK(history_rows == 500);
    write_vt(terminal, "\033[?1049h");
    for (unsigned i = 0; i < 50; ++i) { write_vt(terminal, "alternate\r\n"); rmt_core_maintain(core, false); }
    CHECK(ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_SCROLLBACK_ROWS, &history_rows) == GHOSTTY_SUCCESS);
    CHECK(history_rows == 0);
    write_vt(terminal, "\033[?1049l");
    CHECK(ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_SCROLLBACK_ROWS, &history_rows) == GHOSTTY_SUCCESS);
    CHECK(history_rows == 500);
    rmt_core_free(core);
    options.memory_bytes = 1;
    CHECK(!rmt_core_new(80, 24, &options));
#ifdef __linux__
    if (check_storage) {
        CHECK(process_storage_writes() == storage_before);
        puts("Linux process I/O: zero storage-write bytes during graphics tests.");
    }
#endif
    puts("Core graphics: inline, chunks, PNG, file policy, shared memory, replacement and budgets passed.");
    return 0;
}
