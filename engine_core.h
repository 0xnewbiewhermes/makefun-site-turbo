#pragma once
#include "common.h"
#include "json_in.h"
#include "frontmatter.h"
#include "markdown.h"
#include "template.h"

// ---- Generic fast introsort/insertion sort (no libc qsort!) ----
#define DEFINE_SORT(NAME, TYPE, CMP) \
static void NAME##_ins(TYPE *a, int32_t lo, int32_t hi) { \
    for (int32_t i = lo + 1; i <= hi; i++) { \
        TYPE key = a[i]; \
        int32_t j = i - 1; \
        while (j >= lo && CMP(key, a[j]) < 0) { a[j + 1] = a[j]; j--; } \
        a[j + 1] = key; \
    } \
} \
static void NAME##_rec(TYPE *a, int32_t lo, int32_t hi) { \
    while (hi - lo > 16) { \
        int32_t mid = lo + ((hi - lo) >> 1); \
        if (CMP(a[mid], a[lo]) < 0) { TYPE t = a[lo]; a[lo] = a[mid]; a[mid] = t; } \
        if (CMP(a[hi], a[lo]) < 0) { TYPE t = a[lo]; a[lo] = a[hi]; a[hi] = t; } \
        if (CMP(a[hi], a[mid]) < 0) { TYPE t = a[mid]; a[mid] = a[hi]; a[hi] = t; } \
        TYPE piv = a[mid]; \
        int32_t i = lo, j = hi; \
        for (;;) { \
            while (CMP(a[i], piv) < 0) i++; \
            while (CMP(piv, a[j]) < 0) j--; \
            if (i >= j) break; \
            TYPE t = a[i]; a[i] = a[j]; a[j] = t; \
            i++; j--; \
        } \
        if (j - lo < hi - j) { NAME##_rec(a, lo, j); lo = j + 1; } \
        else { NAME##_rec(a, j + 1, hi); hi = j; } \
    } \
    if (lo < hi) NAME##_ins(a, lo, hi); \
} \
static void NAME(TYPE *a, uint32_t n) { \
    if (n > 1) NAME##_rec(a, 0, (int32_t)n - 1); \
}

static inline int cmp_raw_tpl(RawTemplate a, RawTemplate b) { return cmp_utf16(a.name, b.name); }
static inline int cmp_raw_file(RawFile a, RawFile b) { return cmp_utf16(a.path, b.path); }
static inline int cmp_post_ptr(SourceObj *a, SourceObj *b) {
    int c = cmp_utf16(b->date, a->date);
    if (c != 0) return c;
    return cmp_utf16(a->slug, b->slug);
}
static inline int cmp_tag_obj(TagObj a, TagObj b) {
    if (a.n_posts != b.n_posts) return (a.n_posts > b.n_posts) ? -1 : 1;
    return cmp_utf16(a.slug, b.slug);
}
static inline int cmp_str_utf16(Str a, Str b) { return cmp_utf16(a, b); }

DEFINE_SORT(sort_raw_tpls, RawTemplate, cmp_raw_tpl)
DEFINE_SORT(sort_raw_files, RawFile, cmp_raw_file)
DEFINE_SORT(sort_posts, SourceObj *, cmp_post_ptr)
DEFINE_SORT(sort_tags, TagObj, cmp_tag_obj)
DEFINE_SORT(sort_strs, Str, cmp_str_utf16)

// ---- Dynamic Str array in arena (for report lines) ----
typedef struct {
    Str *items;
    uint32_t len;
    uint32_t cap;
    Arena *arena;
} StrVec;

static inline void svec_init(StrVec *v, Arena *a, uint32_t cap) {
    v->arena = a; v->len = 0; v->cap = cap;
    v->items = cap ? (Str *)arena_alloc(a, cap * sizeof(Str)) : NULL;
}

static inline void svec_push(StrVec *v, Str s) {
    if (v->len >= v->cap) {
        uint32_t ncap = v->cap ? v->cap * 2 : 32;
        Str *ni = (Str *)arena_alloc(v->arena, ncap * sizeof(Str));
        if (v->len) xmemcpy(ni, v->items, v->len * sizeof(Str));
        v->items = ni; v->cap = ncap;
    }
    v->items[v->len++] = s;
}

static inline Str dir_of(Str path) {
    for (int32_t i = (int32_t)path.len - 1; i >= 0; i--) {
        if (path.p[i] == '/') return str_make(path.p, (uint32_t)i);
    }
    return str_make("", 0);
}

// resolvePath(dir, rel)
static int resolve_path(Str dir, Str rel, char *out_buf, uint32_t out_cap, Str *out_res) {
    Str segs[64];
    int n_segs = 0;
    if (dir.len > 0) {
        uint32_t st = 0;
        for (uint32_t k = 0; k <= dir.len; k++) {
            if (k == dir.len || dir.p[k] == '/') {
                if (n_segs < 64) segs[n_segs++] = str_make(dir.p + st, k - st);
                st = k + 1;
            }
        }
    }
    uint32_t st = 0;
    for (uint32_t k = 0; k <= rel.len; k++) {
        if (k == rel.len || rel.p[k] == '/') {
            Str seg = str_make(rel.p + st, k - st);
            st = k + 1;
            if (seg.len == 0 || (seg.len == 1 && seg.p[0] == '.')) continue;
            if (seg.len == 2 && seg.p[0] == '.' && seg.p[1] == '.') {
                if (n_segs == 0) return 0;
                n_segs--;
            } else {
                if (n_segs < 64) segs[n_segs++] = seg;
            }
        }
    }
    uint32_t pos = 0;
    for (int i = 0; i < n_segs; i++) {
        if (i > 0) { if (pos >= out_cap) return 0; out_buf[pos++] = '/'; }
        if (pos + segs[i].len > out_cap) return 0;
        xmemcpy(out_buf + pos, segs[i].p, segs[i].len);
        pos += segs[i].len;
    }
    *out_res = str_make(out_buf, pos);
    return 1;
}

typedef struct {
    Arena *arena;
    Str base;
    Str cur_path;
    Str cur_dir;
    const StrMap *by_source;
    const SourceObj *sources;
    const StrMap *static_paths;
    StrMap *broken_set;
    StrVec *broken_vec;
} LinkCtx;

static inline int has_url_scheme(Str t) {
    if (t.len < 2) return 0;
    char c0 = t.p[0];
    if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z'))) return 0;
    for (uint32_t i = 1; i < t.len; i++) {
        char c = t.p[i];
        if (c == ':') return 1;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '+' || c == '-' || c == '.')) return 0;
    }
    return 0;
}

static Str resolve_md_link(MdLinkEnv *env, Str target) {
    LinkCtx *lc = (LinkCtx *)env->ctx;
    if (target.len > 0 && (target.p[0] == '/' || target.p[0] == '#' || has_url_scheme(target))) {
        return target;
    }
    int32_t hpos = md_indexof(target.p, target.len, 0, '#');
    Str rel = (hpos < 0) ? target : str_make(target.p, (uint32_t)hpos);
    Str frag = (hpos < 0) ? str_make("", 0) : str_make(target.p + hpos, target.len - (uint32_t)hpos);
    char pbuf[1024];
    Str resolved;
    if (rel.len > 0 && resolve_path(lc->cur_dir, rel, pbuf, sizeof(pbuf), &resolved)) {
        int32_t sid = smap_get(lc->by_source, resolved);
        if (sid >= 0) {
            Str u = lc->sources[sid].url;
            if (frag.len == 0) return u;
            char *b = arena_alloc_bytes(lc->arena, u.len + frag.len);
            xmemcpy(b, u.p, u.len);
            xmemcpy(b + u.len, frag.p, frag.len);
            return str_make(b, u.len + frag.len);
        }
        if (smap_has(lc->static_paths, resolved)) {
            uint32_t tot = lc->base.len + 1 + resolved.len + frag.len;
            char *b = arena_alloc_bytes(lc->arena, tot);
            xmemcpy(b, lc->base.p, lc->base.len);
            b[lc->base.len] = '/';
            xmemcpy(b + lc->base.len + 1, resolved.p, resolved.len);
            if (frag.len) xmemcpy(b + lc->base.len + 1 + resolved.len, frag.p, frag.len);
            return str_make(b, tot);
        }
    }
    // Broken link
    // Format: "broken-link <s.path> -> <target>"
    uint32_t blen = 12 + lc->cur_path.len + 4 + target.len;
    char *bb = arena_alloc_bytes(lc->arena, blen);
    xmemcpy(bb, "broken-link ", 12);
    xmemcpy(bb + 12, lc->cur_path.p, lc->cur_path.len);
    xmemcpy(bb + 12 + lc->cur_path.len, " -> ", 4);
    xmemcpy(bb + 12 + lc->cur_path.len + 4, target.p, target.len);
    Str bstr = str_make(bb, blen);
    if (smap_put(lc->broken_set, bstr, 1)) {
        svec_push(lc->broken_vec, bstr);
    }
    return target;
}

static inline Str make_refused(Arena *a, Str path, Str reason) {
    uint32_t tot = 8 + path.len + 2 + reason.len;
    char *b = arena_alloc_bytes(a, tot);
    xmemcpy(b, "refused ", 8);
    xmemcpy(b + 8, path.p, path.len);
    b[8 + path.len] = ':'; b[9 + path.len] = ' ';
    xmemcpy(b + 10 + path.len, reason.p, reason.len);
    return str_make(b, tot);
}

static inline void emit_kv_prefix(OutBuf *o, int *first_kv, Str key) {
    if (!*first_kv) out_putc(o, ',');
    *first_kv = 0;
    out_putc(o, '"');
    out_json_body(o, key.p, key.len);
    out_raw(o, "\":\"", 3);
}

// ---- Process one complete site case and stream its JSON object to OutBuf ----
static void process_case(Arena *a, CaseInput *ci, OutBuf *o) {
    StrVec report; svec_init(&report, a, 64);

    // 0. Parse templates in sorted key order
    sort_raw_tpls(ci->templates, ci->n_templates);
    StrMap tpl_map; smap_init(&tpl_map, a, ci->n_templates * 2 + 16);
    TNodeList *tpl_list = (TNodeList *)arena_alloc(a, (ci->n_templates + 1) * sizeof(TNodeList));
    uint32_t n_valid_tpls = 0;

    for (uint32_t i = 0; i < ci->n_templates; i++) {
        Str err;
        if (parse_template(a, ci->templates[i].name, ci->templates[i].src, &tpl_list[n_valid_tpls], &err)) {
            smap_put(&tpl_map, ci->templates[i].name, (int32_t)n_valid_tpls);
            n_valid_tpls++;
        } else {
            svec_push(&report, err);
        }
    }

    // base = site.base_url.replace(/\/+$/, '')
    Str base = ci->site_base_url;
    while (base.len > 0 && base.p[base.len - 1] == '/') base.len--;

    // 1. readSources: sort files by path, split .md vs static, normalize \r\n? -> \n
    sort_raw_files(ci->files, ci->n_files);
    SourceObj *sources = (SourceObj *)arena_alloc(a, (ci->n_files + 1) * sizeof(SourceObj));
    RawFile *statics = (RawFile *)arena_alloc(a, (ci->n_files + 1) * sizeof(RawFile));
    uint32_t n_sources = 0, n_statics = 0;

    for (uint32_t i = 0; i < ci->n_files; i++) {
        RawFile rf = ci->files[i];
        if (str_ends(rf.path, ".md", 3)) {
            Str norm = normalize_newlines_inplace(rf.content);
            // 2. frontMatter (fused with readSources!)
            FrontMatter fm;
            parse_front_matter(a, norm, &fm);
            if (fm.error.len > 0) {
                svec_push(&report, make_refused(a, rf.path, fm.error));
                continue;
            }
            int is_post = str_starts(rf.path, "posts/", 6);
            if (str_starts(rf.path, "tags/", 5)) {
                svec_push(&report, make_refused(a, rf.path, str_lit("tags/ is the generator's own")));
                continue;
            }
            if (str_starts(rf.path, "page/", 5)) {
                svec_push(&report, make_refused(a, rf.path, str_lit("page/ is the generator's own")));
                continue;
            }
            if (is_post && !fm.has_date) {
                svec_push(&report, make_refused(a, rf.path, str_lit("a post needs a date")));
                continue;
            }
            Str layout = fm.has_layout ? fm.layout : (is_post ? str_lit("post") : str_lit("page"));
            if (!smap_has(&tpl_map, layout)) {
                SB sb; sb_init(&sb, a, 64 + layout.len);
                sb_puts(&sb, "layout \"", 8);
                sb_puts(&sb, layout.p, layout.len);
                sb_puts(&sb, "\" is no template that parses", 28);
                svec_push(&report, make_refused(a, rf.path, sb_finish(&sb)));
                continue;
            }
            if (fm.has_draft && fm.draft) {
                char *db = arena_alloc_bytes(a, 6 + rf.path.len);
                xmemcpy(db, "draft ", 6);
                xmemcpy(db + 6, rf.path.p, rf.path.len);
                svec_push(&report, str_make(db, 6 + rf.path.len));
                continue;
            }
            SourceObj *s = &sources[n_sources++];
            xmemset(s, 0, sizeof(*s));
            s->path = rf.path;
            s->text = norm;
            s->body = fm.body;
            s->title = fm.title;
            s->date = fm.has_date ? fm.date : str_make("", 0);
            s->has_date = fm.has_date;
            s->date_long = fm.has_date ? long_date(a, fm.date) : str_make("", 0);
            s->summary = fm.has_summary ? fm.summary : str_make("", 0);
            s->layout = layout;
            s->slug = fm.has_slug ? fm.slug : str_make("", 0); // temp hold explicit slug
            s->fm_tags = fm.tags;
            s->n_fm_tags = fm.n_tags;
            s->is_post = (uint8_t)is_post;
        } else {
            statics[n_statics++] = rf;
        }
    }

    // 3. permalinks
    StrMap dir_map; smap_init(&dir_map, a, 32);
    StrMap *taken_sets = (StrMap *)arena_alloc(a, 64 * sizeof(StrMap));
    uint32_t n_dirs = 0, cap_dirs = 64;
    StrMap by_source; smap_init(&by_source, a, n_sources * 2 + 16);

    for (uint32_t i = 0; i < n_sources; i++) {
        SourceObj *s = &sources[i];
        Str dir = s->is_post ? str_lit("posts") : dir_of(s->path);
        int32_t did = smap_get(&dir_map, dir);
        if (did < 0) {
            if (n_dirs >= cap_dirs) {
                uint32_t ncap = cap_dirs * 2;
                StrMap *nt = (StrMap *)arena_alloc(a, ncap * sizeof(StrMap));
                xmemcpy(nt, taken_sets, n_dirs * sizeof(StrMap));
                taken_sets = nt; cap_dirs = ncap;
            }
            did = (int32_t)n_dirs++;
            smap_init(&taken_sets[did], a, 64);
            if (dir.len == 0) {
                smap_put(&taken_sets[did], str_lit("posts"), 1);
                smap_put(&taken_sets[did], str_lit("tags"), 1);
                smap_put(&taken_sets[did], str_lit("page"), 1);
            }
            smap_put(&dir_map, dir, did);
        }
        Str raw_slug;
        if (s->slug.len > 0) {
            raw_slug = s->slug;
        } else {
            char sbuf[65];
            Str sl = slugify_buf(s->title, sbuf);
            raw_slug = arena_dup(a, sl.p, sl.len);
        }
        s->slug = unique_slug(a, raw_slug, &taken_sets[did]);
        if (dir.len == 0) {
            s->out_dir = s->slug;
        } else {
            uint32_t ol = dir.len + 1 + s->slug.len;
            char *ob = arena_alloc_bytes(a, ol);
            xmemcpy(ob, dir.p, dir.len);
            ob[dir.len] = '/';
            xmemcpy(ob + dir.len + 1, s->slug.p, s->slug.len);
            s->out_dir = str_make(ob, ol);
        }
        uint32_t ul = base.len + 1 + s->out_dir.len + 1;
        char *ub = arena_alloc_bytes(a, ul);
        xmemcpy(ub, base.p, base.len);
        ub[base.len] = '/';
        xmemcpy(ub + base.len + 1, s->out_dir.p, s->out_dir.len);
        ub[ul - 1] = '/';
        s->url = str_make(ub, ul);
        smap_put(&by_source, s->path, (int32_t)i);
    }

    // 4. Prepare link context + word counts (Markdown is rendered LAZILY in L1/L2-hot reusable buffer during 7a!)
    StrMap static_paths; smap_init(&static_paths, a, n_statics * 2 + 16);
    for (uint32_t i = 0; i < n_statics; i++) smap_put(&static_paths, statics[i].path, (int32_t)i);

    StrMap broken_set; smap_init(&broken_set, a, 64);
    StrVec broken_vec; svec_init(&broken_vec, a, 64);

    LinkCtx lctx;
    lctx.arena = a;
    lctx.base = base;
    lctx.by_source = &by_source;
    lctx.sources = sources;
    lctx.static_paths = &static_paths;
    lctx.broken_set = &broken_set;
    lctx.broken_vec = &broken_vec;

    MdLinkEnv menv;
    menv.resolve = resolve_md_link;
    menv.ctx = &lctx;

    int md_json_mode = !templates_use_escaped_md(tpl_list, n_valid_tpls);

    uint32_t n_posts = 0;
    for (uint32_t i = 0; i < n_sources; i++) {
        SourceObj *s = &sources[i];
        uint32_t wc = count_words_js(s->body);
        s->reading_minutes = (wc <= 200) ? 1 : (int32_t)((wc + 199) / 200);
        if (s->is_post) n_posts++;
    }

    // 5. collections (posts sorted newest-first, then slug)
    SourceObj **posts = (SourceObj **)arena_alloc(a, (n_posts + 1) * sizeof(SourceObj *));
    uint32_t pi = 0;
    for (uint32_t i = 0; i < n_sources; i++) {
        if (sources[i].is_post) posts[pi++] = &sources[i];
    }
    sort_posts(posts, n_posts);
    for (uint32_t k = 0; k < n_posts; k++) {
        posts[k]->newer = (k == 0) ? NULL : posts[k - 1];
        posts[k]->older = (k + 1 == n_posts) ? NULL : posts[k + 1];
    }

    // 6. tags
    StrMap tag_by_slug; smap_init(&tag_by_slug, a, 128);
    uint32_t cap_tags = 64, n_tags = 0;
    TagObj *tags = (TagObj *)arena_alloc(a, cap_tags * sizeof(TagObj));

    // Store indices first during accumulation, then build final TagObj* pointers after sorting!
    // Wait: TagObj moves when sorted or grown! So store TagObj* pointers via stable arena-allocated TagObj nodes OR store tag indices and remap after sorting!
    // Storing stable arena-allocated TagObj* is cleaner AND zero remapping!
    TagObj **tag_ptrs = (TagObj **)arena_alloc(a, 64 * sizeof(TagObj *));
    (void)tags;
    for (uint32_t k = 0; k < n_posts; k++) {
        SourceObj *p = posts[k];
        if (p->n_fm_tags == 0) continue;
        p->tag_refs = (TagObj **)arena_alloc(a, p->n_fm_tags * sizeof(TagObj *));
        Str seen_slugs[64];
        uint32_t n_seen = 0;
        StrMap seen_big; int use_big = (p->n_fm_tags > 64);
        if (use_big) smap_init(&seen_big, a, p->n_fm_tags * 2);

        for (uint32_t j = 0; j < p->n_fm_tags; j++) {
            Str tname = p->fm_tags[j];
            char sbuf[65];
            Str tslug = slugify_buf(tname, sbuf);
            // Dedup within this post's own tags
            int dup = 0;
            if (!use_big) {
                for (uint32_t q = 0; q < n_seen; q++) {
                    if (str_eq(seen_slugs[q], tslug)) { dup = 1; break; }
                }
            } else {
                if (smap_has(&seen_big, tslug)) dup = 1;
            }
            if (dup) continue;

            int32_t tid = smap_get(&tag_by_slug, tslug);
            TagObj *tobj;
            if (tid < 0) {
                Str dslug = arena_dup(a, tslug.p, tslug.len);
                tobj = (TagObj *)arena_alloc(a, sizeof(TagObj));
                xmemset(tobj, 0, sizeof(*tobj));
                tobj->name = tname;
                tobj->slug = dslug;
                uint32_t ul = base.len + 6 + dslug.len + 1;
                char *ub = arena_alloc_bytes(a, ul);
                xmemcpy(ub, base.p, base.len);
                xmemcpy(ub + base.len, "/tags/", 6);
                xmemcpy(ub + base.len + 6, dslug.p, dslug.len);
                ub[ul - 1] = '/';
                tobj->url = str_make(ub, ul);
                tobj->cap_posts = 16;
                tobj->posts = (SourceObj **)arena_alloc(a, 16 * sizeof(SourceObj *));
                if (n_tags >= cap_tags) {
                    uint32_t ncap = cap_tags * 2;
                    TagObj **np = (TagObj **)arena_alloc(a, ncap * sizeof(TagObj *));
                    xmemcpy(np, tag_ptrs, n_tags * sizeof(TagObj *));
                    tag_ptrs = np; cap_tags = ncap;
                }
                tid = (int32_t)n_tags;
                tag_ptrs[n_tags++] = tobj;
                smap_put(&tag_by_slug, dslug, tid);
            } else {
                tobj = tag_ptrs[tid];
            }

            if (!use_big) seen_slugs[n_seen++] = tobj->slug;
            else smap_put(&seen_big, tobj->slug, 1);

            if (tobj->n_posts >= tobj->cap_posts) {
                uint32_t ncap = tobj->cap_posts * 2;
                SourceObj **np = (SourceObj **)arena_alloc(a, ncap * sizeof(SourceObj *));
                xmemcpy(np, tobj->posts, tobj->n_posts * sizeof(SourceObj *));
                tobj->posts = np; tobj->cap_posts = ncap;
            }
            tobj->posts[tobj->n_posts++] = p;
            p->tag_refs[p->n_tag_refs++] = tobj;
        }
    }

    // Non-post sources' tags
    for (uint32_t i = 0; i < n_sources; i++) {
        SourceObj *s = &sources[i];
        if (s->is_post || s->n_fm_tags == 0) continue;
        s->tag_refs = (TagObj **)arena_alloc(a, s->n_fm_tags * sizeof(TagObj *));
        Str seen_slugs[64];
        uint32_t n_seen = 0;
        StrMap seen_big; int use_big = (s->n_fm_tags > 64);
        if (use_big) smap_init(&seen_big, a, s->n_fm_tags * 2);

        for (uint32_t j = 0; j < s->n_fm_tags; j++) {
            Str tname = s->fm_tags[j];
            char sbuf[65];
            Str tslug = slugify_buf(tname, sbuf);
            int dup = 0;
            if (!use_big) {
                for (uint32_t q = 0; q < n_seen; q++) {
                    if (str_eq(seen_slugs[q], tslug)) { dup = 1; break; }
                }
            } else {
                if (smap_has(&seen_big, tslug)) dup = 1;
            }
            if (dup) continue;
            int32_t tid = smap_get(&tag_by_slug, tslug);
            TagObj *tobj;
            if (tid >= 0) {
                tobj = tag_ptrs[tid];
            } else {
                tobj = (TagObj *)arena_alloc(a, sizeof(TagObj));
                xmemset(tobj, 0, sizeof(*tobj));
                tobj->name = tname;
                tobj->slug = arena_dup(a, tslug.p, tslug.len);
                tobj->url = str_make("", 0);
            }
            if (!use_big) seen_slugs[n_seen++] = tobj->slug;
            else smap_put(&seen_big, tobj->slug, 1);
            s->tag_refs[s->n_tag_refs++] = tobj;
        }
    }

    // Build sorted TagObj array for all_tags
    TagObj *sorted_tags = (TagObj *)arena_alloc(a, (n_tags + 1) * sizeof(TagObj));
    for (uint32_t i = 0; i < n_tags; i++) sorted_tags[i] = *tag_ptrs[i];
    sort_tags(sorted_tags, n_tags);

    // 7. layouts: pre-render ONLY the n_recent (3-6) posts so `recent` cards have excerpt ready if any header/footer uses it!
    SiteObj site_obj = { ci->site_title, base };
    uint32_t n_recent = (ci->recent <= 0) ? 0 : ((uint32_t)ci->recent < n_posts ? (uint32_t)ci->recent : n_posts);

    MdWork mdw;
    mdwork_init(&mdw, a);
    for (uint32_t k = 0; k < n_recent; k++) {
        SourceObj *s = posts[k];
        lctx.cur_path = s->path;
        lctx.cur_dir = dir_of(s->path);
        MdResult md;
        render_markdown_mode(a, s->body, &menv, &md, md_json_mode);
        s->html = md.html;
        s->excerpt = md.excerpt;
        s->toc = md.toc;
        s->n_toc = md.n_toc;
        s->md_done = 1;
    }

    RenderCtx rctx;
    xmemset(&rctx, 0, sizeof(rctx));
    rctx.site = &site_obj;
    rctx.all_tags = sorted_tags;
    rctx.n_all_tags = n_tags;
    rctx.recent = posts;
    rctx.n_recent = n_recent;
    rctx.tpl_map = &tpl_map;
    rctx.tpl_list = tpl_list;
    rctx.arena = a;
    rctx.md_is_json = (uint8_t)md_json_mode;

    // Cache compiled page-layout templates by template id
    TNodeList *compiled_page_tpls = (TNodeList *)arena_alloc(a, (n_valid_tpls + 1) * sizeof(TNodeList));
    uint8_t *has_compiled_page = (uint8_t *)arena_alloc(a, n_valid_tpls + 1);
    xmemset(has_compiled_page, 0, n_valid_tpls + 1);

    StrMap emitted_paths; smap_init(&emitted_paths, a, (n_sources + n_tags + 64) * 2);

    out_putc(o, '{');
    int first_kv = 1;
    Scope root_sc; xmemset(&root_sc, 0, sizeof(root_sc));

    // 7a. Every page/post through its layout (fused L1/L2-hot markdown render + immediate emit!)
    for (uint32_t i = 0; i < n_sources; i++) {
        SourceObj *s = &sources[i];
        int32_t tid = smap_get(&tpl_map, s->layout);
        if (!has_compiled_page[tid]) {
            compiled_page_tpls[tid] = compile_template_for(a, &tpl_list[tid], &rctx, ROOT_MASK_PAGE);
            has_compiled_page[tid] = 1;
        }
        if (!s->md_done) {
            lctx.cur_path = s->path;
            lctx.cur_dir = dir_of(s->path);
            MdResult md;
            render_markdown_work(&mdw, s->body, &menv, &md, md_json_mode, s->is_post);
            s->html = md.html;
            s->excerpt = md.excerpt;
            s->toc = md.toc;
            s->n_toc = md.n_toc;
            s->md_done = 1;
        }
        // Output key: "<s.outDir>/index.html"
        uint32_t klen = s->out_dir.len + 11;
        char *kb = arena_alloc_bytes(a, klen);
        xmemcpy(kb, s->out_dir.p, s->out_dir.len);
        xmemcpy(kb + s->out_dir.len, "/index.html", 11);
        Str out_key = str_make(kb, klen);
        smap_put(&emitted_paths, out_key, 1);

        emit_kv_prefix(o, &first_kv, out_key);
        rctx.page = s;
        render_nodes_out(o, &compiled_page_tpls[tid], &root_sc, &rctx, 0);
        out_putc(o, '"');
    }
    rctx.page = NULL;

    // 7b. Index pages
    int32_t idx_tid = smap_get(&tpl_map, str_lit("index"));
    if (idx_tid >= 0) {
        TNodeList comp_idx = compile_template_for(a, &tpl_list[idx_tid], &rctx, ROOT_MASK_POSTS | ROOT_MASK_PAGER);
        int32_t per = ci->per_page;
        uint32_t pages = (per > 0 && n_posts > 0) ? ((n_posts + (uint32_t)per - 1) / (uint32_t)per) : 1;
        if (pages < 1) pages = 1;

        for (uint32_t k = 1; k <= pages; k++) {
            uint32_t start_i = (k - 1) * (uint32_t)per;
            uint32_t end_i = start_i + (uint32_t)per;
            if (end_i > n_posts) end_i = n_posts;
            if (start_i > n_posts) start_i = n_posts;

            PagerObj pg;
            pg.number = (int32_t)k;
            pg.total = (int32_t)pages;

            {
                SB sb; sb_init(&sb, a, base.len + 24);
                sb_puts(&sb, base.p, base.len);
                if (k == 1) sb_putc(&sb, '/');
                else { sb_puts(&sb, "/page/", 6); sb_putu32(&sb, k); sb_putc(&sb, '/'); }
                pg.url = sb_finish(&sb);
            }
            if (k == 1) {
                pg.newer_url = str_make("", 0);
            } else {
                SB sb; sb_init(&sb, a, base.len + 24);
                sb_puts(&sb, base.p, base.len);
                if (k == 2) sb_putc(&sb, '/');
                else { sb_puts(&sb, "/page/", 6); sb_putu32(&sb, k - 1); sb_putc(&sb, '/'); }
                pg.newer_url = sb_finish(&sb);
            }
            if (k == pages) {
                pg.older_url = str_make("", 0);
            } else {
                SB sb; sb_init(&sb, a, base.len + 24);
                sb_puts(&sb, base.p, base.len);
                sb_puts(&sb, "/page/", 6); sb_putu32(&sb, k + 1); sb_putc(&sb, '/');
                pg.older_url = sb_finish(&sb);
            }

            Str out_key;
            if (k == 1) {
                out_key = str_lit("index.html");
            } else {
                SB sb; sb_init(&sb, a, 32);
                sb_puts(&sb, "page/", 5); sb_putu32(&sb, k); sb_puts(&sb, "/index.html", 11);
                out_key = sb_finish(&sb);
            }
            smap_put(&emitted_paths, out_key, 1);

            emit_kv_prefix(o, &first_kv, out_key);
            rctx.posts = posts + start_i;
            rctx.n_posts = end_i - start_i;
            rctx.has_posts = 1;
            rctx.pager = &pg;
            render_nodes_out(o, &comp_idx, &root_sc, &rctx, 0);
            out_putc(o, '"');
        }
        rctx.posts = NULL; rctx.n_posts = 0; rctx.has_posts = 0; rctx.pager = NULL;
    }

    // 7c. Tag pages
    int32_t tag_tid = smap_get(&tpl_map, str_lit("tag"));
    if (tag_tid >= 0) {
        TNodeList comp_tag = compile_template_for(a, &tpl_list[tag_tid], &rctx, ROOT_MASK_TAG);
        for (uint32_t i = 0; i < n_tags; i++) {
            TagObj *t = &sorted_tags[i];
            uint32_t klen = 5 + t->slug.len + 11;
            char *kb = arena_alloc_bytes(a, klen);
            xmemcpy(kb, "tags/", 5);
            xmemcpy(kb + 5, t->slug.p, t->slug.len);
            xmemcpy(kb + 5 + t->slug.len, "/index.html", 11);
            Str out_key = str_make(kb, klen);
            smap_put(&emitted_paths, out_key, 1);

            emit_kv_prefix(o, &first_kv, out_key);
            rctx.tag = t;
            render_nodes_out(o, &comp_tag, &root_sc, &rctx, 0);
            out_putc(o, '"');
        }
        rctx.tag = NULL;
    }

    // 7d. Tags index page
    int32_t tags_tid = smap_get(&tpl_map, str_lit("tags"));
    if (tags_tid >= 0) {
        TNodeList comp_tags = compile_template_for(a, &tpl_list[tags_tid], &rctx, 0);
        Str out_key = str_lit("tags/index.html");
        smap_put(&emitted_paths, out_key, 1);
        emit_kv_prefix(o, &first_kv, out_key);
        render_nodes_out(o, &comp_tags, &root_sc, &rctx, 0);
        out_putc(o, '"');
    }

    // 7e. Static files
    for (uint32_t i = 0; i < n_statics; i++) {
        RawFile f = statics[i];
        if (smap_has(&emitted_paths, f.path)) {
            SB sb; sb_init(&sb, a, 48 + f.path.len);
            sb_puts(&sb, "shadowed ", 9);
            sb_puts(&sb, f.path.p, f.path.len);
            sb_puts(&sb, ": a built page has its path", 27);
            svec_push(&report, sb_finish(&sb));
        } else {
            // Note: static file paths are unique in input.files? If duplicate in output, JSON last wins,
            // makefun-site sets b.output[f.path] = f.content (checked against Object.hasOwn(b.output, f.path)!)
            smap_put(&emitted_paths, f.path, 1);
            emit_kv_prefix(o, &first_kv, f.path);
            out_json_body(o, f.content.p, f.content.len);
            out_putc(o, '"');
        }
    }

    // 8. Report
    uint32_t total_lines = report.len + broken_vec.len;
    Str *all_lines = (Str *)arena_alloc(a, (total_lines + 1) * sizeof(Str));
    if (report.len) xmemcpy(all_lines, report.items, report.len * sizeof(Str));
    if (broken_vec.len) xmemcpy(all_lines + report.len, broken_vec.items, broken_vec.len * sizeof(Str));
    sort_strs(all_lines, total_lines);

    uint32_t n_pages = n_sources - n_posts;
    emit_kv_prefix(o, &first_kv, str_lit("build-report.txt"));
    for (uint32_t i = 0; i < total_lines; i++) {
        out_json_body(o, all_lines[i].p, all_lines[i].len);
        out_raw(o, "\\n", 2);
    }
    out_raw(o, "built ", 6);
    out_putu32(o, n_pages);
    out_raw(o, " pages, ", 8);
    out_putu32(o, n_posts);
    out_raw(o, " posts, ", 8);
    out_putu32(o, n_tags);
    out_raw(o, " tags and ", 10);
    out_putu32(o, n_statics);
    out_raw(o, " static files\\n\"", 16);

    out_putc(o, '}');
}

// Top-level runner: reads cases_buf [0..cases_len), writes JSON array to out_fd
static void run_engine_impl(char *cases_buf, size_t cases_len, uint8_t *arena_mem, size_t arena_cap,
                            uint8_t *out_mem, long out_fd) {
    OutBuf o;
    o.buf = out_mem;
    o.pos = 0;
    o.fd = out_fd;

    Arena arena;
    arena_init(&arena, arena_mem, arena_cap);

    JParser jp;
    jp.p = cases_buf;
    jp.end = cases_buf + cases_len;

    out_putc(&o, '[');
    jp_ws(&jp);
    if (jp.p < jp.end && *jp.p == '[') {
        jp.p++;
        jp_ws(&jp);
        int first_case = 1;
        while (jp.p < jp.end && *jp.p != ']') {
            arena_reset(&arena);
            CaseInput ci;
            if (!jp_parse_case(&jp, &arena, &ci)) break;
            if (!first_case) out_putc(&o, ',');
            first_case = 0;
            process_case(&arena, &ci, &o);
            jp_ws(&jp);
            if (jp.p < jp.end && *jp.p == ',') { jp.p++; jp_ws(&jp); }
            else break;
        }
    }
    out_putc(&o, ']');
    out_flush(&o);
    arena_reset(&arena);
}
