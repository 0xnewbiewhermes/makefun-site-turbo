#pragma once
#include "common.h"

typedef struct {
    Str name;
    Str src;
} RawTemplate;

typedef struct {
    Str path;
    Str content;
} RawFile;

typedef struct {
    Str site_title;
    Str site_base_url;
    int32_t per_page;
    int32_t recent;
    RawTemplate *templates;
    uint32_t n_templates;
    uint32_t cap_templates;
    RawFile *files;
    uint32_t n_files;
    uint32_t cap_files;
} CaseInput;

typedef struct {
    char *p;
    char *end;
} JParser;

static inline void jp_ws(JParser *j) {
    char *p = j->p;
    char *e = j->end;
    while (p < e) {
        uint8_t c = (uint8_t)*p;
        if (c > ' ') break;
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') p++;
        else break;
    }
    j->p = p;
}

static inline int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static inline uint32_t parse_u4(const char *p) {
    return ((uint32_t)hex_val(p[0]) << 12) | ((uint32_t)hex_val(p[1]) << 8) |
           ((uint32_t)hex_val(p[2]) << 4) | (uint32_t)hex_val(p[3]);
}

// Parse JSON string in-place inside mutable input buffer (padded by >= 32 bytes)
static Str jp_str(JParser *j) {
    jp_ws(j);
    if (j->p >= j->end || *j->p != '"') return str_make("", 0);
    char *start = ++j->p;
    char *p = start;
    char *end = j->end;

    // Fast SIMD scan for first '"' or '\\'
#ifdef __AVX2__
    {
        const __m256i v_dq = _mm256_set1_epi8('"');
        const __m256i v_bs = _mm256_set1_epi8('\\');
        while (p + 32 <= end) {
            __m256i v = _mm256_loadu_si256((const __m256i *)p);
            __m256i m = _mm256_or_si256(_mm256_cmpeq_epi8(v, v_dq), _mm256_cmpeq_epi8(v, v_bs));
            uint32_t mask = (uint32_t)_mm256_movemask_epi8(m);
            if (mask) { p += (uint32_t)__builtin_ctz(mask); goto hit; }
            p += 32;
        }
    }
#endif
    {
        const __m128i v_dq = _mm_set1_epi8('"');
        const __m128i v_bs = _mm_set1_epi8('\\');
        while (p + 16 <= end) {
            __m128i v = _mm_loadu_si128((const __m128i *)p);
            __m128i m = _mm_or_si128(_mm_cmpeq_epi8(v, v_dq), _mm_cmpeq_epi8(v, v_bs));
            uint32_t mask = (uint32_t)_mm_movemask_epi8(m);
            if (mask) { p += (uint32_t)__builtin_ctz(mask); goto hit; }
            p += 16;
        }
    }
    while (p < end && *p != '"' && *p != '\\') p++;

hit:
    if (p < end && *p == '"') {
        j->p = p + 1;
        return str_make(start, (uint32_t)(p - start));
    }

    // Contains backslash escapes: unescape in-place with hoisted SIMD + safe speculative stores!
    char *dst = p;
#ifdef __AVX2__
    const __m256i v_dq = _mm256_set1_epi8('"');
    const __m256i v_bs = _mm256_set1_epi8('\\');
#else
    const __m128i v_dq = _mm_set1_epi8('"');
    const __m128i v_bs = _mm_set1_epi8('\\');
#endif
    while (p < end) {
        if (__builtin_expect(*p == '\\', 1)) {
            char e = p[1];
            if (__builtin_expect(e == 'n', 1)) { *dst++ = '\n'; p += 2; }
            else if (e == '"') { *dst++ = '"'; p += 2; }
            else if (e == '\\') { *dst++ = '\\'; p += 2; }
            else if (e == 'r') { *dst++ = '\r'; p += 2; }
            else if (e == 't') { *dst++ = '\t'; p += 2; }
            else if (e == '/') { *dst++ = '/'; p += 2; }
            else if (e == 'b') { *dst++ = '\b'; p += 2; }
            else if (e == 'f') { *dst++ = '\f'; p += 2; }
            else if (e == 'u' && p + 6 <= end) {
                uint32_t cp = parse_u4(p + 2);
                p += 6;
                if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= end && p[0] == '\\' && p[1] == 'u') {
                    uint32_t lo = parse_u4(p + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
                        p += 6;
                    }
                }
                dst += utf8_encode(cp, dst);
            } else {
                p++;
                if (p < end) *dst++ = *p++;
            }
        } else if (*p == '"') {
            j->p = p + 1;
            return str_make(start, (uint32_t)(dst - start));
        }

        // Fast SIMD scan & copy until next '"' or '\\'
#ifdef __AVX2__
        for (;;) {
            __m256i v = _mm256_loadu_si256((const __m256i *)p);
            __m256i m = _mm256_or_si256(_mm256_cmpeq_epi8(v, v_dq), _mm256_cmpeq_epi8(v, v_bs));
            uint32_t mask = (uint32_t)_mm256_movemask_epi8(m);
            if (!mask && p + 32 <= end) {
                _mm256_storeu_si256((__m256i *)dst, v);
                p += 32; dst += 32;
                continue;
            }
            uint32_t adv = mask ? (uint32_t)__builtin_ctz(mask) : 32u;
            if (p + adv > end) adv = (uint32_t)(end - p);
            if (__builtin_expect((size_t)(p - dst) >= 32u, 1)) {
                // dst + 32 <= p: speculative store NEVER clobbers unread input!
                _mm256_storeu_si256((__m256i *)dst, v);
                p += adv; dst += adv;
            } else {
                if (adv >= 16) { __m128i t = _mm_loadu_si128((const __m128i *)p); _mm_storeu_si128((__m128i *)dst, t); p += 16; dst += 16; adv -= 16; }
                if (adv >= 8)  { uint64_t t; __builtin_memcpy(&t, p, 8); __builtin_memcpy(dst, &t, 8); p += 8; dst += 8; adv -= 8; }
                if (adv >= 4)  { uint32_t t; __builtin_memcpy(&t, p, 4); __builtin_memcpy(dst, &t, 4); p += 4; dst += 4; adv -= 4; }
                while (adv--) *dst++ = *p++;
            }
            break;
        }
#else
        for (;;) {
            __m128i v = _mm_loadu_si128((const __m128i *)p);
            __m128i m = _mm_or_si128(_mm_cmpeq_epi8(v, v_dq), _mm_cmpeq_epi8(v, v_bs));
            uint32_t mask = (uint32_t)_mm_movemask_epi8(m);
            if (!mask && p + 16 <= end) {
                _mm_storeu_si128((__m128i *)dst, v);
                p += 16; dst += 16;
                continue;
            }
            uint32_t adv = mask ? (uint32_t)__builtin_ctz(mask) : 16u;
            if (p + adv > end) adv = (uint32_t)(end - p);
            if (__builtin_expect((size_t)(p - dst) >= 16u, 1)) {
                _mm_storeu_si128((__m128i *)dst, v);
                p += adv; dst += adv;
            } else {
                if (adv >= 8)  { uint64_t t; __builtin_memcpy(&t, p, 8); __builtin_memcpy(dst, &t, 8); p += 8; dst += 8; adv -= 8; }
                if (adv >= 4)  { uint32_t t; __builtin_memcpy(&t, p, 4); __builtin_memcpy(dst, &t, 4); p += 4; dst += 4; adv -= 4; }
                while (adv--) *dst++ = *p++;
            }
            break;
        }
#endif
    }
    j->p = p;
    return str_make(start, (uint32_t)(dst - start));
}

static int32_t jp_int(JParser *j) {
    jp_ws(j);
    int neg = 0;
    if (j->p < j->end && *j->p == '-') { neg = 1; j->p++; }
    int64_t v = 0;
    while (j->p < j->end && *j->p >= '0' && *j->p <= '9') {
        v = v * 10 + (*j->p - '0');
        j->p++;
    }
    // Skip any fractional/exponent part just in case
    if (j->p < j->end && (*j->p == '.' || *j->p == 'e' || *j->p == 'E')) {
        while (j->p < j->end) {
            char c = *j->p;
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') j->p++;
            else break;
        }
    }
    return (int32_t)(neg ? -v : v);
}

static void jp_skip(JParser *j) {
    jp_ws(j);
    if (j->p >= j->end) return;
    char c = *j->p;
    if (c == '"') {
        j->p++;
        while (j->p < j->end) {
            if (*j->p == '\\') { j->p += 2; continue; }
            if (*j->p == '"') { j->p++; return; }
            j->p++;
        }
    } else if (c == '{') {
        j->p++;
        jp_ws(j);
        if (j->p < j->end && *j->p == '}') { j->p++; return; }
        while (j->p < j->end) {
            jp_skip(j);
            jp_ws(j);
            if (j->p < j->end && *j->p == ':') j->p++;
            jp_skip(j);
            jp_ws(j);
            if (j->p < j->end && *j->p == ',') { j->p++; continue; }
            if (j->p < j->end && *j->p == '}') { j->p++; return; }
            break;
        }
    } else if (c == '[') {
        j->p++;
        jp_ws(j);
        if (j->p < j->end && *j->p == ']') { j->p++; return; }
        while (j->p < j->end) {
            jp_skip(j);
            jp_ws(j);
            if (j->p < j->end && *j->p == ',') { j->p++; continue; }
            if (j->p < j->end && *j->p == ']') { j->p++; return; }
            break;
        }
    } else {
        while (j->p < j->end) {
            char ch = *j->p;
            if (ch == ',' || ch == '}' || ch == ']' || ch <= ' ') break;
            j->p++;
        }
    }
}

static void ci_push_tpl(Arena *a, CaseInput *ci, Str name, Str src) {
    if (ci->n_templates >= ci->cap_templates) {
        uint32_t ncap = ci->cap_templates ? ci->cap_templates * 2 : 64;
        RawTemplate *nt = (RawTemplate *)arena_alloc(a, ncap * sizeof(RawTemplate));
        if (ci->n_templates) xmemcpy(nt, ci->templates, ci->n_templates * sizeof(RawTemplate));
        ci->templates = nt;
        ci->cap_templates = ncap;
    }
    ci->templates[ci->n_templates].name = name;
    ci->templates[ci->n_templates].src = src;
    ci->n_templates++;
}

static void ci_push_file(Arena *a, CaseInput *ci, Str path, Str content) {
    if (ci->n_files >= ci->cap_files) {
        uint32_t ncap = ci->cap_files ? ci->cap_files * 2 : 768;
        RawFile *nf = (RawFile *)arena_alloc(a, ncap * sizeof(RawFile));
        if (ci->n_files) xmemcpy(nf, ci->files, ci->n_files * sizeof(RawFile));
        ci->files = nf;
        ci->cap_files = ncap;
    }
    ci->files[ci->n_files].path = path;
    ci->files[ci->n_files].content = content;
    ci->n_files++;
}

static void jp_parse_input_obj(JParser *j, Arena *a, CaseInput *ci) {
    xmemset(ci, 0, sizeof(*ci));
    jp_ws(j);
    if (j->p >= j->end || *j->p != '{') return;
    j->p++;
    jp_ws(j);
    if (j->p < j->end && *j->p == '}') { j->p++; return; }
    while (j->p < j->end) {
        Str k = jp_str(j);
        jp_ws(j); if (j->p < j->end && *j->p == ':') j->p++;
        jp_ws(j);
        if (str_eq_lit(k, "site", 4)) {
            if (j->p < j->end && *j->p == '{') {
                j->p++; jp_ws(j);
                if (j->p < j->end && *j->p == '}') { j->p++; }
                else {
                    while (j->p < j->end) {
                        Str sk = jp_str(j);
                        jp_ws(j); if (j->p < j->end && *j->p == ':') j->p++;
                        if (str_eq_lit(sk, "title", 5)) ci->site_title = jp_str(j);
                        else if (str_eq_lit(sk, "base_url", 8)) ci->site_base_url = jp_str(j);
                        else if (str_eq_lit(sk, "per_page", 8)) ci->per_page = jp_int(j);
                        else if (str_eq_lit(sk, "recent", 6)) ci->recent = jp_int(j);
                        else jp_skip(j);
                        jp_ws(j);
                        if (j->p < j->end && *j->p == ',') { j->p++; continue; }
                        if (j->p < j->end && *j->p == '}') { j->p++; break; }
                        break;
                    }
                }
            } else jp_skip(j);
        } else if (str_eq_lit(k, "templates", 9)) {
            if (j->p < j->end && *j->p == '{') {
                j->p++; jp_ws(j);
                if (j->p < j->end && *j->p == '}') { j->p++; }
                else {
                    while (j->p < j->end) {
                        Str tn = jp_str(j);
                        jp_ws(j); if (j->p < j->end && *j->p == ':') j->p++;
                        Str ts = jp_str(j);
                        ci_push_tpl(a, ci, tn, ts);
                        jp_ws(j);
                        if (j->p < j->end && *j->p == ',') { j->p++; continue; }
                        if (j->p < j->end && *j->p == '}') { j->p++; break; }
                        break;
                    }
                }
            } else jp_skip(j);
        } else if (str_eq_lit(k, "files", 5)) {
            if (j->p < j->end && *j->p == '[') {
                j->p++; jp_ws(j);
                if (j->p < j->end && *j->p == ']') { j->p++; }
                else {
                    while (j->p < j->end) {
                        jp_ws(j);
                        Str fp = str_make("", 0), fc = str_make("", 0);
                        if (j->p < j->end && *j->p == '{') {
                            j->p++; jp_ws(j);
                            if (j->p < j->end && *j->p == '}') { j->p++; }
                            else {
                                while (j->p < j->end) {
                                    Str fk = jp_str(j);
                                    jp_ws(j); if (j->p < j->end && *j->p == ':') j->p++;
                                    if (str_eq_lit(fk, "path", 4)) fp = jp_str(j);
                                    else if (str_eq_lit(fk, "content", 7)) fc = jp_str(j);
                                    else jp_skip(j);
                                    jp_ws(j);
                                    if (j->p < j->end && *j->p == ',') { j->p++; continue; }
                                    if (j->p < j->end && *j->p == '}') { j->p++; break; }
                                    break;
                                }
                            }
                        } else jp_skip(j);
                        ci_push_file(a, ci, fp, fc);
                        jp_ws(j);
                        if (j->p < j->end && *j->p == ',') { j->p++; continue; }
                        if (j->p < j->end && *j->p == ']') { j->p++; break; }
                        break;
                    }
                }
            } else jp_skip(j);
        } else {
            jp_skip(j);
        }
        jp_ws(j);
        if (j->p < j->end && *j->p == ',') { j->p++; continue; }
        if (j->p < j->end && *j->p == '}') { j->p++; break; }
        break;
    }
}

// Parse one case object { "id": ..., "input": { ... } }
static int jp_parse_case(JParser *j, Arena *a, CaseInput *ci) {
    jp_ws(j);
    if (j->p >= j->end || *j->p != '{') return 0;
    j->p++;
    jp_ws(j);
    if (j->p < j->end && *j->p == '}') { j->p++; return 1; }
    while (j->p < j->end) {
        Str k = jp_str(j);
        jp_ws(j); if (j->p < j->end && *j->p == ':') j->p++;
        if (str_eq_lit(k, "input", 5)) {
            jp_parse_input_obj(j, a, ci);
        } else {
            jp_skip(j);
        }
        jp_ws(j);
        if (j->p < j->end && *j->p == ',') { j->p++; continue; }
        if (j->p < j->end && *j->p == '}') { j->p++; break; }
        break;
    }
    return 1;
}
