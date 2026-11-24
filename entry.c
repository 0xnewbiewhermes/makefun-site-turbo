#include <stdint.h>
#include <stddef.h>
#include "sys_linux.h"
#define DEFINE_NFKD_TABLE
#include "nfkd_table.h"

#define STATIC_IN_CAP    (16u * 1024u * 1024u)
#define STATIC_ARENA_CAP (48u * 1024u * 1024u)
#define OUT_BUF_CAP      (256u * 1024u)

static char    g_in_buf[STATIC_IN_CAP + 64] __attribute__((aligned(64)));
static uint8_t g_arena[STATIC_ARENA_CAP]    __attribute__((aligned(64)));
static uint8_t g_out_buf[OUT_BUF_CAP]       __attribute__((aligned(64)));

extern void run_engine_avx2(char *cases_buf, size_t cases_len, uint8_t *arena_mem, size_t arena_cap,
                            uint8_t *out_mem, long out_fd);
extern void run_engine_sse2(char *cases_buf, size_t cases_len, uint8_t *arena_mem, size_t arena_cap,
                            uint8_t *out_mem, long out_fd);

static inline void cpuid_ex(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}

static inline uint64_t xgetbv0(void) {
    uint32_t lo, hi;
    __asm__ volatile ("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return ((uint64_t)hi << 32) | lo;
}

static int has_avx2_bmi2(void) {
    uint32_t a, b, c, d;
    cpuid_ex(0, 0, &a, &b, &c, &d);
    if (a < 7) return 0;
    cpuid_ex(1, 0, &a, &b, &c, &d);
    // Check POPCNT (bit 23), OSXSAVE (bit 27), AVX (bit 28)
    if ((c & ((1u << 23) | (1u << 27) | (1u << 28))) != ((1u << 23) | (1u << 27) | (1u << 28))) return 0;
    // Check OS enabled XMM + YMM state (bits 1 and 2 of XCR0)
    if ((xgetbv0() & 6u) != 6u) return 0;
    cpuid_ex(7, 0, &a, &b, &c, &d);
    // Check BMI1 (bit 3), AVX2 (bit 5), BMI2 (bit 8)
    return (b & ((1u << 3) | (1u << 5) | (1u << 8))) == ((1u << 3) | (1u << 5) | (1u << 8));
}

static int env_match(const char *s, const char *p) {
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

static void join_path(char *dst, size_t cap, const char *dir, const char *file) {
    size_t i = 0;
    while (*dir && i + 2 < cap) dst[i++] = *dir++;
    if (i > 0 && dst[i - 1] != '/' && i + 1 < cap) dst[i++] = '/';
    while (*file && i + 1 < cap) dst[i++] = *file++;
    dst[i] = '\0';
}

__attribute__((constructor))
static void sg_entry(int argc, char **argv, char **envp) {
    (void)argc; (void)argv;
    const char *in_dir = "/input";
    const char *out_dir = "/output";
    if (envp) {
        for (int i = 0; envp[i]; i++) {
            const char *e = envp[i];
            if (e[0] == 'M' && e[1] == 'A' && e[2] == 'K' && e[3] == 'E') {
                if (env_match(e, "MAKEFUN_INPUT_DIR=")) in_dir = e + 18;
                else if (env_match(e, "MAKEFUN_OUTPUT_DIR=")) out_dir = e + 19;
            }
        }
    }

    char in_path[512], out_path[512];
    join_path(in_path, sizeof(in_path), in_dir, "cases.json");
    join_path(out_path, sizeof(out_path), out_dir, "answers.json");

    long ifd = sys_open_ro(in_path);
    if (ifd < 0) sys_exit_group(1);
    long fsz = sys_fsize(ifd);
    if (fsz < 0) sys_exit_group(1);

    char *buf = g_in_buf;
    size_t mmap_sz = 0;
    if ((size_t)fsz > STATIC_IN_CAP) {
        mmap_sz = ((size_t)fsz + 64 + 4095) & ~(size_t)4095;
        buf = (char *)sys_mmap_anon(mmap_sz);
        if (!buf) sys_exit_group(1);
    }

    size_t got = 0;
    while (got < (size_t)fsz) {
        long r = sys_read(ifd, buf + got, (size_t)fsz - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    sys_close(ifd);
    for (int k = 0; k < 64; k++) buf[got + k] = 0;

    long ofd = sys_open_wo(out_path);
    if (ofd < 0) sys_exit_group(1);

    if (has_avx2_bmi2()) {
        run_engine_avx2(buf, got, g_arena, STATIC_ARENA_CAP, g_out_buf, ofd);
    } else {
        run_engine_sse2(buf, got, g_arena, STATIC_ARENA_CAP, g_out_buf, ofd);
    }

    sys_close(ofd);
    sys_exit_group(0);
}
