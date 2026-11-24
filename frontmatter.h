#pragma once
#include "common.h"

typedef struct {
    Str title;
    Str date;
    Str summary;
    Str layout;
    Str slug;
    Str *tags;
    uint32_t n_tags;
    uint8_t has_title;
    uint8_t has_date;
    uint8_t has_summary;
    uint8_t has_layout;
    uint8_t has_slug;
    uint8_t has_tags;
    uint8_t has_draft;
    uint8_t draft;
    Str body;
    Str error; // len > 0 means error
} FrontMatter;

// Normalize \r\n and \r to \n in-place inside mutable buffer
static inline Str normalize_newlines_inplace(Str s) {
    char *p = (char *)s.p;
    uint32_t len = s.len;
    uint32_t i = 0;
#ifdef __AVX2__
    {
        const __m256i v_cr = _mm256_set1_epi8('\r');
        while (i + 32 <= len) {
            __m256i v = _mm256_loadu_si256((const __m256i *)(p + i));
            uint32_t m = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, v_cr));
            if (m) { i += (uint32_t)__builtin_ctz(m); goto found_cr; }
            i += 32;
        }
    }
#endif
    {
        const __m128i v_cr = _mm_set1_epi8('\r');
        while (i + 16 <= len) {
            __m128i v = _mm_loadu_si128((const __m128i *)(p + i));
            uint32_t m = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(v, v_cr));
            if (m) { i += (uint32_t)__builtin_ctz(m); goto found_cr; }
            i += 16;
        }
    }
    while (i < len && p[i] != '\r') i++;
    if (i == len) return s;

found_cr:;
    uint32_t w = i;
    while (i < len) {
        if (p[i] == '\r') {
            p[w++] = '\n';
            if (i + 1 < len && p[i + 1] == '\n') i += 2;
            else i += 1;
        } else {
            p[w++] = p[i++];
        }
    }
    return str_make(p, w);
}

static inline int days_in_month(int y, int m) {
    if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
    return (m == 4 || m == 6 || m == 9 || m == 11) ? 30 : 31;
}

static Str fm_err_fmt_key(Arena *a, const char *pre, Str key, const char *suf) {
    uint32_t pl = 0; while (pre[pl]) pl++;
    uint32_t sl = 0; while (suf[sl]) sl++;
    char *b = arena_alloc_bytes(a, pl + key.len + sl);
    xmemcpy(b, pre, pl);
    xmemcpy(b + pl, key.p, key.len);
    xmemcpy(b + pl + key.len, suf, sl);
    return str_make(b, pl + key.len + sl);
}

static int fm_string_of(Arena *a, Str key, Str raw, Str *out, Str *err) {
    if (raw.len > 0 && raw.p[0] == '"') {
        if (raw.len < 2 || raw.p[raw.len - 1] != '"') {
            *err = fm_err_fmt_key(a, "key \"", key, "\": a quoted string is not closed");
            return 0;
        }
        // Check if any backslash or stray quote
        char *buf = arena_alloc_bytes(a, raw.len - 2);
        uint32_t olen = 0;
        for (uint32_t i = 1; i < raw.len - 1; i++) {
            char c = raw.p[i];
            if (c == '\\') {
                if (i + 1 >= raw.len - 1) {
                    *err = fm_err_fmt_key(a, "key \"", key, "\": a quoted string holds an escape other than \\\" or \\\\");
                    return 0;
                }
                char n = raw.p[i + 1];
                if (n != '"' && n != '\\') {
                    *err = fm_err_fmt_key(a, "key \"", key, "\": a quoted string holds an escape other than \\\" or \\\\");
                    return 0;
                }
                buf[olen++] = n;
                i++;
            } else if (c == '"') {
                *err = fm_err_fmt_key(a, "key \"", key, "\": a quoted string holds a stray quote");
                return 0;
            } else {
                buf[olen++] = c;
            }
        }
        *out = str_make(buf, olen);
        return 1;
    }
    if (raw.len > 0 && raw.p[0] == '[') {
        *err = fm_err_fmt_key(a, "key \"", key, "\": a bare string opens on [");
        return 0;
    }
    *out = raw;
    return 1;
}

static inline int is_valid_slug_fm(Str s) {
    if (s.len == 0 || s.len > 64) return 0;
    int prev_dash = 1;
    for (uint32_t i = 0; i < s.len; i++) {
        char c = s.p[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            prev_dash = 0;
        } else if (c == '-') {
            if (prev_dash) return 0;
            prev_dash = 1;
        } else return 0;
    }
    return !prev_dash;
}

static inline int is_valid_name_fm(Str s) {
    if (s.len == 0) return 0;
    for (uint32_t i = 0; i < s.len; i++) {
        char c = s.p[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return 0;
    }
    return 1;
}

static void parse_front_matter(Arena *a, Str text, FrontMatter *fm) {
    xmemset(fm, 0, sizeof(*fm));
    const char *p = text.p;
    uint32_t len = text.len;

    // Find end of line 0
    uint32_t nl0 = 0;
    while (nl0 < len && p[nl0] != '\n') nl0++;
    if (nl0 != 3 || p[0] != '-' || p[1] != '-' || p[2] != '-') {
        fm->error = str_lit("no front matter");
        return;
    }

    // Find closing "---" line
    uint32_t pos = (nl0 < len) ? nl0 + 1 : len;
    uint32_t close_start = 0, close_after = 0;
    int found_close = 0;
    uint32_t scan = pos;
    while (scan < len || (scan == len && nl0 < len && p[len - 1] == '\n')) {
        uint32_t ls = scan;
        while (scan < len && p[scan] != '\n') scan++;
        uint32_t llen = scan - ls;
        if (llen == 3 && p[ls] == '-' && p[ls + 1] == '-' && p[ls + 2] == '-') {
            close_start = ls;
            close_after = (scan < len) ? scan + 1 : len;
            found_close = 1;
            break;
        }
        if (scan >= len) break;
        scan++;
    }
    if (!found_close) {
        fm->error = str_lit("the front matter is not closed");
        return;
    }

    // Parse lines between line 0 and close_start
    uint32_t cur = pos;
    uint32_t line_no = 2; // 1-based line number (line 1 was opening ---)
    while (cur < close_start) {
        uint32_t ls = cur;
        while (cur < close_start && p[cur] != '\n') cur++;
        uint32_t llen = cur - ls;
        if (cur < close_start && p[cur] == '\n') cur++;

        const char *lp = p + ls;
        // Skip blank ([ \t]*) or comment (#...)
        if (llen > 0 && lp[0] == '#') { line_no++; continue; }
        uint32_t h = 0;
        while (h < llen && (lp[h] == ' ' || lp[h] == '\t')) h++;
        if (h == llen) { line_no++; continue; }

        // Match /^([a-z_]+):(?: (.*))?$/
        uint32_t klen = 0;
        while (klen < llen && ((lp[klen] >= 'a' && lp[klen] <= 'z') || lp[klen] == '_')) klen++;
        if (klen == 0 || klen >= llen || lp[klen] != ':' || (klen + 1 < llen && lp[klen + 1] != ' ')) {
            SB sb; sb_init(&sb, a, 48);
            sb_puts(&sb, "line ", 5);
            sb_putu32(&sb, line_no);
            sb_puts(&sb, " is not \"key: value\"", 20);
            fm->error = sb_finish(&sb);
            return;
        }
        Str key = str_make(lp, klen);
        const char *rp = (klen + 1 < llen) ? (lp + klen + 2) : lp + llen;
        uint32_t rlen = (klen + 1 < llen) ? (llen - (klen + 2)) : 0;
        while (rlen > 0 && (rp[rlen - 1] == ' ' || rp[rlen - 1] == '\t')) rlen--;
        Str raw = str_make(rp, rlen);

        // Check known keys: title, date, tags, draft, layout, slug, summary
        int kid = -1;
        if (str_eq_lit(key, "title", 5)) kid = 0;
        else if (str_eq_lit(key, "date", 4)) kid = 1;
        else if (str_eq_lit(key, "tags", 4)) kid = 2;
        else if (str_eq_lit(key, "draft", 5)) kid = 3;
        else if (str_eq_lit(key, "layout", 6)) kid = 4;
        else if (str_eq_lit(key, "slug", 4)) kid = 5;
        else if (str_eq_lit(key, "summary", 7)) kid = 6;
        else {
            fm->error = fm_err_fmt_key(a, "unknown key \"", key, "\"");
            return;
        }

        uint8_t *has_ptr = NULL;
        if (kid == 0) has_ptr = &fm->has_title;
        else if (kid == 1) has_ptr = &fm->has_date;
        else if (kid == 2) has_ptr = &fm->has_tags;
        else if (kid == 3) has_ptr = &fm->has_draft;
        else if (kid == 4) has_ptr = &fm->has_layout;
        else if (kid == 5) has_ptr = &fm->has_slug;
        else has_ptr = &fm->has_summary;

        if (*has_ptr) {
            fm->error = fm_err_fmt_key(a, "key \"", key, "\" is given twice");
            return;
        }
        if (raw.len == 0) {
            fm->error = fm_err_fmt_key(a, "key \"", key, "\" has no value");
            return;
        }

        if (kid == 0) {
            if (!fm_string_of(a, key, raw, &fm->title, &fm->error)) return;
            fm->has_title = 1;
        } else if (kid == 6) {
            if (!fm_string_of(a, key, raw, &fm->summary, &fm->error)) return;
            fm->has_summary = 1;
        } else if (kid == 1) {
            // YYYY-MM-DD
            int ok = (raw.len == 10 && raw.p[4] == '-' && raw.p[7] == '-');
            for (int q = 0; ok && q < 10; q++) {
                if (q == 4 || q == 7) continue;
                if (raw.p[q] < '0' || raw.p[q] > '9') ok = 0;
            }
            if (ok) {
                int y = (raw.p[0]-'0')*1000 + (raw.p[1]-'0')*100 + (raw.p[2]-'0')*10 + (raw.p[3]-'0');
                int mo = (raw.p[5]-'0')*10 + (raw.p[6]-'0');
                int d = (raw.p[8]-'0')*10 + (raw.p[9]-'0');
                if (y < 1970 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo)) ok = 0;
            }
            if (!ok) {
                fm->error = str_lit("key \"date\": not a date from 1970-01-01 to 2099-12-31 (YYYY-MM-DD)");
                return;
            }
            fm->date = raw;
            fm->has_date = 1;
        } else if (kid == 2) {
            if (raw.len < 2 || raw.p[0] != '[' || raw.p[raw.len - 1] != ']') {
                fm->error = str_lit("key \"tags\": not a list [a, b]");
                return;
            }
            Str inner = str_trim_js(str_make(raw.p + 1, raw.len - 2));
            if (inner.len == 0) {
                fm->tags = NULL;
                fm->n_tags = 0;
            } else {
                // Count commas in raw[1..len-2]
                Str raw_inner = str_make(raw.p + 1, raw.len - 2);
                uint32_t cnt = 1;
                for (uint32_t q = 0; q < raw_inner.len; q++) if (raw_inner.p[q] == ',') cnt++;
                Str *arr = (Str *)arena_alloc(a, cnt * sizeof(Str));
                uint32_t ti = 0, st = 0;
                for (uint32_t q = 0; q <= raw_inner.len; q++) {
                    if (q == raw_inner.len || raw_inner.p[q] == ',') {
                        Str t = str_trim_js(str_make(raw_inner.p + st, q - st));
                        if (t.len == 0) {
                            fm->error = str_lit("key \"tags\": an empty tag, or one holding \" [ or ]");
                            return;
                        }
                        for (uint32_t z = 0; z < t.len; z++) {
                            char ch = t.p[z];
                            if (ch == '"' || ch == '[' || ch == ']') {
                                fm->error = str_lit("key \"tags\": an empty tag, or one holding \" [ or ]");
                                return;
                            }
                        }
                        arr[ti++] = t;
                        st = q + 1;
                    }
                }
                fm->tags = arr;
                fm->n_tags = ti;
            }
            fm->has_tags = 1;
        } else if (kid == 3) {
            if (str_eq_lit(raw, "true", 4)) fm->draft = 1;
            else if (str_eq_lit(raw, "false", 5)) fm->draft = 0;
            else {
                fm->error = str_lit("key \"draft\": true or false");
                return;
            }
            fm->has_draft = 1;
        } else if (kid == 4) {
            if (!is_valid_name_fm(raw)) {
                fm->error = str_lit("key \"layout\": not a template name");
                return;
            }
            fm->layout = raw;
            fm->has_layout = 1;
        } else if (kid == 5) {
            if (!is_valid_slug_fm(raw)) {
                fm->error = str_lit("key \"slug\": not a slug");
                return;
            }
            fm->slug = raw;
            fm->has_slug = 1;
        }
        line_no++;
    }

    if (!fm->has_title) {
        fm->error = str_lit("no title");
        return;
    }
    fm->body = str_make(p + close_after, len - close_after);
}
