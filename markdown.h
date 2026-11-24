#pragma once
#include "common.h"

// 14 backslash-escapable chars: \ ` * _ [ ] ( ) # + - . ! >
static const uint8_t MD_ESCAPABLE[256] = {
    ['\\'] = 1, ['`'] = 1, ['*'] = 1, ['_'] = 1,
    ['['] = 1,  [']'] = 1, ['('] = 1, [')'] = 1,
    ['#'] = 1,  ['+'] = 1, ['-'] = 1, ['.'] = 1,
    ['!'] = 1,  ['>'] = 1,
};

// 9 inline-special bytes: \ ` * [ & < > " '
static const uint8_t MD_INLINE_SPECIAL[256] = {
    ['\\'] = 1, ['`'] = 1, ['*'] = 1, ['['] = 1,
    ['&'] = 1,  ['<'] = 1, ['>'] = 1, ['"'] = 1, ['\''] = 1,
};

typedef struct {
    int32_t level;
    Str id;
    Str html;
} TocItem;

typedef struct MdLinkEnv MdLinkEnv;
struct MdLinkEnv {
    Str (*resolve)(MdLinkEnv *env, Str target);
    void *ctx;
};

static inline int32_t md_indexof(const char *s, uint32_t len, uint32_t from, char ch) {
    uint32_t i = from;
#ifdef __AVX2__
    {
        const __m256i vc = _mm256_set1_epi8(ch);
        while (i + 32 <= len) {
            __m256i v = _mm256_loadu_si256((const __m256i *)(s + i));
            uint32_t m = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, vc));
            if (m) return (int32_t)(i + (uint32_t)__builtin_ctz(m));
            i += 32;
        }
    }
#endif
    {
        const __m128i vc = _mm_set1_epi8(ch);
        while (i + 16 <= len) {
            __m128i v = _mm_loadu_si128((const __m128i *)(s + i));
            uint32_t m = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(v, vc));
            if (m) return (int32_t)(i + (uint32_t)__builtin_ctz(m));
            i += 16;
        }
    }
    while (i < len) {
        if (s[i] == ch) return (int32_t)i;
        i++;
    }
    return -1;
}

// SIMD-accelerated find of unescaped single char (e.g. ']')
static inline int32_t md_find_unescaped_char(const char *s, uint32_t len, uint32_t from, char ch) {
    uint32_t j = from;
#ifdef __AVX2__
    const __m256i vc = _mm256_set1_epi8(ch);
    const __m256i vb = _mm256_set1_epi8('\\');
    while (j + 32 <= len) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(s + j));
        uint32_t m = (uint32_t)_mm256_movemask_epi8(_mm256_or_si256(_mm256_cmpeq_epi8(v, vc), _mm256_cmpeq_epi8(v, vb)));
        if (!m) { j += 32; continue; }
        j += (uint32_t)__builtin_ctz(m);
        if (s[j] == ch) return (int32_t)j;
        if (j + 1 < len && MD_ESCAPABLE[(uint8_t)s[j + 1]]) j += 2;
        else j++;
    }
#else
    const __m128i vc = _mm_set1_epi8(ch);
    const __m128i vb = _mm_set1_epi8('\\');
    while (j + 16 <= len) {
        __m128i v = _mm_loadu_si128((const __m128i *)(s + j));
        uint32_t m = (uint32_t)_mm_movemask_epi8(_mm_or_si128(_mm_cmpeq_epi8(v, vc), _mm_cmpeq_epi8(v, vb)));
        if (!m) { j += 16; continue; }
        j += (uint32_t)__builtin_ctz(m);
        if (s[j] == ch) return (int32_t)j;
        if (j + 1 < len && MD_ESCAPABLE[(uint8_t)s[j + 1]]) j += 2;
        else j++;
    }
#endif
    for (; j < len; j++) {
        char c = s[j];
        if (c == '\\' && j + 1 < len && MD_ESCAPABLE[(uint8_t)s[j + 1]]) { j++; continue; }
        if (c == ch) return (int32_t)j;
    }
    return -1;
}

// SIMD-accelerated find of unescaped "**"
static inline int32_t md_find_unescaped_2star(const char *s, uint32_t len, uint32_t from) {
    if (len < 2) return -1;
    uint32_t j = from;
#ifdef __AVX2__
    const __m256i vs = _mm256_set1_epi8('*');
    const __m256i vb = _mm256_set1_epi8('\\');
    while (j + 32 <= len) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(s + j));
        uint32_t m = (uint32_t)_mm256_movemask_epi8(_mm256_or_si256(_mm256_cmpeq_epi8(v, vs), _mm256_cmpeq_epi8(v, vb)));
        if (!m) { j += 32; continue; }
        j += (uint32_t)__builtin_ctz(m);
        if (s[j] == '\\') {
            if (j + 1 < len && MD_ESCAPABLE[(uint8_t)s[j + 1]]) j += 2;
            else j++;
        } else {
            if (j + 1 < len && s[j + 1] == '*') return (int32_t)j;
            j++;
        }
    }
#else
    const __m128i vs = _mm_set1_epi8('*');
    const __m128i vb = _mm_set1_epi8('\\');
    while (j + 16 <= len) {
        __m128i v = _mm_loadu_si128((const __m128i *)(s + j));
        uint32_t m = (uint32_t)_mm_movemask_epi8(_mm_or_si128(_mm_cmpeq_epi8(v, vs), _mm_cmpeq_epi8(v, vb)));
        if (!m) { j += 16; continue; }
        j += (uint32_t)__builtin_ctz(m);
        if (s[j] == '\\') {
            if (j + 1 < len && MD_ESCAPABLE[(uint8_t)s[j + 1]]) j += 2;
            else j++;
        } else {
            if (j + 1 < len && s[j + 1] == '*') return (int32_t)j;
            j++;
        }
    }
#endif
    for (; j + 1 < len; j++) {
        char c = s[j];
        if (c == '\\' && j + 1 < len && MD_ESCAPABLE[(uint8_t)s[j + 1]]) { j++; continue; }
        if (c == '*' && s[j + 1] == '*') return (int32_t)j;
    }
    return -1;
}

// SIMD-accelerated find of unescaped single "*" (not "**")
static inline int32_t md_find_single_star(const char *s, uint32_t len, uint32_t from) {
    uint32_t j = from;
#ifdef __AVX2__
    const __m256i vs = _mm256_set1_epi8('*');
    const __m256i vb = _mm256_set1_epi8('\\');
    while (j + 32 <= len) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(s + j));
        uint32_t m = (uint32_t)_mm256_movemask_epi8(_mm256_or_si256(_mm256_cmpeq_epi8(v, vs), _mm256_cmpeq_epi8(v, vb)));
        if (!m) { j += 32; continue; }
        j += (uint32_t)__builtin_ctz(m);
        if (s[j] == '\\') {
            if (j + 1 < len && MD_ESCAPABLE[(uint8_t)s[j + 1]]) j += 2;
            else j++;
        } else {
            if (j + 1 < len && s[j + 1] == '*') { j += 2; continue; }
            return (int32_t)j;
        }
    }
#else
    const __m128i vs = _mm_set1_epi8('*');
    const __m128i vb = _mm_set1_epi8('\\');
    while (j + 16 <= len) {
        __m128i v = _mm_loadu_si128((const __m128i *)(s + j));
        uint32_t m = (uint32_t)_mm_movemask_epi8(_mm_or_si128(_mm_cmpeq_epi8(v, vs), _mm_cmpeq_epi8(v, vb)));
        if (!m) { j += 16; continue; }
        j += (uint32_t)__builtin_ctz(m);
        if (s[j] == '\\') {
            if (j + 1 < len && MD_ESCAPABLE[(uint8_t)s[j + 1]]) j += 2;
            else j++;
        } else {
            if (j + 1 < len && s[j + 1] == '*') { j += 2; continue; }
            return (int32_t)j;
        }
    }
#endif
    for (; j < len; j++) {
        char c = s[j];
        if (c == '\\' && j + 1 < len && MD_ESCAPABLE[(uint8_t)s[j + 1]]) { j++; continue; }
        if (c != '*') continue;
        if (j + 1 < len && s[j + 1] == '*') { j++; continue; }
        return (int32_t)j;
    }
    return -1;
}

// Fast register-cached HTML (+ JSON when json_mode=1) escape into SB
static inline void sb_put_html_mode(SB *sb, const char *s, uint32_t len, int json_mode) {
    if (!len) return;
    if (!json_mode) {
        sb_put_html_escaped(sb, s, len);
        return;
    }
    sb_reserve(sb, len + 64);
    char *d = sb->buf + sb->len;
    char *dend = sb->buf + sb->cap - 64;
    uint32_t i = 0;
#ifdef __AVX2__
    const __m256i lo_lut = _mm256_setr_epi8(
        0x10, 0x10, 0x11, 0x10, 0x10, 0x10, 0x11, 0x11, 0x10, 0x10, 0x10, 0x10, 0x16, 0x10, 0x12, 0x10,
        0x10, 0x10, 0x11, 0x10, 0x10, 0x10, 0x11, 0x11, 0x10, 0x10, 0x10, 0x10, 0x16, 0x10, 0x12, 0x10);
    const __m256i hi_lut = _mm256_setr_epi8(
        0x10, 0x10, 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x10, 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
    const __m256i v_0f   = _mm256_set1_epi8(0x0f);
    const __m256i v_zero = _mm256_setzero_si256();
    while (i + 32 <= len) {
        if (__builtin_expect(d > dend, 0)) {
            sb->len = (uint32_t)(d - sb->buf);
            sb_grow(sb, (len - i) + 128);
            d = sb->buf + sb->len;
            dend = sb->buf + sb->cap - 64;
        }
        __m256i v = _mm256_loadu_si256((const __m256i *)(s + i));
        __m256i hi = _mm256_and_si256(_mm256_srli_epi16(v, 4), v_0f);
        __m256i hit = _mm256_and_si256(_mm256_shuffle_epi8(lo_lut, v), _mm256_shuffle_epi8(hi_lut, hi));
        uint32_t mask = ~(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(hit, v_zero));
        _mm256_storeu_si256((__m256i *)d, v);
        if (!mask) { d += 32; i += 32; continue; }
        uint32_t adv = (uint32_t)__builtin_ctz(mask);
        d += adv; i += adv;
        uint8_t c = (uint8_t)s[i++];
        if (c == '&') { __builtin_memcpy(d, "&amp;", 5); d += 5; }
        else if (c == '<') { __builtin_memcpy(d, "&lt;", 4); d += 4; }
        else if (c == '>') { __builtin_memcpy(d, "&gt;", 4); d += 4; }
        else if (c == '"') { __builtin_memcpy(d, "&quot;", 6); d += 6; }
        else if (c == '\'') { __builtin_memcpy(d, "&#39;", 5); d += 5; }
        else if (c == '\\') { d[0] = '\\'; d[1] = '\\'; d += 2; }
        else if (c == '\n') { d[0] = '\\'; d[1] = 'n'; d += 2; }
        else if (c == '\r') { d[0] = '\\'; d[1] = 'r'; d += 2; }
        else if (c == '\t') { d[0] = '\\'; d[1] = 't'; d += 2; }
        else if (c == '\b') { d[0] = '\\'; d[1] = 'b'; d += 2; }
        else if (c == '\f') { d[0] = '\\'; d[1] = 'f'; d += 2; }
        else { d[0]='\\'; d[1]='u'; d[2]='0'; d[3]='0'; d[4]=HEX_DIGITS[c>>4]; d[5]=HEX_DIGITS[c&15]; d += 6; }
    }
#endif
    while (i < len) {
        if (__builtin_expect(d > dend, 0)) {
            sb->len = (uint32_t)(d - sb->buf);
            sb_grow(sb, (len - i) + 64);
            d = sb->buf + sb->len;
            dend = sb->buf + sb->cap - 64;
        }
        uint8_t c = (uint8_t)s[i++];
        if (__builtin_expect(c >= 0x20 && c != '&' && c != '<' && c != '>' && c != '"' && c != '\'' && c != '\\', 1)) {
            *d++ = (char)c;
        } else if (c == '&') { __builtin_memcpy(d, "&amp;", 5); d += 5; }
        else if (c == '<') { __builtin_memcpy(d, "&lt;", 4); d += 4; }
        else if (c == '>') { __builtin_memcpy(d, "&gt;", 4); d += 4; }
        else if (c == '"') { __builtin_memcpy(d, "&quot;", 6); d += 6; }
        else if (c == '\'') { __builtin_memcpy(d, "&#39;", 5); d += 5; }
        else if (c == '\\') { d[0] = '\\'; d[1] = '\\'; d += 2; }
        else if (c == '\n') { d[0] = '\\'; d[1] = 'n'; d += 2; }
        else if (c == '\r') { d[0] = '\\'; d[1] = 'r'; d += 2; }
        else if (c == '\t') { d[0] = '\\'; d[1] = 't'; d += 2; }
        else if (c == '\b') { d[0] = '\\'; d[1] = 'b'; d += 2; }
        else if (c == '\f') { d[0] = '\\'; d[1] = 'f'; d += 2; }
        else { d[0]='\\'; d[1]='u'; d[2]='0'; d[3]='0'; d[4]=HEX_DIGITS[c>>4]; d[5]=HEX_DIGITS[c&15]; d += 6; }
    }
    sb->len = (uint32_t)(d - sb->buf);
}

// 9 inline-special bytes + < 0x20 flag table for dirty-tracking in json_mode!
static const uint8_t MD_JSON_INLINE_SPECIAL[256] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    ['\\'] = 1, ['`'] = 1, ['*'] = 1, ['['] = 1,
    ['&'] = 1,  ['<'] = 1, ['>'] = 1, ['"'] = 1, ['\''] = 1,
};

// Dirty-tracked fast span scanners for json_mode:
// Track whether any byte in [from..j) needs inline/HTML/JSON processing.
// When dirty == 0, caller copies s[from..j) with a single memcpy — ZERO recursion!
static inline int32_t md_find_bt_dirty(const char *s, uint32_t len, uint32_t from, int *dirty) {
    int d = 0;
    for (uint32_t j = from; j < len; j++) {
        uint8_t c = (uint8_t)s[j];
        if (c == '`') { *dirty = d; return (int32_t)j; }
        if (c < 0x20 || c == '&' || c == '<' || c == '>' || c == '"' || c == '\'' || c == '\\') d = 1;
    }
    *dirty = d;
    return -1;
}

static inline int32_t md_find_1star_dirty(const char *s, uint32_t len, uint32_t from, int *dirty) {
    int d = 0;
    for (uint32_t j = from; j < len; j++) {
        uint8_t c = (uint8_t)s[j];
        if (c == '*') {
            if (j + 1 < len && s[j + 1] == '*') { d = 1; j++; continue; }
            *dirty = d;
            return (int32_t)j;
        }
        if (MD_JSON_INLINE_SPECIAL[c]) {
            d = 1;
            if (c == '\\' && j + 1 < len && MD_ESCAPABLE[(uint8_t)s[j + 1]]) j++;
        }
    }
    *dirty = d;
    return -1;
}

static inline int32_t md_find_2star_dirty(const char *s, uint32_t len, uint32_t from, int *dirty) {
    int d = 0;
    for (uint32_t j = from; j + 1 < len; j++) {
        uint8_t c = (uint8_t)s[j];
        if (c == '*' && s[j + 1] == '*') { *dirty = d; return (int32_t)j; }
        if (MD_JSON_INLINE_SPECIAL[c]) {
            d = 1;
            if (c == '\\' && j + 1 < len && MD_ESCAPABLE[(uint8_t)s[j + 1]]) j++;
        }
    }
    *dirty = d;
    return -1;
}

static inline int32_t md_find_rbracket_dirty(const char *s, uint32_t len, uint32_t from, int *dirty) {
    int d = 0;
    for (uint32_t j = from; j < len; j++) {
        uint8_t c = (uint8_t)s[j];
        if (c == ']') { *dirty = d; return (int32_t)j; }
        if (MD_JSON_INLINE_SPECIAL[c]) {
            d = 1;
            if (c == '\\' && j + 1 < len && MD_ESCAPABLE[(uint8_t)s[j + 1]]) j++;
        }
    }
    *dirty = d;
    return -1;
}

// Fast inline copy of small clean span directly to register `d`
static inline char *copy_small(char *d, const char *s, uint32_t n) {
    if (n >= 16) {
        xmemcpy(d, s, n);
        return d + n;
    }
    if (n >= 8) { uint64_t t; __builtin_memcpy(&t, s, 8); __builtin_memcpy(d, &t, 8); s += 8; d += 8; n -= 8; }
    if (n >= 4) { uint32_t t; __builtin_memcpy(&t, s, 4); __builtin_memcpy(d, &t, 4); s += 4; d += 4; n -= 4; }
    while (n--) *d++ = *s++;
    return d;
}

static void md_inline_json(SB *out, const char *s, uint32_t len, MdLinkEnv *env);

// Handle one special byte at s[i] in json_mode with dirty-tracked zero-recursion fast paths!
#define HANDLE_JSON_SPECIAL_AT_I() do { \
    uint8_t c = (uint8_t)s[i]; \
    if (c == '\n') { d[0] = '\\'; d[1] = 'n'; d += 2; i++; } \
    else if (c == '&') { __builtin_memcpy(d, "&amp;", 5); d += 5; i++; } \
    else if (c == '<') { __builtin_memcpy(d, "&lt;", 4); d += 4; i++; } \
    else if (c == '>') { __builtin_memcpy(d, "&gt;", 4); d += 4; i++; } \
    else if (c == '"') { __builtin_memcpy(d, "&quot;", 6); d += 6; i++; } \
    else if (c == '\'') { __builtin_memcpy(d, "&#39;", 5); d += 5; i++; } \
    else if (c < 0x20) { \
        if (c == '\t') { d[0]='\\'; d[1]='t'; d += 2; } \
        else if (c == '\r') { d[0]='\\'; d[1]='r'; d += 2; } \
        else if (c == '\b') { d[0]='\\'; d[1]='b'; d += 2; } \
        else if (c == '\f') { d[0]='\\'; d[1]='f'; d += 2; } \
        else { d[0]='\\'; d[1]='u'; d[2]='0'; d[3]='0'; d[4]=HEX_DIGITS[c>>4]; d[5]=HEX_DIGITS[c&15]; d += 6; } \
        i++; \
    } else if (c == '*') { \
        if (i + 1 < len && s[i + 1] == '*') { \
            int dirty; \
            int32_t j = md_find_2star_dirty(s, len, i + 2, &dirty); \
            if (j > (int32_t)(i + 2)) { \
                uint32_t ilen = (uint32_t)j - (i + 2); \
                if (__builtin_expect(!dirty && d + ilen + 24 <= dend + 64, 1)) { \
                    __builtin_memcpy(d, "<strong>", 8); d += 8; \
                    d = copy_small(d, s + i + 2, ilen); \
                    __builtin_memcpy(d, "</strong>", 9); d += 9; \
                } else { \
                    __builtin_memcpy(d, "<strong>", 8); d += 8; \
                    out->len = (uint32_t)(d - out->buf); \
                    if (!dirty) sb_puts(out, s + i + 2, ilen); \
                    else md_inline_json(out, s + i + 2, ilen, env); \
                    sb_reserve(out, 64); \
                    d = out->buf + out->len; dend = out->buf + out->cap - 64; \
                    __builtin_memcpy(d, "</strong>", 9); d += 9; \
                } \
                i = (uint32_t)j + 2; \
            } else { d[0] = '*'; d[1] = '*'; d += 2; i += 2; } \
        } else { \
            int dirty; \
            int32_t j = md_find_1star_dirty(s, len, i + 1, &dirty); \
            if (j > (int32_t)(i + 1)) { \
                uint32_t ilen = (uint32_t)j - (i + 1); \
                if (__builtin_expect(!dirty && d + ilen + 16 <= dend + 64, 1)) { \
                    __builtin_memcpy(d, "<em>", 4); d += 4; \
                    d = copy_small(d, s + i + 1, ilen); \
                    __builtin_memcpy(d, "</em>", 5); d += 5; \
                } else { \
                    __builtin_memcpy(d, "<em>", 4); d += 4; \
                    out->len = (uint32_t)(d - out->buf); \
                    if (!dirty) sb_puts(out, s + i + 1, ilen); \
                    else md_inline_json(out, s + i + 1, ilen, env); \
                    sb_reserve(out, 64); \
                    d = out->buf + out->len; dend = out->buf + out->cap - 64; \
                    __builtin_memcpy(d, "</em>", 5); d += 5; \
                } \
                i = (uint32_t)j + 1; \
            } else { *d++ = '*'; i++; } \
        } \
    } else if (c == '[') { \
        int dirty; \
        int32_t k = md_find_rbracket_dirty(s, len, i + 1, &dirty); \
        int32_t m = (k != -1 && (uint32_t)k + 1 < len && s[k + 1] == '(') \
                    ? md_indexof(s, len, (uint32_t)k + 2, ')') : -1; \
        if (m != -1) { \
            Str target = str_make(s + k + 2, (uint32_t)m - ((uint32_t)k + 2)); \
            if (target.len > 0 && !has_js_ws(target)) { \
                Str href = env->resolve(env, target); \
                __builtin_memcpy(d, "<a href=\\\"", 10); d += 10; \
                out->len = (uint32_t)(d - out->buf); \
                sb_put_html_mode(out, href.p, href.len, 1); \
                uint32_t ilen = (uint32_t)k - (i + 1); \
                sb_reserve(out, ilen + 80); \
                d = out->buf + out->len; dend = out->buf + out->cap - 64; \
                __builtin_memcpy(d, "\\\">", 3); d += 3; \
                if (__builtin_expect(!dirty, 1)) { \
                    d = copy_small(d, s + i + 1, ilen); \
                } else { \
                    out->len = (uint32_t)(d - out->buf); \
                    md_inline_json(out, s + i + 1, ilen, env); \
                    sb_reserve(out, 64); \
                    d = out->buf + out->len; dend = out->buf + out->cap - 64; \
                } \
                __builtin_memcpy(d, "</a>", 4); d += 4; \
                i = (uint32_t)m + 1; \
            } else { *d++ = '['; i++; } \
        } else { *d++ = '['; i++; } \
    } else if (c == '`') { \
        int dirty; \
        int32_t j = md_find_bt_dirty(s, len, i + 1, &dirty); \
        if (j > (int32_t)(i + 1)) { \
            uint32_t ilen = (uint32_t)j - (i + 1); \
            if (__builtin_expect(!dirty && d + ilen + 20 <= dend + 64, 1)) { \
                __builtin_memcpy(d, "<code>", 6); d += 6; \
                d = copy_small(d, s + i + 1, ilen); \
                __builtin_memcpy(d, "</code>", 7); d += 7; \
            } else { \
                __builtin_memcpy(d, "<code>", 6); d += 6; \
                out->len = (uint32_t)(d - out->buf); \
                if (!dirty) sb_puts(out, s + i + 1, ilen); \
                else sb_put_html_mode(out, s + i + 1, ilen, 1); \
                sb_reserve(out, 64); \
                d = out->buf + out->len; dend = out->buf + out->cap - 64; \
                __builtin_memcpy(d, "</code>", 7); d += 7; \
            } \
            i = (uint32_t)j + 1; \
        } else { *d++ = '`'; i++; } \
    } else { \
        if (i + 1 < len && MD_ESCAPABLE[(uint8_t)s[i + 1]]) { \
            char n = s[i + 1]; \
            if (n == '>') { __builtin_memcpy(d, "&gt;", 4); d += 4; } \
            else if (n == '\\') { d[0] = '\\'; d[1] = '\\'; d += 2; } \
            else *d++ = n; \
            i += 2; \
        } else { \
            d[0] = '\\'; d[1] = '\\'; d += 2; \
            i++; \
        } \
    } \
} while (0)

static void md_inline_json(SB *out, const char *s, uint32_t len, MdLinkEnv *env) {
    sb_reserve(out, len + 128);
    char *d = out->buf + out->len;
    char *dend = out->buf + out->cap - 64;
    uint32_t i = 0;
#ifdef __AVX2__
    const __m256i lo_lut = _mm256_setr_epi8(
        0x18,0x10,0x11,0x10,0x10,0x10,0x11,0x11,0x10,0x10,0x11,0x14,0x16,0x10,0x12,0x10,
        0x18,0x10,0x11,0x10,0x10,0x10,0x11,0x11,0x10,0x10,0x11,0x14,0x16,0x10,0x12,0x10);
    const __m256i hi_lut = _mm256_setr_epi8(
        0x10,0x10,0x01,0x02,0x00,0x04,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x10,0x10,0x01,0x02,0x00,0x04,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
    const __m256i v_0f = _mm256_set1_epi8(0x0f);
    const __m256i v_zero = _mm256_setzero_si256();
    while (i + 32 <= len) {
        if (__builtin_expect(d > dend, 0)) {
            out->len = (uint32_t)(d - out->buf);
            sb_grow(out, (len - i) + 256);
            d = out->buf + out->len;
            dend = out->buf + out->cap - 64;
        }
        __m256i v = _mm256_loadu_si256((const __m256i *)(s + i));
        __m256i hi = _mm256_and_si256(_mm256_srli_epi16(v, 4), v_0f);
        __m256i hit = _mm256_and_si256(_mm256_shuffle_epi8(lo_lut, v), _mm256_shuffle_epi8(hi_lut, hi));
        uint32_t mask = ~(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(hit, v_zero));
        if (!mask) {
            _mm256_storeu_si256((__m256i *)d, v);
            d += 32; i += 32;
            continue;
        }
        uint32_t cbase = i;
        uint32_t cend = i + 32;
        while (mask) {
            uint32_t pos = cbase + (uint32_t)__builtin_ctz(mask);
            mask &= mask - 1;
            if (pos < i) continue;
            if (__builtin_expect(d > dend, 0)) {
                out->len = (uint32_t)(d - out->buf);
                sb_grow(out, (len - i) + 256);
                d = out->buf + out->len;
                dend = out->buf + out->cap - 64;
            }
            if (pos > i) { d = copy_small(d, s + i, pos - i); i = pos; }
            HANDLE_JSON_SPECIAL_AT_I();
            if (i >= cend) goto next_avx_chunk;
        }
        if (i < cend) { d = copy_small(d, s + i, cend - i); i = cend; }
    next_avx_chunk:;
    }
#else
    const __m128i v_bs  = _mm_set1_epi8('\\');
    const __m128i v_bt  = _mm_set1_epi8('`');
    const __m128i v_st  = _mm_set1_epi8('*');
    const __m128i v_lb  = _mm_set1_epi8('[');
    const __m128i v_amp = _mm_set1_epi8('&');
    const __m128i v_lt  = _mm_set1_epi8('<');
    const __m128i v_gt  = _mm_set1_epi8('>');
    const __m128i v_dq  = _mm_set1_epi8('"');
    const __m128i v_sq  = _mm_set1_epi8('\'');
    const __m128i v_80  = _mm_set1_epi8((char)0x80);
    const __m128i v_spu = _mm_set1_epi8((char)(0x20 ^ 0x80));
    while (i + 16 <= len) {
        if (__builtin_expect(d > dend, 0)) {
            out->len = (uint32_t)(d - out->buf);
            sb_grow(out, (len - i) + 256);
            d = out->buf + out->len;
            dend = out->buf + out->cap - 64;
        }
        __m128i v = _mm_loadu_si128((const __m128i *)(s + i));
        __m128i m1 = _mm_or_si128(_mm_cmpeq_epi8(v, v_bs), _mm_cmpeq_epi8(v, v_bt));
        __m128i m2 = _mm_or_si128(_mm_cmpeq_epi8(v, v_st), _mm_cmpeq_epi8(v, v_lb));
        __m128i m3 = _mm_or_si128(_mm_cmpeq_epi8(v, v_amp), _mm_cmpeq_epi8(v, v_lt));
        __m128i m4 = _mm_or_si128(_mm_cmpeq_epi8(v, v_gt), _mm_or_si128(_mm_cmpeq_epi8(v, v_dq), _mm_cmpeq_epi8(v, v_sq)));
        __m128i m = _mm_or_si128(_mm_or_si128(_mm_or_si128(m1, m2), _mm_or_si128(m3, m4)),
                                 _mm_cmpgt_epi8(v_spu, _mm_xor_si128(v, v_80)));
        uint32_t mask = (uint32_t)_mm_movemask_epi8(m);
        if (!mask) {
            _mm_storeu_si128((__m128i *)d, v);
            d += 16; i += 16;
            continue;
        }
        uint32_t cbase = i;
        uint32_t cend = i + 16;
        while (mask) {
            uint32_t pos = cbase + (uint32_t)__builtin_ctz(mask);
            mask &= mask - 1;
            if (pos < i) continue;
            if (__builtin_expect(d > dend, 0)) {
                out->len = (uint32_t)(d - out->buf);
                sb_grow(out, (len - i) + 256);
                d = out->buf + out->len;
                dend = out->buf + out->cap - 64;
            }
            if (pos > i) { d = copy_small(d, s + i, pos - i); i = pos; }
            HANDLE_JSON_SPECIAL_AT_I();
            if (i >= cend) goto next_sse_chunk;
        }
        if (i < cend) { d = copy_small(d, s + i, cend - i); i = cend; }
    next_sse_chunk:;
    }
#endif
    while (i < len) {
        if (__builtin_expect(d > dend, 0)) {
            out->len = (uint32_t)(d - out->buf);
            sb_grow(out, (len - i) + 128);
            d = out->buf + out->len;
            dend = out->buf + out->cap - 64;
        }
        uint8_t c = (uint8_t)s[i];
        if (__builtin_expect(!MD_JSON_INLINE_SPECIAL[c], 1)) {
            *d++ = (char)c;
            i++;
        } else {
            HANDLE_JSON_SPECIAL_AT_I();
        }
    }
    out->len = (uint32_t)(d - out->buf);
}

// Cold compact fallback for rare templates that use {{ page.content }} (HTML-escaped markdown)
__attribute__((cold, noinline))
static void md_inline_raw(SB *out, const char *s, uint32_t len, MdLinkEnv *env) {
    uint32_t i = 0;
    while (i < len) {
        uint8_t c = (uint8_t)s[i];
        if (!MD_INLINE_SPECIAL[c]) { sb_putc(out, (char)c); i++; continue; }
        if (c == '&') { sb_puts(out, "&amp;", 5); i++; }
        else if (c == '<') { sb_puts(out, "&lt;", 4); i++; }
        else if (c == '>') { sb_puts(out, "&gt;", 4); i++; }
        else if (c == '"') { sb_puts(out, "&quot;", 6); i++; }
        else if (c == '\'') { sb_puts(out, "&#39;", 5); i++; }
        else if (c == '\\') {
            if (i + 1 < len && MD_ESCAPABLE[(uint8_t)s[i + 1]]) {
                if (s[i + 1] == '>') sb_puts(out, "&gt;", 4);
                else sb_putc(out, s[i + 1]);
                i += 2;
            } else { sb_putc(out, '\\'); i++; }
        } else if (c == '`') {
            int32_t j = md_indexof(s, len, i + 1, '`');
            if (j > (int32_t)(i + 1)) {
                sb_puts(out, "<code>", 6);
                sb_put_html_escaped(out, s + i + 1, (uint32_t)j - (i + 1));
                sb_puts(out, "</code>", 7);
                i = (uint32_t)j + 1;
            } else { sb_putc(out, '`'); i++; }
        } else if (c == '*') {
            if (i + 1 < len && s[i + 1] == '*') {
                int32_t j = md_find_unescaped_2star(s, len, i + 2);
                if (j > (int32_t)(i + 2)) {
                    sb_puts(out, "<strong>", 8);
                    md_inline_raw(out, s + i + 2, (uint32_t)j - (i + 2), env);
                    sb_puts(out, "</strong>", 9);
                    i = (uint32_t)j + 2;
                } else { sb_puts(out, "**", 2); i += 2; }
            } else {
                int32_t j = md_find_single_star(s, len, i + 1);
                if (j > (int32_t)(i + 1)) {
                    sb_puts(out, "<em>", 4);
                    md_inline_raw(out, s + i + 1, (uint32_t)j - (i + 1), env);
                    sb_puts(out, "</em>", 5);
                    i = (uint32_t)j + 1;
                } else { sb_putc(out, '*'); i++; }
            }
        } else {
            int32_t k = md_find_unescaped_char(s, len, i + 1, ']');
            int32_t m = (k != -1 && (uint32_t)k + 1 < len && s[k + 1] == '(')
                        ? md_indexof(s, len, (uint32_t)k + 2, ')') : -1;
            if (m != -1) {
                Str target = str_make(s + k + 2, (uint32_t)m - ((uint32_t)k + 2));
                if (target.len > 0 && !has_js_ws(target)) {
                    Str href = env->resolve(env, target);
                    sb_puts(out, "<a href=\"", 9);
                    sb_put_html_escaped(out, href.p, href.len);
                    sb_puts(out, "\">", 2);
                    md_inline_raw(out, s + i + 1, (uint32_t)k - (i + 1), env);
                    sb_puts(out, "</a>", 4);
                    i = (uint32_t)m + 1;
                    continue;
                }
            }
            sb_putc(out, '['); i++;
        }
    }
}

static inline void md_inline_core(SB *out, const char *s, uint32_t len, MdLinkEnv *env, int json_mode) {
    if (__builtin_expect(json_mode, 1)) md_inline_json(out, s, len, env);
    else md_inline_raw(out, s, len, env);
}

// ---- Line classification helpers matching markdown.js regexes ----
static inline int md_is_blank(const char *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (p[i] != ' ' && p[i] != '\t') return 0;
    return 1;
}

static inline int md_is_fence_open(const char *p, uint32_t n, Str *lang) {
    if (n < 3 || p[0] != '`' || p[1] != '`' || p[2] != '`') return 0;
    uint32_t i = 3;
    while (i < n) {
        char c = p[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '+' || c == '-') i++;
        else break;
    }
    uint32_t le = i;
    while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
    if (i != n) return 0;
    if (lang) *lang = str_make(p + 3, le - 3);
    return 1;
}

static inline int md_is_fence_close(const char *p, uint32_t n) {
    if (n < 3 || p[0] != '`' || p[1] != '`' || p[2] != '`') return 0;
    for (uint32_t i = 3; i < n; i++) if (p[i] != ' ' && p[i] != '\t') return 0;
    return 1;
}

static inline int md_heading_of(const char *p, uint32_t n, int *depth, Str *text) {
    if (n == 0 || p[0] != '#') return 0;
    uint32_t d = 0;
    while (d < n && d < 7 && p[d] == '#') d++;
    if (d == 0 || d > 6 || d >= n || (p[d] != ' ' && p[d] != '\t')) return 0;
    uint32_t a = d;
    while (a < n && (p[a] == ' ' || p[a] == '\t')) a++;
    uint32_t b = n;
    while (b > a && (p[b - 1] == ' ' || p[b - 1] == '\t')) b--;
    if (a == b) return 0;
    if (depth) *depth = (int)d;
    if (text) *text = str_make(p + a, b - a);
    return 1;
}

static inline int md_is_hr(const char *p, uint32_t n) {
    if (n < 3) return 0;
    if (!((p[0] == '-' && p[1] == '-' && p[2] == '-') ||
          (p[0] == '*' && p[1] == '*' && p[2] == '*'))) return 0;
    for (uint32_t i = 3; i < n; i++) if (p[i] != ' ' && p[i] != '\t') return 0;
    return 1;
}

static inline int md_ends_paragraph(const char *p, uint32_t n) {
    if (n == 0) return 1;
    char c0 = p[0];
    if (c0 == ' ' || c0 == '\t') return md_is_blank(p, n);
    if (c0 == '`') return md_is_fence_open(p, n, NULL);
    if (c0 == '#') return md_heading_of(p, n, NULL, NULL);
    if (c0 == '-' || c0 == '*') return md_is_hr(p, n);
    return 0;
}

static inline int md_is_bullet(const char *p, uint32_t n, Str *rest) {
    if (n >= 2 && (p[0] == '-' || p[0] == '*') && p[1] == ' ') {
        if (rest) *rest = str_make(p + 2, n - 2);
        return 1;
    }
    return 0;
}

static inline int md_is_ordered(const char *p, uint32_t n, uint32_t *num, Str *rest) {
    uint32_t d = 0;
    uint32_t v = 0;
    while (d < n && d < 10 && p[d] >= '0' && p[d] <= '9') {
        v = v * 10 + (uint32_t)(p[d] - '0');
        d++;
    }
    if (d == 0 || d > 9 || d + 1 >= n || p[d] != '.' || p[d + 1] != ' ') return 0;
    if (num) *num = v;
    if (rest) *rest = str_make(p + d + 2, n - (d + 2));
    return 1;
}

static inline int md_is_continued(const char *p, uint32_t n) {
    if (n < 3 || p[0] != ' ' || p[1] != ' ') return 0;
    uint32_t i = 2;
    while (i < n && p[i] == ' ') i++;
    if (i >= n) return 0;
    const uint8_t *u = (const uint8_t *)p;
    if (u[i] < 0x80) return !is_ascii_ws(u[i]);
    uint32_t adv;
    return !is_js_ws_cp(utf8_decode(u + i, n - i, &adv));
}

static inline uint32_t swar_popcnt16(uint32_t v) {
    v = v - ((v >> 1) & 0x5555u);
    v = (v & 0x3333u) + ((v >> 2) & 0x3333u);
    v = (v + (v >> 4)) & 0x0F0Fu;
    return (v + (v >> 8)) & 0x1Fu;
}

// ULTRA-FAST branchless AVX2/SSE2 SIMD wordsOf(text) matching /\S+/g!
static uint32_t count_words_js(Str s) {
    const uint8_t *p = (const uint8_t *)s.p;
    uint32_t len = s.len;
    uint32_t count = 0;
    uint32_t prev_ws = 1;
    uint32_t i = 0;

#ifdef __AVX2__
    {
        const __m256i v_sp  = _mm256_set1_epi8(' ');
        const __m256i v_off = _mm256_set1_epi8((char)(9 + 128));
        const __m256i v_lim = _mm256_set1_epi8((char)(-123));
        while (i + 32 <= len) {
            __m256i v = _mm256_loadu_si256((const __m256i *)(p + i));
            uint32_t hi = (uint32_t)_mm256_movemask_epi8(v);
            if (__builtin_expect(hi == 0, 1)) {
                __m256i is_sp = _mm256_cmpeq_epi8(v, v_sp);
                __m256i is_ctl = _mm256_cmpgt_epi8(v_lim, _mm256_sub_epi8(v, v_off));
                uint32_t ws = (uint32_t)_mm256_movemask_epi8(_mm256_or_si256(is_sp, is_ctl));
                uint32_t starts = (~ws) & ((ws << 1) | prev_ws);
                count += (uint32_t)__builtin_popcount(starts);
                prev_ws = ws >> 31;
                i += 32;
                continue;
            }
            uint32_t end_chunk = i + 32;
            while (i < end_chunk) {
                uint8_t c = p[i];
                if (c < 0x80) {
                    uint32_t ws = (c == ' ' || (c >= 9 && c <= 13)) ? 1u : 0u;
                    count += (!ws) & prev_ws;
                    prev_ws = ws;
                    i++;
                } else {
                    uint32_t adv;
                    uint32_t cp = utf8_decode(p + i, len - i, &adv);
                    uint32_t ws = (uint32_t)is_js_ws_cp(cp);
                    count += (!ws) & prev_ws;
                    prev_ws = ws;
                    i += adv;
                }
            }
        }
    }
#else
    {
        const __m128i v_sp  = _mm_set1_epi8(' ');
        const __m128i v_off = _mm_set1_epi8((char)(9 + 128));
        const __m128i v_lim = _mm_set1_epi8((char)(-123));
        while (i + 16 <= len) {
            __m128i v = _mm_loadu_si128((const __m128i *)(p + i));
            uint32_t hi = (uint32_t)_mm_movemask_epi8(v);
            if (__builtin_expect(hi == 0, 1)) {
                __m128i is_sp = _mm_cmpeq_epi8(v, v_sp);
                __m128i is_ctl = _mm_cmpgt_epi8(v_lim, _mm_sub_epi8(v, v_off));
                uint32_t ws = (uint32_t)_mm_movemask_epi8(_mm_or_si128(is_sp, is_ctl));
                uint32_t starts = ((~ws) & 0xFFFFu) & ((ws << 1) | prev_ws);
                count += swar_popcnt16(starts);
                prev_ws = (ws >> 15) & 1u;
                i += 16;
                continue;
            }
            uint32_t end_chunk = i + 16;
            while (i < end_chunk) {
                uint8_t c = p[i];
                if (c < 0x80) {
                    uint32_t ws = (c == ' ' || (c >= 9 && c <= 13)) ? 1u : 0u;
                    count += (!ws) & prev_ws;
                    prev_ws = ws;
                    i++;
                } else {
                    uint32_t adv;
                    uint32_t cp = utf8_decode(p + i, len - i, &adv);
                    uint32_t ws = (uint32_t)is_js_ws_cp(cp);
                    count += (!ws) & prev_ws;
                    prev_ws = ws;
                    i += adv;
                }
            }
        }
    }
#endif
    while (i < len) {
        uint8_t c = p[i];
        if (__builtin_expect(c < 0x80, 1)) {
            uint32_t ws = (c == ' ' || (c >= 9 && c <= 13)) ? 1u : 0u;
            count += (!ws) & prev_ws;
            prev_ws = ws;
            i++;
        } else {
            uint32_t adv;
            uint32_t cp = utf8_decode(p + i, len - i, &adv);
            uint32_t ws = (uint32_t)is_js_ws_cp(cp);
            count += (!ws) & prev_ws;
            prev_ws = ws;
            i += adv;
        }
    }
    return count;
}

typedef struct {
    Str html;
    Str excerpt;
    TocItem *toc;
    uint32_t n_toc;
} MdResult;

// Zero-redundancy cached LineIter: peek NEVER rescans for '\n' twice!
typedef struct {
    const char *src;
    uint32_t len;
    uint32_t pos;
    int done;
    int has_peek;
    const char *peek_p;
    uint32_t peek_n;
    uint32_t peek_next_pos;
    int peek_next_done;
} LineIter;

static inline void li_init(LineIter *it, Str s) {
    it->src = s.p; it->len = s.len; it->pos = 0; it->done = 0; it->has_peek = 0;
}

static inline int li_peek(LineIter *it, const char **lp, uint32_t *ln) {
    if (it->has_peek) { *lp = it->peek_p; *ln = it->peek_n; return 1; }
    if (it->done) return 0;
    uint32_t start = it->pos;
    int32_t nl = md_indexof(it->src, it->len, start, '\n');
    it->peek_p = it->src + start;
    if (nl < 0) {
        it->peek_n = it->len - start;
        it->peek_next_pos = it->len;
        it->peek_next_done = 1;
    } else {
        it->peek_n = (uint32_t)nl - start;
        it->peek_next_pos = (uint32_t)nl + 1;
        it->peek_next_done = 0;
    }
    it->has_peek = 1;
    *lp = it->peek_p;
    *ln = it->peek_n;
    return 1;
}

static inline int li_next(LineIter *it, const char **lp, uint32_t *ln) {
    if (it->has_peek) {
        *lp = it->peek_p;
        *ln = it->peek_n;
        it->pos = it->peek_next_pos;
        it->done = it->peek_next_done;
        it->has_peek = 0;
        return 1;
    }
    if (it->done) return 0;
    uint32_t start = it->pos;
    int32_t nl = md_indexof(it->src, it->len, start, '\n');
    *lp = it->src + start;
    if (nl < 0) {
        *ln = it->len - start;
        it->pos = it->len;
        it->done = 1;
    } else {
        *ln = (uint32_t)nl - start;
        it->pos = (uint32_t)nl + 1;
    }
    return 1;
}

typedef struct {
    SB out;
    SB scratch;
    SB toc_str;
    TocItem *toc;
    uint32_t cap_toc;
    StrMap ids;
    Arena *arena;
} MdWork;

static void mdwork_init(MdWork *w, Arena *a) {
    w->arena = a;
    sb_init(&w->out, a, 65536);
    sb_init(&w->scratch, a, 8192);
    sb_init(&w->toc_str, a, 4096);
    w->cap_toc = 64;
    w->toc = (TocItem *)arena_alloc(a, w->cap_toc * sizeof(TocItem));
    smap_init(&w->ids, a, 64);
}

static void render_markdown_work(MdWork *w, Str body, MdLinkEnv *env, MdResult *res, int json_mode, int persist_excerpt) {
    Arena *a = w->arena;
    w->out.len = 0;
    w->scratch.len = 0;
    w->toc_str.len = 0;
    // Reset ids map in-place (zero allocations!)
    w->ids.count = 0;
    xmemset(w->ids.hashes, 0, (w->ids.mask + 1) * sizeof(uint64_t));

    uint32_t n_toc = 0;
    int has_excerpt = 0;
    uint32_t ex_off = 0, ex_len = 0;

    LineIter it; li_init(&it, body);
    const char *lp; uint32_t ln;

    while (li_next(&it, &lp, &ln)) {
        if (ln == 0 || ((lp[0] == ' ' || lp[0] == '\t') && md_is_blank(lp, ln))) continue;

        // 1. Fenced code
        Str lang;
        if (lp[0] == '`' && md_is_fence_open(lp, ln, &lang)) {
            if (lang.len == 0) {
                sb_puts(&w->out, "<pre><code>", 11);
            } else {
                if (json_mode) sb_puts(&w->out, "<pre><code class=\\\"language-", 28);
                else sb_puts(&w->out, "<pre><code class=\"language-", 27);
                sb_put_html_mode(&w->out, lang.p, lang.len, json_mode);
                if (json_mode) sb_puts(&w->out, "\\\">", 3);
                else sb_puts(&w->out, "\">", 2);
            }
            while (li_next(&it, &lp, &ln)) {
                if (ln >= 3 && lp[0] == '`' && md_is_fence_close(lp, ln)) break;
                sb_put_html_mode(&w->out, lp, ln, json_mode);
                if (json_mode) sb_puts(&w->out, "\\n", 2);
                else sb_putc(&w->out, '\n');
            }
            if (json_mode) sb_puts(&w->out, "</code></pre>\\n", 15);
            else sb_puts(&w->out, "</code></pre>\n", 14);
            continue;
        }

        // 2. ATX Heading
        int depth; Str htext;
        if (lp[0] == '#' && md_heading_of(lp, ln, &depth, &htext)) {
            char slug_tmp[65];
            Str base_slug = slugify_buf(htext, slug_tmp);
            // Put slug into reusable toc_str? Only unique_slug needs stable key in w->ids during this doc!
            uint32_t id_off = w->toc_str.len;
            sb_puts(&w->toc_str, base_slug.p, base_slug.len);
            // Wait: w->toc_str can realloc, so use arena_dup for heading ids (tiny, ~100 bytes/doc!)
            Str dup_slug = arena_dup(a, base_slug.p, base_slug.len);
            Str id = unique_slug(a, dup_slug, &w->ids);
            w->toc_str.len = id_off;

            sb_puts(&w->out, "<h", 2);
            sb_putc(&w->out, (char)('0' + depth));
            if (json_mode) sb_puts(&w->out, " id=\\\"", 6);
            else sb_puts(&w->out, " id=\"", 5);
            sb_puts(&w->out, id.p, id.len);
            if (json_mode) sb_puts(&w->out, "\\\">", 3);
            else sb_puts(&w->out, "\">", 2);
            uint32_t inner_start = w->out.len;
            md_inline_core(&w->out, htext.p, htext.len, env, json_mode);
            uint32_t inner_len = w->out.len - inner_start;
            sb_puts(&w->out, "</h", 3);
            sb_putc(&w->out, (char)('0' + depth));
            if (json_mode) sb_puts(&w->out, ">\\n", 3);
            else sb_puts(&w->out, ">\n", 2);

            if (depth == 2 || depth == 3) {
                if (n_toc >= w->cap_toc) {
                    uint32_t ncap = w->cap_toc * 2;
                    TocItem *nt = (TocItem *)arena_alloc(a, ncap * sizeof(TocItem));
                    xmemcpy(nt, w->toc, n_toc * sizeof(TocItem));
                    w->toc = nt; w->cap_toc = ncap;
                }
                // Store offset into w->out temporarily (resolved after loop once w->out stops growing!)
                w->toc[n_toc].level = depth;
                w->toc[n_toc].id = id;
                w->toc[n_toc].html = str_make((const char *)(uintptr_t)inner_start, inner_len);
                n_toc++;
            }
            continue;
        }

        // 3. HR
        if ((lp[0] == '-' || lp[0] == '*') && md_is_hr(lp, ln)) {
            if (json_mode) sb_puts(&w->out, "<hr>\\n", 6);
            else sb_puts(&w->out, "<hr>\n", 5);
            continue;
        }

        // 4. Blockquote
        if (lp[0] == '>') {
            w->scratch.len = 0;
            int any = 0;
            for (;;) {
                const char *qp = lp + 1;
                uint32_t qn = ln - 1;
                if (qn > 0 && qp[0] == ' ') { qp++; qn--; }
                Str t = str_trim_hws(str_make(qp, qn));
                if (t.len > 0) {
                    if (any) sb_putc(&w->scratch, '\n');
                    sb_puts(&w->scratch, t.p, t.len);
                    any = 1;
                }
                const char *np; uint32_t nn;
                if (!li_peek(&it, &np, &nn) || nn == 0 || np[0] != '>') break;
                li_next(&it, &lp, &ln);
            }
            if (!any) {
                if (json_mode) sb_puts(&w->out, "<blockquote>\\n</blockquote>\\n", 29);
                else sb_puts(&w->out, "<blockquote>\n</blockquote>\n", 27);
            } else {
                if (json_mode) sb_puts(&w->out, "<blockquote>\\n<p>", 17);
                else sb_puts(&w->out, "<blockquote>\n<p>", 16);
                md_inline_core(&w->out, w->scratch.buf, w->scratch.len, env, json_mode);
                if (json_mode) sb_puts(&w->out, "</p>\\n</blockquote>\\n", 21);
                else sb_puts(&w->out, "</p>\n</blockquote>\n", 19);
            }
            continue;
        }

        // 5. Bullet or Ordered List
        Str rest;
        uint32_t start_num = 1;
        int is_bullet = md_is_bullet(lp, ln, &rest);
        int is_ordered = !is_bullet && (lp[0] >= '0' && lp[0] <= '9') && md_is_ordered(lp, ln, &start_num, &rest);
        if (is_bullet || is_ordered) {
            if (is_ordered) {
                if (start_num != 1) {
                    if (json_mode) sb_puts(&w->out, "<ol start=\\\"", 12);
                    else sb_puts(&w->out, "<ol start=\"", 11);
                    sb_putu32(&w->out, start_num);
                    if (json_mode) sb_puts(&w->out, "\\\">\\n", 5);
                    else sb_puts(&w->out, "\">\n", 3);
                } else {
                    if (json_mode) sb_puts(&w->out, "<ol>\\n", 6);
                    else sb_puts(&w->out, "<ol>\n", 5);
                }
            } else {
                if (json_mode) sb_puts(&w->out, "<ul>\\n", 6);
                else sb_puts(&w->out, "<ul>\n", 5);
            }
            for (;;) {
                Str first = str_trim_hws(rest);
                const char *np; uint32_t nn;
                if (!li_peek(&it, &np, &nn) || !md_is_continued(np, nn)) {
                    sb_puts(&w->out, "<li>", 4);
                    md_inline_core(&w->out, first.p, first.len, env, json_mode);
                    if (json_mode) sb_puts(&w->out, "</li>\\n", 7);
                    else sb_puts(&w->out, "</li>\n", 6);
                } else {
                    w->scratch.len = 0;
                    sb_puts(&w->scratch, first.p, first.len);
                    for (;;) {
                        if (!li_peek(&it, &np, &nn) || !md_is_continued(np, nn)) break;
                        li_next(&it, &lp, &ln);
                        Str cont = str_trim_hws(str_make(lp, ln));
                        sb_putc(&w->scratch, '\n');
                        sb_puts(&w->scratch, cont.p, cont.len);
                    }
                    sb_puts(&w->out, "<li>", 4);
                    md_inline_core(&w->out, w->scratch.buf, w->scratch.len, env, json_mode);
                    if (json_mode) sb_puts(&w->out, "</li>\\n", 7);
                    else sb_puts(&w->out, "</li>\n", 6);
                }
                if (!li_peek(&it, &np, &nn)) break;
                if (is_ordered) {
                    if (!(nn > 0 && np[0] >= '0' && np[0] <= '9' && md_is_ordered(np, nn, NULL, &rest))) break;
                } else {
                    if (!md_is_bullet(np, nn, &rest)) break;
                }
                li_next(&it, &lp, &ln);
            }
            if (json_mode) sb_puts(&w->out, is_ordered ? "</ol>\\n" : "</ul>\\n", 7);
            else sb_puts(&w->out, is_ordered ? "</ol>\n" : "</ul>\n", 6);
            continue;
        }

        // 6. Paragraph (ZERO-COPY fast path!)
        const char *p_start = lp;
        const char *p_end = lp + ln;
        int dirty = (lp[0] == ' ' || lp[0] == '\t' || lp[ln - 1] == ' ' || lp[ln - 1] == '\t');
        if (!dirty) {
            for (;;) {
                const char *np; uint32_t nn;
                if (!li_peek(&it, &np, &nn) || md_ends_paragraph(np, nn)) break;
                li_next(&it, &lp, &ln);
                if (lp[0] == ' ' || lp[0] == '\t' || lp[ln - 1] == ' ' || lp[ln - 1] == '\t') {
                    w->scratch.len = 0;
                    sb_puts(&w->scratch, p_start, (uint32_t)(p_end - p_start));
                    Str cont = str_trim_hws(str_make(lp, ln));
                    sb_putc(&w->scratch, '\n');
                    sb_puts(&w->scratch, cont.p, cont.len);
                    dirty = 1;
                    break;
                }
                p_end = lp + ln;
            }
        } else {
            w->scratch.len = 0;
            Str first = str_trim_hws(str_make(lp, ln));
            sb_puts(&w->scratch, first.p, first.len);
        }
        if (dirty) {
            for (;;) {
                const char *np; uint32_t nn;
                if (!li_peek(&it, &np, &nn) || md_ends_paragraph(np, nn)) break;
                li_next(&it, &lp, &ln);
                Str cont = str_trim_hws(str_make(lp, ln));
                sb_putc(&w->scratch, '\n');
                sb_puts(&w->scratch, cont.p, cont.len);
            }
        }
        sb_puts(&w->out, "<p>", 3);
        uint32_t pstart = w->out.len;
        if (!dirty) md_inline_core(&w->out, p_start, (uint32_t)(p_end - p_start), env, json_mode);
        else md_inline_core(&w->out, w->scratch.buf, w->scratch.len, env, json_mode);
        uint32_t plen = w->out.len - pstart;
        if (!has_excerpt) {
            ex_off = pstart;
            ex_len = plen;
            has_excerpt = 1;
        }
        if (json_mode) sb_puts(&w->out, "</p>\\n", 6);
        else sb_puts(&w->out, "</p>\n", 5);
    }

    // Resolve toc html pointers directly into w->out.buf (ZERO copy!)
    for (uint32_t k = 0; k < n_toc; k++) {
        uintptr_t off = (uintptr_t)w->toc[k].html.p;
        w->toc[k].html.p = w->out.buf + off;
    }
    res->html = sb_finish(&w->out);
    if (has_excerpt) {
        res->excerpt = persist_excerpt ? arena_dup(a, w->out.buf + ex_off, ex_len)
                                       : str_make(w->out.buf + ex_off, ex_len);
    } else {
        res->excerpt = str_make("", 0);
    }
    res->toc = w->toc;
    res->n_toc = n_toc;
}

static void render_markdown_mode(Arena *a, Str body, MdLinkEnv *env, MdResult *res, int json_mode) {
    MdWork w;
    mdwork_init(&w, a);
    render_markdown_work(&w, body, env, res, json_mode, 1);
    // Copy html/toc out if needed (used only by prof3 / recent pre-render)
    res->html = arena_dup(a, res->html.p, res->html.len);
    if (res->n_toc) {
        TocItem *nt = (TocItem *)arena_alloc(a, res->n_toc * sizeof(TocItem));
        for (uint32_t k = 0; k < res->n_toc; k++) {
            nt[k] = res->toc[k];
            nt[k].html = arena_dup(a, res->toc[k].html.p, res->toc[k].html.len);
        }
        res->toc = nt;
    }
}

static inline void render_markdown(Arena *a, Str body, MdLinkEnv *env, MdResult *res) {
    render_markdown_mode(a, body, env, res, 0);
}
