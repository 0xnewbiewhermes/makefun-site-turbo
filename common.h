#pragma once
#include <stdint.h>
#include <stddef.h>
#include <immintrin.h>
#include "sys_linux.h"
#include "nfkd_table.h"

// ---- Freestanding memory helpers (avoid any libc symbol emissions) ----
static inline void xmemcpy(void *restrict dst, const void *restrict src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
#ifdef __AVX2__
    while (n >= 32) {
        __m256i v = _mm256_loadu_si256((const __m256i *)s);
        _mm256_storeu_si256((__m256i *)d, v);
        s += 32; d += 32; n -= 32;
    }
#endif
    while (n >= 16) {
        __m128i v = _mm_loadu_si128((const __m128i *)s);
        _mm_storeu_si128((__m128i *)d, v);
        s += 16; d += 16; n -= 16;
    }
    if (n >= 8) {
        uint64_t v; __builtin_memcpy(&v, s, 8); __builtin_memcpy(d, &v, 8);
        s += 8; d += 8; n -= 8;
    }
    if (n >= 4) {
        uint32_t v; __builtin_memcpy(&v, s, 4); __builtin_memcpy(d, &v, 4);
        s += 4; d += 4; n -= 4;
    }
    while (n--) *d++ = *s++;
}

static inline void xmemset(void *dst, uint8_t val, size_t n) {
    uint8_t *d = (uint8_t *)dst;
#ifdef __AVX2__
    if (n >= 32) {
        __m256i v = _mm256_set1_epi8((char)val);
        while (n >= 32) { _mm256_storeu_si256((__m256i *)d, v); d += 32; n -= 32; }
    }
#endif
    if (n >= 16) {
        __m128i v = _mm_set1_epi8((char)val);
        while (n >= 16) { _mm_storeu_si128((__m128i *)d, v); d += 16; n -= 16; }
    }
    while (n--) *d++ = val;
}

static inline int xmemcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p = (const uint8_t *)a;
    const uint8_t *q = (const uint8_t *)b;
    while (n >= 8) {
        uint64_t va, vb;
        __builtin_memcpy(&va, p, 8);
        __builtin_memcpy(&vb, q, 8);
        if (va != vb) {
            uint64_t diff = va ^ vb;
            int idx = __builtin_ctzll(diff) >> 3;
            return (int)p[idx] - (int)q[idx];
        }
        p += 8; q += 8; n -= 8;
    }
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}

static inline int xmemeq(const void *a, const void *b, size_t n) {
    const uint8_t *p = (const uint8_t *)a;
    const uint8_t *q = (const uint8_t *)b;
    while (n >= 8) {
        uint64_t va, vb;
        __builtin_memcpy(&va, p, 8);
        __builtin_memcpy(&vb, q, 8);
        if (va != vb) return 0;
        p += 8; q += 8; n -= 8;
    }
    if (n >= 4) {
        uint32_t va, vb;
        __builtin_memcpy(&va, p, 4);
        __builtin_memcpy(&vb, q, 4);
        if (va != vb) return 0;
        p += 4; q += 4; n -= 4;
    }
    while (n--) { if (*p++ != *q++) return 0; }
    return 1;
}

// ---- String slice ----
typedef struct {
    const char *p;
    uint32_t len;
} Str;

static inline Str str_make(const char *p, uint32_t len) { Str s = {p, len}; return s; }
static inline Str str_lit(const char *p) {
    uint32_t n = 0; while (p[n]) n++;
    Str s = {p, n}; return s;
}
static inline int str_eq(Str a, Str b) {
    return a.len == b.len && (a.len == 0 || xmemeq(a.p, b.p, a.len));
}
static inline int str_eq_lit(Str a, const char *lit, uint32_t llen) {
    return a.len == llen && xmemeq(a.p, lit, llen);
}
static inline int str_starts(Str a, const char *pre, uint32_t plen) {
    return a.len >= plen && xmemeq(a.p, pre, plen);
}
static inline int str_ends(Str a, const char *suf, uint32_t slen) {
    return a.len >= slen && xmemeq(a.p + a.len - slen, suf, slen);
}

// ---- UTF-8 decode & UTF-16 code unit comparison ----
static inline uint32_t utf8_decode(const uint8_t *s, uint32_t len, uint32_t *adv) {
    uint8_t c = s[0];
    if (c < 0x80) { *adv = 1; return c; }
    if ((c & 0xE0) == 0xC0 && len >= 2) {
        *adv = 2;
        return ((uint32_t)(c & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && len >= 3) {
        *adv = 3;
        return ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && len >= 4) {
        *adv = 4;
        return ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
               ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    *adv = 1;
    return c;
}

static inline uint32_t utf8_encode(uint32_t cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

// Exact JS UTF-16 code unit order comparison for UTF-8 strings
static int cmp_utf16(Str a, Str b) {
    uint32_t minlen = a.len < b.len ? a.len : b.len;
    const uint8_t *pa = (const uint8_t *)a.p;
    const uint8_t *pb = (const uint8_t *)b.p;
    uint32_t i = 0;
    while (i < minlen && pa[i] == pb[i]) i++;
    if (i == minlen) return (a.len < b.len) ? -1 : (a.len > b.len) ? 1 : 0;
    // Fast path: if differing bytes are < 0xEE (so neither is 3-byte E000+ vs 4-byte F0+)
    // Back up if inside a UTF-8 continuation byte (0x80..0xBF)
    uint8_t ca = pa[i], cb = pb[i];
    if (ca < 0x80 && cb < 0x80) return (int)ca - (int)cb;
    while (i > 0 && (pa[i] & 0xC0) == 0x80) i--;
    uint32_t adva, advb;
    uint32_t cpa = utf8_decode(pa + i, a.len - i, &adva);
    uint32_t cpb = utf8_decode(pb + i, b.len - i, &advb);
    uint16_t ua = (cpa >= 0x10000) ? (uint16_t)(0xD800 + ((cpa - 0x10000) >> 10)) : (uint16_t)cpa;
    uint16_t ub = (cpb >= 0x10000) ? (uint16_t)(0xD800 + ((cpb - 0x10000) >> 10)) : (uint16_t)cpb;
    if (ua != ub) return (int)ua - (int)ub;
    uint16_t la = (cpa >= 0x10000) ? (uint16_t)(0xDC00 + ((cpa - 0x10000) & 0x3FF)) : 0;
    uint16_t lb = (cpb >= 0x10000) ? (uint16_t)(0xDC00 + ((cpb - 0x10000) & 0x3FF)) : 0;
    return (int)la - (int)lb;
}

// ---- Exact JS \s whitespace check ----
static inline int is_js_ws_cp(uint32_t cp) {
    if (cp <= 0x20) return cp == 0x20 || (cp >= 0x09 && cp <= 0x0D);
    if (cp < 0xA0) return 0;
    if (cp == 0xA0 || cp == 0x1680 || cp == 0x2028 || cp == 0x2029 ||
        cp == 0x202F || cp == 0x205F || cp == 0x3000 || cp == 0xFEFF) return 1;
    return cp >= 0x2000 && cp <= 0x200A;
}

static inline int is_ascii_ws(uint8_t c) {
    return c == ' ' || (c >= 0x09 && c <= 0x0D);
}

static inline int has_js_ws(Str s) {
    const uint8_t *p = (const uint8_t *)s.p;
    uint32_t i = 0;
    while (i < s.len) {
        if (p[i] < 0x80) {
            if (is_ascii_ws(p[i])) return 1;
            i++;
        } else {
            uint32_t adv;
            uint32_t cp = utf8_decode(p + i, s.len - i, &adv);
            if (is_js_ws_cp(cp)) return 1;
            i += adv;
        }
    }
    return 0;
}

static inline Str str_trim_js(Str s) {
    const uint8_t *p = (const uint8_t *)s.p;
    uint32_t a = 0, b = s.len;
    while (a < b) {
        if (p[a] < 0x80) {
            if (!is_ascii_ws(p[a])) break;
            a++;
        } else {
            uint32_t adv;
            uint32_t cp = utf8_decode(p + a, b - a, &adv);
            if (!is_js_ws_cp(cp)) break;
            a += adv;
        }
    }
    while (b > a) {
        if (p[b - 1] < 0x80) {
            if (!is_ascii_ws(p[b - 1])) break;
            b--;
        } else {
            uint32_t st = b - 1;
            while (st > a && (p[st] & 0xC0) == 0x80) st--;
            uint32_t adv;
            uint32_t cp = utf8_decode(p + st, b - st, &adv);
            if (!is_js_ws_cp(cp)) break;
            b = st;
        }
    }
    return str_make(s.p + a, b - a);
}

static inline Str str_trim_hws(Str s) {
    const char *p = s.p;
    uint32_t a = 0, b = s.len;
    while (a < b && (p[a] == ' ' || p[a] == '\t')) a++;
    while (b > a && (p[b - 1] == ' ' || p[b - 1] == '\t')) b--;
    return str_make(p + a, b - a);
}

// ---- Arena allocator (bump pointer over static .bss with overflow mmap safety) ----
typedef struct {
    uint8_t *base;
    size_t pos;
    size_t cap;
    uint8_t *extra_ptrs[16];
    size_t extra_sz[16];
    int extra_cnt;
} Arena;

static inline void arena_init(Arena *a, uint8_t *buf, size_t cap) {
    a->base = buf; a->pos = 0; a->cap = cap; a->extra_cnt = 0;
}

static inline void arena_reset(Arena *a) {
    a->pos = 0;
    for (int i = 0; i < a->extra_cnt; i++) sys_munmap(a->extra_ptrs[i], a->extra_sz[i]);
    a->extra_cnt = 0;
}

static void *arena_alloc_slow(Arena *a, size_t sz) {
    size_t msz = (sz + 4095) & ~(size_t)4095;
    if (msz < (4u << 20)) msz = (4u << 20);
    void *p = sys_mmap_anon(msz);
    if (a->extra_cnt < 16) {
        a->extra_ptrs[a->extra_cnt] = (uint8_t *)p;
        a->extra_sz[a->extra_cnt] = msz;
        a->extra_cnt++;
    }
    a->base = (uint8_t *)p;
    a->cap = msz;
    a->pos = sz;
    return p;
}

static inline void *arena_alloc(Arena *a, size_t sz) {
    size_t p = (a->pos + 7u) & ~(size_t)7u;
    if (__builtin_expect(p + sz <= a->cap, 1)) {
        void *r = a->base + p;
        a->pos = p + sz;
        return r;
    }
    return arena_alloc_slow(a, sz);
}

static inline char *arena_alloc_bytes(Arena *a, size_t sz) {
    if (__builtin_expect(a->pos + sz <= a->cap, 1)) {
        char *r = (char *)(a->base + a->pos);
        a->pos += sz;
        return r;
    }
    return (char *)arena_alloc_slow(a, sz);
}

static inline Str arena_dup(Arena *a, const char *p, uint32_t len) {
    char *d = arena_alloc_bytes(a, len);
    if (len) xmemcpy(d, p, len);
    return str_make(d, len);
}

// ---- Dynamic String Builder backed by Arena ----
typedef struct {
    char *buf;
    uint32_t len;
    uint32_t cap;
    Arena *arena;
} SB;

static inline void sb_init(SB *sb, Arena *a, uint32_t cap) {
    sb->arena = a;
    sb->len = 0;
    sb->cap = cap;
    sb->buf = cap ? arena_alloc_bytes(a, cap) : NULL;
}

static void sb_grow(SB *sb, uint32_t need) {
    uint32_t ncap = sb->cap ? sb->cap * 2 : 256;
    while (ncap < sb->len + need) ncap *= 2;
    // Fast path: if sb->buf is at top of arena, just extend in place!
    if (sb->buf && (uint8_t *)sb->buf + sb->cap == sb->arena->base + sb->arena->pos &&
        sb->arena->pos + (ncap - sb->cap) <= sb->arena->cap) {
        sb->arena->pos += (ncap - sb->cap);
        sb->cap = ncap;
        return;
    }
    char *nb = arena_alloc_bytes(sb->arena, ncap);
    if (sb->len) xmemcpy(nb, sb->buf, sb->len);
    sb->buf = nb;
    sb->cap = ncap;
}

static inline void sb_reserve(SB *sb, uint32_t need) {
    if (__builtin_expect(sb->len + need > sb->cap, 0)) sb_grow(sb, need);
}

static inline void sb_putc(SB *sb, char c) {
    if (__builtin_expect(sb->len >= sb->cap, 0)) sb_grow(sb, 1);
    sb->buf[sb->len++] = c;
}

static inline void sb_puts(SB *sb, const char *p, uint32_t n) {
    if (!n) return;
    if (__builtin_expect(sb->len + n > sb->cap, 0)) sb_grow(sb, n);
    xmemcpy(sb->buf + sb->len, p, n);
    sb->len += n;
}

static inline void sb_putu32(SB *sb, uint32_t v) {
    char tmp[12];
    int k = 12;
    do { tmp[--k] = (char)('0' + (v % 10)); v /= 10; } while (v);
    sb_puts(sb, tmp + k, (uint32_t)(12 - k));
}

static inline Str sb_finish(SB *sb) {
    return str_make(sb->buf, sb->len);
}

// ---- Fast SIMD HTML escape into SB ----
static inline void sb_put_html_escaped(SB *sb, const char *p, uint32_t len) {
    if (!len) return;
    sb_reserve(sb, len + 32);
    uint32_t i = 0;
    const char *s = p;
    while (i < len) {
        uint32_t start = i;
#ifdef __AVX2__
        const __m256i v_amp = _mm256_set1_epi8('&');
        const __m256i v_lt  = _mm256_set1_epi8('<');
        const __m256i v_gt  = _mm256_set1_epi8('>');
        const __m256i v_dq  = _mm256_set1_epi8('"');
        const __m256i v_sq  = _mm256_set1_epi8('\'');
        while (i + 32 <= len) {
            __m256i v = _mm256_loadu_si256((const __m256i *)(s + i));
            __m256i m = _mm256_or_si256(
                _mm256_or_si256(_mm256_cmpeq_epi8(v, v_amp), _mm256_cmpeq_epi8(v, v_lt)),
                _mm256_or_si256(_mm256_cmpeq_epi8(v, v_gt),
                    _mm256_or_si256(_mm256_cmpeq_epi8(v, v_dq), _mm256_cmpeq_epi8(v, v_sq))));
            uint32_t mask = (uint32_t)_mm256_movemask_epi8(m);
            if (mask) { i += (uint32_t)__builtin_ctz(mask); goto found_esc; }
            i += 32;
        }
#endif
        {
            const __m128i v_amp = _mm_set1_epi8('&');
            const __m128i v_lt  = _mm_set1_epi8('<');
            const __m128i v_gt  = _mm_set1_epi8('>');
            const __m128i v_dq  = _mm_set1_epi8('"');
            const __m128i v_sq  = _mm_set1_epi8('\'');
            while (i + 16 <= len) {
                __m128i v = _mm_loadu_si128((const __m128i *)(s + i));
                __m128i m = _mm_or_si128(
                    _mm_or_si128(_mm_cmpeq_epi8(v, v_amp), _mm_cmpeq_epi8(v, v_lt)),
                    _mm_or_si128(_mm_cmpeq_epi8(v, v_gt),
                        _mm_or_si128(_mm_cmpeq_epi8(v, v_dq), _mm_cmpeq_epi8(v, v_sq))));
                uint32_t mask = (uint32_t)_mm_movemask_epi8(m);
                if (mask) { i += (uint32_t)__builtin_ctz(mask); goto found_esc; }
                i += 16;
            }
        }
        while (i < len) {
            char c = s[i];
            if (c == '&' || c == '<' || c == '>' || c == '"' || c == '\'') goto found_esc;
            i++;
        }
        if (i > start) sb_puts(sb, s + start, i - start);
        break;
    found_esc:
        if (i > start) sb_puts(sb, s + start, i - start);
        char c = s[i++];
        if (c == '&') sb_puts(sb, "&amp;", 5);
        else if (c == '<') sb_puts(sb, "&lt;", 4);
        else if (c == '>') sb_puts(sb, "&gt;", 4);
        else if (c == '"') sb_puts(sb, "&quot;", 6);
        else sb_puts(sb, "&#39;", 5);
    }
}

// ---- Streaming L2-cache Output Buffer (direct JSON emitter!) ----
#define OUT_BUF_CAP (512u * 1024u)

typedef struct {
    uint8_t *buf;
    uint32_t pos;
    long fd;
} OutBuf;

static void out_flush(OutBuf *o) {
    uint32_t off = 0;
    while (off < o->pos) {
        long w = sys_write(o->fd, o->buf + off, o->pos - off);
        if (w <= 0) break;
        off += (uint32_t)w;
    }
    o->pos = 0;
}

static inline void out_ensure(OutBuf *o, uint32_t n) {
    if (__builtin_expect(o->pos + n > OUT_BUF_CAP, 0)) out_flush(o);
}

static inline void out_putc(OutBuf *o, char c) {
    if (__builtin_expect(o->pos >= OUT_BUF_CAP, 0)) out_flush(o);
    o->buf[o->pos++] = (uint8_t)c;
}

static inline void out_raw(OutBuf *o, const char *p, uint32_t n) {
    if (__builtin_expect(o->pos + n <= OUT_BUF_CAP, 1)) {
        if (n) xmemcpy(o->buf + o->pos, p, n);
        o->pos += n;
        return;
    }
    out_flush(o);
    if (n >= OUT_BUF_CAP) {
        uint32_t off = 0;
        while (off < n) {
            long w = sys_write(o->fd, p + off, n - off);
            if (w <= 0) break;
            off += (uint32_t)w;
        }
    } else {
        xmemcpy(o->buf, p, n);
        o->pos = n;
    }
}

static inline void out_putu32(OutBuf *o, uint32_t v) {
    char tmp[12];
    int k = 12;
    do { tmp[--k] = (char)('0' + (v % 10)); v /= 10; } while (v);
    out_raw(o, tmp + k, (uint32_t)(12 - k));
}

static const char HEX_DIGITS[17] = "0123456789abcdef";

// Emit JSON-escaped string body (without surrounding quotes) into OutBuf using register-cached write ptr + SIMD!
static void out_json_body(OutBuf *o, const char *s, uint32_t len) {
    uint32_t i = 0;
    if (__builtin_expect(o->pos + len + 64 > OUT_BUF_CAP, 0)) out_flush(o);
    uint8_t *op = o->buf + o->pos;
    uint8_t *oend = o->buf + OUT_BUF_CAP - 64;
#ifdef __AVX2__
    if (len >= 32) {
        const __m256i v_dq   = _mm256_set1_epi8('"');
        const __m256i v_bs   = _mm256_set1_epi8('\\');
        const __m256i v_80   = _mm256_set1_epi8((char)0x80);
        const __m256i v_sp_u = _mm256_set1_epi8((char)(0x20 ^ 0x80));
        while (i + 32 <= len) {
            if (__builtin_expect(op > oend, 0)) {
                o->pos = (uint32_t)(op - o->buf); out_flush(o); op = o->buf;
            }
            __m256i v = _mm256_loadu_si256((const __m256i *)(s + i));
            __m256i vu = _mm256_xor_si256(v, v_80);
            __m256i m = _mm256_or_si256(_mm256_cmpgt_epi8(v_sp_u, vu),
                        _mm256_or_si256(_mm256_cmpeq_epi8(v, v_dq), _mm256_cmpeq_epi8(v, v_bs)));
            uint32_t mask = (uint32_t)_mm256_movemask_epi8(m);
            _mm256_storeu_si256((__m256i *)op, v);
            if (!mask) { op += 32; i += 32; continue; }
            uint32_t adv = (uint32_t)__builtin_ctz(mask);
            op += adv; i += adv;
            uint8_t c = (uint8_t)s[i++];
            if (c == '"') { op[0] = '\\'; op[1] = '"'; op += 2; }
            else if (c == '\\') { op[0] = '\\'; op[1] = '\\'; op += 2; }
            else if (c == '\n') { op[0] = '\\'; op[1] = 'n'; op += 2; }
            else if (c == '\r') { op[0] = '\\'; op[1] = 'r'; op += 2; }
            else if (c == '\t') { op[0] = '\\'; op[1] = 't'; op += 2; }
            else if (c == '\b') { op[0] = '\\'; op[1] = 'b'; op += 2; }
            else if (c == '\f') { op[0] = '\\'; op[1] = 'f'; op += 2; }
            else { op[0]='\\'; op[1]='u'; op[2]='0'; op[3]='0'; op[4]=(uint8_t)HEX_DIGITS[c>>4]; op[5]=(uint8_t)HEX_DIGITS[c&15]; op += 6; }
        }
    }
#else
    if (len >= 16) {
        const __m128i v_dq   = _mm_set1_epi8('"');
        const __m128i v_bs   = _mm_set1_epi8('\\');
        const __m128i v_80   = _mm_set1_epi8((char)0x80);
        const __m128i v_sp_u = _mm_set1_epi8((char)(0x20 ^ 0x80));
        while (i + 16 <= len) {
            if (__builtin_expect(op > oend, 0)) {
                o->pos = (uint32_t)(op - o->buf); out_flush(o); op = o->buf;
            }
            __m128i v = _mm_loadu_si128((const __m128i *)(s + i));
            __m128i vu = _mm_xor_si128(v, v_80);
            __m128i m = _mm_or_si128(_mm_cmpgt_epi8(v_sp_u, vu),
                        _mm_or_si128(_mm_cmpeq_epi8(v, v_dq), _mm_cmpeq_epi8(v, v_bs)));
            uint32_t mask = (uint32_t)_mm_movemask_epi8(m);
            _mm_storeu_si128((__m128i *)op, v);
            if (!mask) { op += 16; i += 16; continue; }
            uint32_t adv = (uint32_t)__builtin_ctz(mask);
            op += adv; i += adv;
            uint8_t c = (uint8_t)s[i++];
            if (c == '"') { op[0] = '\\'; op[1] = '"'; op += 2; }
            else if (c == '\\') { op[0] = '\\'; op[1] = '\\'; op += 2; }
            else if (c == '\n') { op[0] = '\\'; op[1] = 'n'; op += 2; }
            else if (c == '\r') { op[0] = '\\'; op[1] = 'r'; op += 2; }
            else if (c == '\t') { op[0] = '\\'; op[1] = 't'; op += 2; }
            else if (c == '\b') { op[0] = '\\'; op[1] = 'b'; op += 2; }
            else if (c == '\f') { op[0] = '\\'; op[1] = 'f'; op += 2; }
            else { op[0]='\\'; op[1]='u'; op[2]='0'; op[3]='0'; op[4]=(uint8_t)HEX_DIGITS[c>>4]; op[5]=(uint8_t)HEX_DIGITS[c&15]; op += 6; }
        }
    }
#endif
    while (i < len) {
        if (__builtin_expect(op > oend, 0)) {
            o->pos = (uint32_t)(op - o->buf); out_flush(o); op = o->buf;
        }
        uint8_t c = (uint8_t)s[i++];
        if (__builtin_expect(c >= 0x20 && c != '"' && c != '\\', 1)) {
            *op++ = c;
        } else if (c == '"') { op[0] = '\\'; op[1] = '"'; op += 2; }
        else if (c == '\\') { op[0] = '\\'; op[1] = '\\'; op += 2; }
        else if (c == '\n') { op[0] = '\\'; op[1] = 'n'; op += 2; }
        else if (c == '\r') { op[0] = '\\'; op[1] = 'r'; op += 2; }
        else if (c == '\t') { op[0] = '\\'; op[1] = 't'; op += 2; }
        else if (c == '\b') { op[0] = '\\'; op[1] = 'b'; op += 2; }
        else if (c == '\f') { op[0] = '\\'; op[1] = 'f'; op += 2; }
        else { op[0]='\\'; op[1]='u'; op[2]='0'; op[3]='0'; op[4]=(uint8_t)HEX_DIGITS[c>>4]; op[5]=(uint8_t)HEX_DIGITS[c&15]; op += 6; }
    }
    o->pos = (uint32_t)(op - o->buf);
}

// Emit HTML-escaped AND JSON-escaped string body directly into OutBuf (for {{ var }}) using register-cached write ptr + nibble-LUT SIMD!
static void out_json_html_body(OutBuf *o, const char *s, uint32_t len) {
    uint32_t i = 0;
    if (__builtin_expect(o->pos + len + 64 > OUT_BUF_CAP, 0)) out_flush(o);
    uint8_t *op = o->buf + o->pos;
    uint8_t *oend = o->buf + OUT_BUF_CAP - 64;
#ifdef __AVX2__
    if (len >= 32) {
        const __m256i lo_lut = _mm256_setr_epi8(
            0x10, 0x10, 0x11, 0x10, 0x10, 0x10, 0x11, 0x11, 0x10, 0x10, 0x10, 0x10, 0x16, 0x10, 0x12, 0x10,
            0x10, 0x10, 0x11, 0x10, 0x10, 0x10, 0x11, 0x11, 0x10, 0x10, 0x10, 0x10, 0x16, 0x10, 0x12, 0x10);
        const __m256i hi_lut = _mm256_setr_epi8(
            0x10, 0x10, 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x10, 0x10, 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
        const __m256i v_0f   = _mm256_set1_epi8(0x0f);
        const __m256i v_zero = _mm256_setzero_si256();
        while (i + 32 <= len) {
            if (__builtin_expect(op > oend, 0)) {
                o->pos = (uint32_t)(op - o->buf); out_flush(o); op = o->buf;
            }
            __m256i v = _mm256_loadu_si256((const __m256i *)(s + i));
            __m256i hi = _mm256_and_si256(_mm256_srli_epi16(v, 4), v_0f);
            __m256i hit = _mm256_and_si256(_mm256_shuffle_epi8(lo_lut, v), _mm256_shuffle_epi8(hi_lut, hi));
            uint32_t mask = ~(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(hit, v_zero));
            _mm256_storeu_si256((__m256i *)op, v);
            if (!mask) { op += 32; i += 32; continue; }
            uint32_t adv = (uint32_t)__builtin_ctz(mask);
            op += adv; i += adv;
            uint8_t c = (uint8_t)s[i++];
            if (c == '&') { __builtin_memcpy(op, "&amp;", 5); op += 5; }
            else if (c == '<') { __builtin_memcpy(op, "&lt;", 4); op += 4; }
            else if (c == '>') { __builtin_memcpy(op, "&gt;", 4); op += 4; }
            else if (c == '"') { __builtin_memcpy(op, "&quot;", 6); op += 6; }
            else if (c == '\'') { __builtin_memcpy(op, "&#39;", 5); op += 5; }
            else if (c == '\\') { op[0] = '\\'; op[1] = '\\'; op += 2; }
            else if (c == '\n') { op[0] = '\\'; op[1] = 'n'; op += 2; }
            else if (c == '\r') { op[0] = '\\'; op[1] = 'r'; op += 2; }
            else if (c == '\t') { op[0] = '\\'; op[1] = 't'; op += 2; }
            else if (c == '\b') { op[0] = '\\'; op[1] = 'b'; op += 2; }
            else if (c == '\f') { op[0] = '\\'; op[1] = 'f'; op += 2; }
            else { op[0]='\\'; op[1]='u'; op[2]='0'; op[3]='0'; op[4]=(uint8_t)HEX_DIGITS[c>>4]; op[5]=(uint8_t)HEX_DIGITS[c&15]; op += 6; }
        }
    }
#else
    if (len >= 16) {
        const __m128i v_80   = _mm_set1_epi8((char)0x80);
        const __m128i v_sp_u = _mm_set1_epi8((char)(0x20 ^ 0x80));
        const __m128i v_dq   = _mm_set1_epi8('"');
        const __m128i v_bs   = _mm_set1_epi8('\\');
        const __m128i v_amp  = _mm_set1_epi8('&');
        const __m128i v_lt   = _mm_set1_epi8('<');
        const __m128i v_gt   = _mm_set1_epi8('>');
        const __m128i v_sq   = _mm_set1_epi8('\'');
        while (i + 16 <= len) {
            if (__builtin_expect(op > oend, 0)) {
                o->pos = (uint32_t)(op - o->buf); out_flush(o); op = o->buf;
            }
            __m128i v = _mm_loadu_si128((const __m128i *)(s + i));
            __m128i vu = _mm_xor_si128(v, v_80);
            __m128i m1 = _mm_or_si128(_mm_cmpgt_epi8(v_sp_u, vu), _mm_cmpeq_epi8(v, v_dq));
            __m128i m2 = _mm_or_si128(_mm_cmpeq_epi8(v, v_bs), _mm_cmpeq_epi8(v, v_amp));
            __m128i m3 = _mm_or_si128(_mm_cmpeq_epi8(v, v_lt), _mm_cmpeq_epi8(v, v_gt));
            __m128i m  = _mm_or_si128(_mm_or_si128(m1, m2), _mm_or_si128(m3, _mm_cmpeq_epi8(v, v_sq)));
            uint32_t mask = (uint32_t)_mm_movemask_epi8(m);
            _mm_storeu_si128((__m128i *)op, v);
            if (!mask) { op += 16; i += 16; continue; }
            uint32_t adv = (uint32_t)__builtin_ctz(mask);
            op += adv; i += adv;
            uint8_t c = (uint8_t)s[i++];
            if (c == '&') { __builtin_memcpy(op, "&amp;", 5); op += 5; }
            else if (c == '<') { __builtin_memcpy(op, "&lt;", 4); op += 4; }
            else if (c == '>') { __builtin_memcpy(op, "&gt;", 4); op += 4; }
            else if (c == '"') { __builtin_memcpy(op, "&quot;", 6); op += 6; }
            else if (c == '\'') { __builtin_memcpy(op, "&#39;", 5); op += 5; }
            else if (c == '\\') { op[0] = '\\'; op[1] = '\\'; op += 2; }
            else if (c == '\n') { op[0] = '\\'; op[1] = 'n'; op += 2; }
            else if (c == '\r') { op[0] = '\\'; op[1] = 'r'; op += 2; }
            else if (c == '\t') { op[0] = '\\'; op[1] = 't'; op += 2; }
            else if (c == '\b') { op[0] = '\\'; op[1] = 'b'; op += 2; }
            else if (c == '\f') { op[0] = '\\'; op[1] = 'f'; op += 2; }
            else { op[0]='\\'; op[1]='u'; op[2]='0'; op[3]='0'; op[4]=(uint8_t)HEX_DIGITS[c>>4]; op[5]=(uint8_t)HEX_DIGITS[c&15]; op += 6; }
        }
    }
#endif
    while (i < len) {
        if (__builtin_expect(op > oend, 0)) {
            o->pos = (uint32_t)(op - o->buf); out_flush(o); op = o->buf;
        }
        uint8_t c = (uint8_t)s[i++];
        if (__builtin_expect(c >= 0x20 && c != '"' && c != '\\' && c != '&' && c != '<' && c != '>' && c != '\'', 1)) {
            *op++ = c;
        } else if (c == '&') { __builtin_memcpy(op, "&amp;", 5); op += 5; }
        else if (c == '<') { __builtin_memcpy(op, "&lt;", 4); op += 4; }
        else if (c == '>') { __builtin_memcpy(op, "&gt;", 4); op += 4; }
        else if (c == '"') { __builtin_memcpy(op, "&quot;", 6); op += 6; }
        else if (c == '\'') { __builtin_memcpy(op, "&#39;", 5); op += 5; }
        else if (c == '\\') { op[0] = '\\'; op[1] = '\\'; op += 2; }
        else if (c == '\n') { op[0] = '\\'; op[1] = 'n'; op += 2; }
        else if (c == '\r') { op[0] = '\\'; op[1] = 'r'; op += 2; }
        else if (c == '\t') { op[0] = '\\'; op[1] = 't'; op += 2; }
        else if (c == '\b') { op[0] = '\\'; op[1] = 'b'; op += 2; }
        else if (c == '\f') { op[0] = '\\'; op[1] = 'f'; op += 2; }
        else { op[0]='\\'; op[1]='u'; op[2]='0'; op[3]='0'; op[4]=(uint8_t)HEX_DIGITS[c>>4]; op[5]=(uint8_t)HEX_DIGITS[c&15]; op += 6; }
    }
    o->pos = (uint32_t)(op - o->buf);
}

// Also JSON-escape into an SB (for pre-escaping template literals & card cache!)
static void sb_put_json_body(SB *sb, const char *s, uint32_t len) {
    uint32_t i = 0;
    while (i < len) {
        uint32_t start = i;
        while (i < len) {
            uint8_t c = (uint8_t)s[i];
            if (c < 0x20 || c == '"' || c == '\\') break;
            i++;
        }
        if (i > start) sb_puts(sb, s + start, i - start);
        if (i >= len) break;
        uint8_t c = (uint8_t)s[i++];
        if (c == '"') sb_puts(sb, "\\\"", 2);
        else if (c == '\\') sb_puts(sb, "\\\\", 2);
        else if (c == '\n') sb_puts(sb, "\\n", 2);
        else if (c == '\r') sb_puts(sb, "\\r", 2);
        else if (c == '\t') sb_puts(sb, "\\t", 2);
        else if (c == '\b') sb_puts(sb, "\\b", 2);
        else if (c == '\f') sb_puts(sb, "\\f", 2);
        else {
            char tmp[6] = {'\\','u','0','0', HEX_DIGITS[c >> 4], HEX_DIGITS[c & 15]};
            sb_puts(sb, tmp, 6);
        }
    }
}

static void sb_put_json_html_body(SB *sb, const char *s, uint32_t len) {
    uint32_t i = 0;
    while (i < len) {
        uint32_t start = i;
        while (i < len) {
            uint8_t c = (uint8_t)s[i];
            if (c < 0x20 || c == '"' || c == '\\' || c == '&' || c == '<' || c == '>' || c == '\'') break;
            i++;
        }
        if (i > start) sb_puts(sb, s + start, i - start);
        if (i >= len) break;
        uint8_t c = (uint8_t)s[i++];
        if (c == '&') sb_puts(sb, "&amp;", 5);
        else if (c == '<') sb_puts(sb, "&lt;", 4);
        else if (c == '>') sb_puts(sb, "&gt;", 4);
        else if (c == '"') sb_puts(sb, "&quot;", 6);
        else if (c == '\'') sb_puts(sb, "&#39;", 5);
        else if (c == '\\') sb_puts(sb, "\\\\", 2);
        else if (c == '\n') sb_puts(sb, "\\n", 2);
        else if (c == '\r') sb_puts(sb, "\\r", 2);
        else if (c == '\t') sb_puts(sb, "\\t", 2);
        else if (c == '\b') sb_puts(sb, "\\b", 2);
        else if (c == '\f') sb_puts(sb, "\\f", 2);
        else {
            char tmp[6] = {'\\','u','0','0', HEX_DIGITS[c >> 4], HEX_DIGITS[c & 15]};
            sb_puts(sb, tmp, 6);
        }
    }
}

// ---- Exact Node 22 slugify(text) ----
static inline int nfkd_is_mark(uint32_t cp) {
    uint32_t lo = 0, hi = NFKD_MARKS_LEN;
    while (lo < hi) {
        uint32_t mid = (lo + hi) >> 1;
        if (cp < NFKD_MARKS[mid].lo) hi = mid;
        else if (cp > NFKD_MARKS[mid].hi) lo = mid + 1;
        else return 1;
    }
    return 0;
}

static inline const NfkdEmit *nfkd_find_emit(uint32_t cp) {
    uint32_t lo = 0, hi = NFKD_EMITS_LEN;
    while (lo < hi) {
        uint32_t mid = (lo + hi) >> 1;
        if (cp < NFKD_EMITS[mid].cp) hi = mid;
        else if (cp > NFKD_EMITS[mid].cp) lo = mid + 1;
        else return &NFKD_EMITS[mid];
    }
    return NULL;
}

static Str slugify_buf(Str text, char out64[65]) {
    uint32_t olen = 0;
    int gap = 0;
    const uint8_t *p = (const uint8_t *)text.p;
    uint32_t i = 0;
    while (i < text.len && olen < 64) {
        uint8_t b = p[i];
        if (__builtin_expect(b < 0x80, 1)) {
            i++;
            char k = 0;
            if (b >= 'A' && b <= 'Z') k = (char)(b + 32);
            else if ((b >= 'a' && b <= 'z') || (b >= '0' && b <= '9')) k = (char)b;
            if (!k) {
                gap = (olen > 0);
            } else {
                if (gap) {
                    out64[olen++] = '-';
                    gap = 0;
                    if (olen >= 64) break;
                }
                out64[olen++] = k;
            }
        } else {
            uint32_t adv;
            uint32_t cp = utf8_decode(p + i, text.len - i, &adv);
            i += adv;
            if (cp >= 0x80 && cp <= 0x24F) {
                uint8_t lv = NFKD_LATIN[cp - 0x80];
                if (lv == 0) { gap = (olen > 0); continue; }
                if (lv == 1) { continue; } // combining mark: ignored, no gap!
                if (lv > 2) {
                    if (gap) {
                        out64[olen++] = '-';
                        gap = 0;
                        if (olen >= 64) break;
                    }
                    out64[olen++] = (char)lv;
                    continue;
                }
                // lv == 2: fall through to general emit table
            } else if (nfkd_is_mark(cp)) {
                continue;
            }
            const NfkdEmit *em = nfkd_find_emit(cp);
            if (!em) {
                gap = (olen > 0);
            } else {
                for (uint32_t j = 0; j < em->len && olen < 64; j++) {
                    char ch = em->seq[j];
                    if (ch == '-') {
                        gap = (olen > 0);
                    } else {
                        if (gap) {
                            out64[olen++] = '-';
                            gap = 0;
                            if (olen >= 64) break;
                        }
                        out64[olen++] = ch;
                    }
                }
            }
        }
    }
    while (olen > 0 && out64[olen - 1] == '-') olen--;
    if (olen == 0) {
        xmemcpy(out64, "untitled", 8);
        return str_make(out64, 8);
    }
    return str_make(out64, olen);
}

// ---- longDate("YYYY-MM-DD") -> "9 November 2026" (zero relocations!) ----
static const char MONTHS[12][10] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};
static const uint8_t MONTH_LEN[12] = {7, 8, 5, 5, 3, 4, 4, 6, 9, 7, 8, 8};

static Str long_date(Arena *a, Str date) {
    if (date.len == 0) return str_make("", 0);
    // date is validated YYYY-MM-DD (10 bytes)
    int m = (date.p[5] - '0') * 10 + (date.p[6] - '0');
    int d = (date.p[8] - '0') * 10 + (date.p[9] - '0');
    char *buf = arena_alloc_bytes(a, 24);
    uint32_t k = 0;
    if (d >= 10) buf[k++] = (char)('0' + (d / 10));
    buf[k++] = (char)('0' + (d % 10));
    buf[k++] = ' ';
    uint8_t ml = MONTH_LEN[m - 1];
    xmemcpy(buf + k, MONTHS[m - 1], ml);
    k += ml;
    buf[k++] = ' ';
    xmemcpy(buf + k, date.p, 4);
    k += 4;
    return str_make(buf, k);
}

// ---- Fast FNV-1a / Wyhash-like 64-bit hash ----
static inline uint64_t hash_bytes(const char *p, uint32_t len) {
    uint64_t h = 0xcbf29ce484222325ULL ^ ((uint64_t)len * 0x9e3779b97f4a7c15ULL);
    const uint8_t *s = (const uint8_t *)p;
    while (len >= 8) {
        uint64_t v; __builtin_memcpy(&v, s, 8);
        h ^= v;
        h *= 0x100000001b3ULL;
        s += 8; len -= 8;
    }
    while (len--) {
        h ^= *s++;
        h *= 0x100000001b3ULL;
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return h ? h : 1;
}

// ---- Open-addressing Str -> int32 Map (arena-allocated) ----
typedef struct {
    Str *keys;
    uint64_t *hashes;
    int32_t *vals;
    uint32_t mask;
    uint32_t count;
    Arena *arena;
} StrMap;

static void smap_init(StrMap *m, Arena *a, uint32_t init_cap) {
    uint32_t cap = 16;
    while (cap < init_cap) cap <<= 1;
    m->arena = a;
    m->mask = cap - 1;
    m->count = 0;
    m->keys = (Str *)arena_alloc(a, cap * sizeof(Str));
    m->hashes = (uint64_t *)arena_alloc(a, cap * sizeof(uint64_t));
    m->vals = (int32_t *)arena_alloc(a, cap * sizeof(int32_t));
    xmemset(m->hashes, 0, cap * sizeof(uint64_t));
}

static void smap_grow(StrMap *m) {
    uint32_t old_cap = m->mask + 1;
    uint32_t new_cap = old_cap << 1;
    Str *ok = m->keys; uint64_t *oh = m->hashes; int32_t *ov = m->vals;
    m->keys = (Str *)arena_alloc(m->arena, new_cap * sizeof(Str));
    m->hashes = (uint64_t *)arena_alloc(m->arena, new_cap * sizeof(uint64_t));
    m->vals = (int32_t *)arena_alloc(m->arena, new_cap * sizeof(int32_t));
    xmemset(m->hashes, 0, new_cap * sizeof(uint64_t));
    m->mask = new_cap - 1;
    for (uint32_t i = 0; i < old_cap; i++) {
        if (!oh[i]) continue;
        uint32_t idx = (uint32_t)oh[i] & m->mask;
        while (m->hashes[idx]) idx = (idx + 1) & m->mask;
        m->hashes[idx] = oh[i];
        m->keys[idx] = ok[i];
        m->vals[idx] = ov[i];
    }
}

static inline int32_t smap_get(const StrMap *m, Str key) {
    uint64_t h = hash_bytes(key.p, key.len);
    uint32_t idx = (uint32_t)h & m->mask;
    while (m->hashes[idx]) {
        if (m->hashes[idx] == h && str_eq(m->keys[idx], key)) return m->vals[idx];
        idx = (idx + 1) & m->mask;
    }
    return -1;
}

static inline int smap_put(StrMap *m, Str key, int32_t val) {
    if (__builtin_expect((m->count + 1) * 10 >= (m->mask + 1) * 7, 0)) smap_grow(m);
    uint64_t h = hash_bytes(key.p, key.len);
    uint32_t idx = (uint32_t)h & m->mask;
    while (m->hashes[idx]) {
        if (m->hashes[idx] == h && str_eq(m->keys[idx], key)) {
            m->vals[idx] = val;
            return 0; // existed
        }
        idx = (idx + 1) & m->mask;
    }
    m->hashes[idx] = h;
    m->keys[idx] = key;
    m->vals[idx] = val;
    m->count++;
    return 1; // inserted new
}

static inline int smap_has(const StrMap *m, Str key) {
    return smap_get(m, key) >= 0;
}

// uniqueSlug(slug, taken_set) -> returns arena-allocated unique slug and adds to set
static Str unique_slug(Arena *a, Str base_slug, StrMap *taken) {
    if (smap_put(taken, base_slug, 1)) return base_slug;
    char tmp[80];
    xmemcpy(tmp, base_slug.p, base_slug.len);
    tmp[base_slug.len] = '-';
    for (uint32_t n = 2; ; n++) {
        char num[12];
        int k = 12; uint32_t v = n;
        do { num[--k] = (char)('0' + (v % 10)); v /= 10; } while (v);
        uint32_t nl = (uint32_t)(12 - k);
        xmemcpy(tmp + base_slug.len + 1, num + k, nl);
        uint32_t tot = base_slug.len + 1 + nl;
        Str cand = str_make(tmp, tot);
        if (!smap_has(taken, cand)) {
            Str dup = arena_dup(a, tmp, tot);
            smap_put(taken, dup, 1);
            return dup;
        }
    }
}
