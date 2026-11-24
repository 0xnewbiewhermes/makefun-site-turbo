#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef STANDALONE_TEST
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>

static inline long sys_open_ro(const char *path) { return open(path, O_RDONLY); }
static inline long sys_open_wo(const char *path) { return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644); }
static inline long sys_read(long fd, void *buf, size_t n) { return read((int)fd, buf, n); }
static inline long sys_write(long fd, const void *buf, size_t n) { return write((int)fd, buf, n); }
static inline long sys_close(long fd) { return close((int)fd); }
static inline long sys_fsize(long fd) { struct stat st; if (fstat((int)fd, &st) < 0) return -1; return (long)st.st_size; }
static inline void *sys_mmap_anon(size_t sz) {
    void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}
static inline void sys_munmap(void *p, size_t sz) { munmap(p, sz); }
static inline void sys_exit_group(int code) { _exit(code); }
#else

#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT  0100
#define O_TRUNC  01000
#define PROT_READ  1
#define PROT_WRITE 2
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20

static inline long sys1(long n, long a1) {
    long r;
    __asm__ volatile ("syscall" : "=a"(r) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return r;
}
static inline long sys2(long n, long a1, long a2) {
    long r;
    __asm__ volatile ("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return r;
}
static inline long sys3(long n, long a1, long a2, long a3) {
    long r;
    __asm__ volatile ("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return r;
}
static inline long sys4(long n, long a1, long a2, long a3, long a4) {
    long r;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile ("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10) : "rcx", "r11", "memory");
    return r;
}
static inline long sys6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    long r;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile ("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return r;
}

static inline long sys_open_ro(const char *path) { return sys4(257, AT_FDCWD, (long)path, O_RDONLY, 0); }
static inline long sys_open_wo(const char *path) { return sys4(257, AT_FDCWD, (long)path, O_WRONLY | O_CREAT | O_TRUNC, 0644); }
static inline long sys_read(long fd, void *buf, size_t n) { return sys3(0, fd, (long)buf, (long)n); }
static inline long sys_write(long fd, const void *buf, size_t n) { return sys3(1, fd, (long)buf, (long)n); }
static inline long sys_close(long fd) { return sys1(3, fd); }
static inline long sys_fsize(long fd) {
    // Linux x86_64 struct stat is 144 bytes, st_size is int64_t at offset 48
    uint8_t st[144];
    long r = sys2(5, fd, (long)st);
    if (r < 0) return -1;
    int64_t sz;
    __builtin_memcpy(&sz, st + 48, 8);
    return (long)sz;
}
static inline void *sys_mmap_anon(size_t sz) {
    long r = sys6(9, 0, (long)sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (r < 0 && r > -4096) return NULL;
    return (void *)r;
}
static inline void sys_munmap(void *p, size_t sz) { sys2(11, (long)p, (long)sz); }
static inline void sys_exit_group(int code) {
    __asm__ volatile ("syscall" : : "a"(231L), "D"((long)code) : "rcx", "r11", "memory");
    __builtin_unreachable();
}

// Fast inline memory primitives (avoid libc calls in freestanding -nostdlib build)
static inline void *fast_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n >= 32) {
        uint64_t a, b, c, e;
        __builtin_memcpy(&a, s, 8); __builtin_memcpy(&b, s + 8, 8);
        __builtin_memcpy(&c, s + 16, 8); __builtin_memcpy(&e, s + 24, 8);
        __builtin_memcpy(d, &a, 8); __builtin_memcpy(d + 8, &b, 8);
        __builtin_memcpy(d + 16, &c, 8); __builtin_memcpy(d + 24, &e, 8);
        s += 32; d += 32; n -= 32;
    }
    if (n >= 16) {
        uint64_t a, b;
        __builtin_memcpy(&a, s, 8); __builtin_memcpy(&b, s + 8, 8);
        __builtin_memcpy(d, &a, 8); __builtin_memcpy(d + 8, &b, 8);
        s += 16; d += 16; n -= 16;
    }
    if (n >= 8) {
        uint64_t a; __builtin_memcpy(&a, s, 8); __builtin_memcpy(d, &a, 8);
        s += 8; d += 8; n -= 8;
    }
    if (n >= 4) {
        uint32_t a; __builtin_memcpy(&a, s, 4); __builtin_memcpy(d, &a, 4);
        s += 4; d += 4; n -= 4;
    }
    while (n--) *d++ = *s++;
    return dst;
}
#endif
