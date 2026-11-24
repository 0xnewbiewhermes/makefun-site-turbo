#pragma once
#include "common.h"
#include "markdown.h"

// Forward declarations of domain structs shared between engine_core.h and template.h
typedef struct TagObj TagObj;
typedef struct SourceObj SourceObj;

struct TagObj {
    Str name;
    Str slug;
    Str url;
    SourceObj **posts;
    uint32_t n_posts;
    uint32_t cap_posts;
};

struct SourceObj {
    Str path;
    Str text;
    Str body;
    Str title;
    Str date;
    Str date_long;
    Str summary;
    Str layout;
    Str slug;
    Str out_dir;
    Str url;
    Str html;
    Str excerpt;
    TocItem *toc;
    uint32_t n_toc;
    Str *fm_tags;
    uint32_t n_fm_tags;
    TagObj **tag_refs;
    uint32_t n_tag_refs;
    SourceObj *newer;
    SourceObj *older;
    int32_t reading_minutes;
    uint8_t is_post;
    uint8_t has_date;
    uint8_t md_done;
    // Cached pre-JSON-escaped card rendering (shared between index and tag pages!)
    Str card_cache;
    uint64_t card_cache_key;
};

typedef struct {
    Str title;
    Str base_url;
} SiteObj;

typedef struct {
    int32_t number;
    int32_t total;
    Str url;
    Str newer_url;
    Str older_url;
} PagerObj;

typedef struct {
    int32_t index;
    int32_t length;
    uint8_t first;
    uint8_t last;
} LoopObj;

// ---- Template AST ----
enum {
    TNODE_TEXT = 0,     // raw unescaped text (in parsed AST) or pre-JSON-escaped (in compiled AST)
    TNODE_VAR = 1,      // {{ path }}
    TNODE_RAW = 2,      // {{{ path }}}
    TNODE_IF = 3,       // {% if [not] path %} ... {% else %} ... {% endif %}
    TNODE_FOR = 4,      // {% for var in path %} ... {% endfor %}
    TNODE_INCLUDE = 5,  // {% include "name" %}
};

typedef struct TNode TNode;
typedef struct {
    TNode *items;
    uint32_t len;
    uint32_t cap;
} TNodeList;

struct TNode {
    uint8_t kind;
    uint8_t is_not;       // for IF
    uint8_t is_json_lit;  // for TEXT: 1 = already JSON-escaped
    uint8_t cacheable_card_loop; // for FOR over posts/tag.posts whose body only uses loop var + constants
    uint8_t fast_op;      // direct integer opcode (0 = generic scope_lookup fallback)
    uint8_t fast_depth;   // scope climb steps for loop-var fast_op
    Str text;             // for TEXT, INCLUDE name, or FOR loop var name
    Str *path;            // for VAR, RAW, IF, FOR
    uint32_t path_len;
    uint64_t body_hash;   // for cacheable_card_loop
    TNodeList body;       // for IF then-branch or FOR body
    TNodeList else_body;  // for IF else-branch
    uint8_t has_else;
};

static inline void tlist_push(Arena *a, TNodeList *list, TNode node) {
    if (list->len >= list->cap) {
        uint32_t ncap = list->cap ? list->cap * 2 : 8;
        TNode *ni = (TNode *)arena_alloc(a, ncap * sizeof(TNode));
        if (list->len) xmemcpy(ni, list->items, list->len * sizeof(TNode));
        list->items = ni;
        list->cap = ncap;
    }
    list->items[list->len++] = node;
}

static inline int is_ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
static inline int is_ident_cont(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

// Parse path: /^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*$/
static int tpl_parse_path(Arena *a, Str tname, Str text, Str **out_parts, uint32_t *out_len, Str *err) {
    int ok = text.len > 0;
    uint32_t segs = 0;
    uint32_t i = 0;
    while (ok && i < text.len) {
        if (!is_ident_start(text.p[i])) { ok = 0; break; }
        i++;
        while (i < text.len && is_ident_cont(text.p[i])) i++;
        segs++;
        if (i < text.len) {
            if (text.p[i] != '.') { ok = 0; break; }
            i++;
            if (i == text.len) { ok = 0; break; }
        }
    }
    if (!ok || segs == 0) {
        SB sb; sb_init(&sb, a, 64 + tname.len + text.len);
        sb_puts(&sb, "template \"", 10);
        sb_puts(&sb, tname.p, tname.len);
        sb_puts(&sb, "\": \"", 4);
        sb_put_json_body(&sb, text.p, text.len);
        sb_puts(&sb, "\" is not a path", 15);
        *err = sb_finish(&sb);
        return 0;
    }
    Str *parts = (Str *)arena_alloc(a, segs * sizeof(Str));
    uint32_t pi = 0, st = 0;
    for (uint32_t k = 0; k <= text.len; k++) {
        if (k == text.len || text.p[k] == '.') {
            parts[pi++] = str_make(text.p + st, k - st);
            st = k + 1;
        }
    }
    *out_parts = parts;
    *out_len = segs;
    return 1;
}

static Str tpl_err_simple(Arena *a, Str tname, const char *mid, Str arg, const char *suf) {
    SB sb; sb_init(&sb, a, 64 + tname.len + arg.len);
    sb_puts(&sb, "template \"", 10);
    sb_puts(&sb, tname.p, tname.len);
    sb_puts(&sb, "\": ", 3);
    if (mid) { uint32_t ml = 0; while (mid[ml]) ml++; sb_puts(&sb, mid, ml); }
    if (arg.len) sb_puts(&sb, arg.p, arg.len);
    if (suf) { uint32_t sl = 0; while (suf[sl]) sl++; sb_puts(&sb, suf, sl); }
    return sb_finish(&sb);
}

typedef struct {
    TNodeList *target;
    TNode *owner; // NULL for root
    int owner_kind; // 0=root, TNODE_FOR, TNODE_IF
    TNodeList *parent_list;
    uint32_t owner_idx;
} ParseFrame;

static int parse_template(Arena *a, Str tname, Str src, TNodeList *out_root, Str *err) {
    xmemset(out_root, 0, sizeof(*out_root));
    *err = str_make("", 0);

    // Tokenize + parse simultaneously
    typedef struct {
        TNodeList *parent_list;
        uint32_t node_idx; // index in parent_list (wait: parent_list->items can realloc! So track stack of TNodeList directly? Better: build tree with pointer-stable TNode* OR index-based!)
    } Dummy;
    (void)sizeof(Dummy);

    // Since TNodeList can realloc, allocate a stack of TNode* pointers? Wait: we only need stack of TNode* if TNode is heap/arena-allocated OR we store children as TNodeList on stack and copy into parent on pop!
    // YES!! Storing the open TNode on the stack and pushing it to its parent's list on pop (or mutating stack[top]) avoids ALL dangling pointers!
    typedef struct {
        TNode node;
        int in_else;
    } StackItem;

    StackItem stack[64];
    int sp = 0;
    xmemset(&stack[0], 0, sizeof(StackItem));

    SB text_acc; sb_init(&text_acc, a, 256);

    const char *p = src.p;
    uint32_t len = src.len;
    uint32_t i = 0;

    while (i < len) {
        int32_t open_pos = md_indexof(p, len, i, '{');
        if (open_pos < 0 || (uint32_t)open_pos + 1 >= len) {
            sb_puts(&text_acc, p + i, len - i);
            break;
        }
        uint32_t open = (uint32_t)open_pos;
        char next = p[open + 1];
        if (next != '{' && next != '%') {
            sb_puts(&text_acc, p + i, open + 1 - i);
            i = open + 1;
            continue;
        }
        sb_puts(&text_acc, p + i, open - i);

        if (next == '{') {
            int raw = (open + 2 < len && p[open + 2] == '{');
            uint32_t start_search = open + (raw ? 3 : 2);
            int32_t close = -1;
            if (raw) {
                for (uint32_t k = start_search; k + 2 < len; k++) {
                    if (p[k] == '}' && p[k + 1] == '}' && p[k + 2] == '}') { close = (int32_t)k; break; }
                }
            } else {
                for (uint32_t k = start_search; k + 1 < len; k++) {
                    if (p[k] == '}' && p[k + 1] == '}') { close = (int32_t)k; break; }
                }
            }
            if (close < 0) {
                *err = tpl_err_simple(a, tname, raw ? "a {{{ is not closed" : "a {{ is not closed", str_make("", 0), NULL);
                return 0;
            }
            TNodeList *cur_body = stack[sp].in_else ? &stack[sp].node.else_body : &stack[sp].node.body;
            if (text_acc.len > 0) {
                TNode tn; xmemset(&tn, 0, sizeof(tn));
                tn.kind = TNODE_TEXT;
                tn.text = arena_dup(a, text_acc.buf, text_acc.len);
                tlist_push(a, cur_body, tn);
                text_acc.len = 0;
            }
            Str expr = str_trim_js(str_make(p + start_search, (uint32_t)close - start_search));
            TNode vn; xmemset(&vn, 0, sizeof(vn));
            vn.kind = raw ? TNODE_RAW : TNODE_VAR;
            if (!tpl_parse_path(a, tname, expr, &vn.path, &vn.path_len, err)) return 0;
            tlist_push(a, cur_body, vn);
            i = (uint32_t)close + (raw ? 3 : 2);
            continue;
        }

        // {% ... %}
        int32_t close = -1;
        for (uint32_t k = open + 2; k + 1 < len; k++) {
            if (p[k] == '%' && p[k + 1] == '}') { close = (int32_t)k; break; }
        }
        if (close < 0) {
            *err = tpl_err_simple(a, tname, "a {% is not closed", str_make("", 0), NULL);
            return 0;
        }
        uint32_t end = (uint32_t)close + 2;
        uint32_t line_start = open;
        while (line_start > 0 && p[line_start - 1] != '\n') line_start--;
        int32_t nl = md_indexof(p, len, end, '\n');
        uint32_t line_end = (nl < 0) ? len : (uint32_t)nl;
        int standalone = 1;
        for (uint32_t k = line_start; k < open; k++) {
            if (p[k] != ' ' && p[k] != '\t') { standalone = 0; break; }
        }
        if (standalone) {
            for (uint32_t k = end; k < line_end; k++) {
                if (p[k] != ' ' && p[k] != '\t') { standalone = 0; break; }
            }
        }
        if (standalone) {
            uint32_t strip = open - line_start;
            if (text_acc.len >= strip) text_acc.len -= strip;
            else text_acc.len = 0;
        }

        TNodeList *cur_body = stack[sp].in_else ? &stack[sp].node.else_body : &stack[sp].node.body;
        if (text_acc.len > 0) {
            TNode tn; xmemset(&tn, 0, sizeof(tn));
            tn.kind = TNODE_TEXT;
            tn.text = arena_dup(a, text_acc.buf, text_acc.len);
            tlist_push(a, cur_body, tn);
            text_acc.len = 0;
        }

        Str tag = str_trim_js(str_make(p + open + 2, (uint32_t)close - (open + 2)));
        i = standalone ? ((nl < 0) ? len : (uint32_t)nl + 1) : end;

        // Parse tag grammar:
        // FOR: /^for ([A-Za-z_][A-Za-z0-9_]*) in (\S+)$/
        // IF: /^if (not )?(\S+)$/
        // else
        // endfor / endif
        // INCLUDE: /^include "([a-z0-9-]+)"$/
        if (str_starts(tag, "for ", 4)) {
            uint32_t k = 4;
            if (k < tag.len && is_ident_start(tag.p[k])) {
                uint32_t vs = k++;
                while (k < tag.len && is_ident_cont(tag.p[k])) k++;
                uint32_t ve = k;
                if (k + 4 <= tag.len && xmemeq(tag.p + k, " in ", 4)) {
                    Str pstr = str_make(tag.p + k + 4, tag.len - (k + 4));
                    if (pstr.len > 0 && !has_js_ws(pstr)) {
                        if (sp + 1 >= 64) { *err = tpl_err_simple(a, tname, "nesting too deep", str_make("", 0), NULL); return 0; }
                        sp++;
                        xmemset(&stack[sp], 0, sizeof(StackItem));
                        stack[sp].node.kind = TNODE_FOR;
                        stack[sp].node.text = str_make(tag.p + vs, ve - vs);
                        if (!tpl_parse_path(a, tname, pstr, &stack[sp].node.path, &stack[sp].node.path_len, err)) return 0;
                        continue;
                    }
                }
            }
            *err = tpl_err_simple(a, tname, "{% ", tag, " %} is not a tag");
            return 0;
        } else if (str_starts(tag, "if ", 3)) {
            uint32_t k = 3;
            int is_not = 0;
            if (tag.len >= 7 && xmemeq(tag.p + 3, "not ", 4)) { is_not = 1; k = 7; }
            Str pstr = str_make(tag.p + k, tag.len - k);
            if (pstr.len > 0 && !has_js_ws(pstr)) {
                if (sp + 1 >= 64) { *err = tpl_err_simple(a, tname, "nesting too deep", str_make("", 0), NULL); return 0; }
                sp++;
                xmemset(&stack[sp], 0, sizeof(StackItem));
                stack[sp].node.kind = TNODE_IF;
                stack[sp].node.is_not = (uint8_t)is_not;
                if (!tpl_parse_path(a, tname, pstr, &stack[sp].node.path, &stack[sp].node.path_len, err)) return 0;
                continue;
            }
            *err = tpl_err_simple(a, tname, "{% ", tag, " %} is not a tag");
            return 0;
        } else if (str_eq_lit(tag, "else", 4)) {
            if (sp == 0 || stack[sp].node.kind != TNODE_IF || stack[sp].node.has_else) {
                *err = tpl_err_simple(a, tname, "an else outside an if", str_make("", 0), NULL);
                return 0;
            }
            stack[sp].node.has_else = 1;
            stack[sp].in_else = 1;
            continue;
        } else if (str_eq_lit(tag, "endfor", 6)) {
            if (sp == 0 || stack[sp].node.kind != TNODE_FOR) {
                *err = tpl_err_simple(a, tname, "an endfor with no open for", str_make("", 0), NULL);
                return 0;
            }
            TNode done = stack[sp--].node;
            TNodeList *pbody = stack[sp].in_else ? &stack[sp].node.else_body : &stack[sp].node.body;
            tlist_push(a, pbody, done);
            continue;
        } else if (str_eq_lit(tag, "endif", 5)) {
            if (sp == 0 || stack[sp].node.kind != TNODE_IF) {
                *err = tpl_err_simple(a, tname, "an endif with no open if", str_make("", 0), NULL);
                return 0;
            }
            TNode done = stack[sp--].node;
            TNodeList *pbody = stack[sp].in_else ? &stack[sp].node.else_body : &stack[sp].node.body;
            tlist_push(a, pbody, done);
            continue;
        } else if (str_starts(tag, "include \"", 9) && tag.len >= 11 && tag.p[tag.len - 1] == '"') {
            Str iname = str_make(tag.p + 9, tag.len - 10);
            if (is_valid_name_fm(iname)) {
                TNode inc; xmemset(&inc, 0, sizeof(inc));
                inc.kind = TNODE_INCLUDE;
                inc.text = iname;
                tlist_push(a, cur_body, inc);
                continue;
            }
            *err = tpl_err_simple(a, tname, "{% ", tag, " %} is not a tag");
            return 0;
        } else {
            *err = tpl_err_simple(a, tname, "{% ", tag, " %} is not a tag");
            return 0;
        }
    }

    if (text_acc.len > 0) {
        TNodeList *cur_body = stack[sp].in_else ? &stack[sp].node.else_body : &stack[sp].node.body;
        TNode tn; xmemset(&tn, 0, sizeof(tn));
        tn.kind = TNODE_TEXT;
        tn.text = arena_dup(a, text_acc.buf, text_acc.len);
        tlist_push(a, cur_body, tn);
    }

    if (sp > 0) {
        *err = tpl_err_simple(a, tname, stack[sp].node.kind == TNODE_FOR ? "a for is not closed" : "an if is not closed", str_make("", 0), NULL);
        // Wait: check template.js line 111: `template "${name}": a ${stack[stack.length - 1].kind} is not closed` -> "a if is not closed"!!
        *err = tpl_err_simple(a, tname, stack[sp].node.kind == TNODE_FOR ? "a for is not closed" : "a if is not closed", str_make("", 0), NULL);
        return 0;
    }

    *out_root = stack[0].node.body;
    return 1;
}

// ---- Runtime Typed Value & Scope Evaluator ----
enum {
    VAL_UNDEF = 0,
    VAL_NULL,
    VAL_BOOL,
    VAL_INT,
    VAL_STR,
    VAL_STR_JSON,     // already JSON-escaped string (for pre-escaped markdown html/excerpt/toc.html)
    VAL_SITE,
    VAL_PAGE,
    VAL_CARD,
    VAL_TAG_SUMMARY,
    VAL_TAG_FULL,
    VAL_TAG_REF,
    VAL_LINK,
    VAL_TOC_ITEM,
    VAL_PAGER,
    VAL_LOOP,
    VAL_ARR_TAGS,     // TagObj* array (as TAG_SUMMARY)
    VAL_ARR_CARDS,    // SourceObj** array (as CARD)
    VAL_ARR_TAG_REFS, // TagObj** array (as TAG_REF)
    VAL_ARR_TOC,      // TocItem* array (as TOC_ITEM)
};

// Direct integer opcodes for zero-string-compare template rendering
enum {
    FOP_GENERIC = 0,
    // Root: page.*
    FOP_PAGE_OBJ,
    FOP_PAGE_TITLE,
    FOP_PAGE_URL,
    FOP_PAGE_DATE,
    FOP_PAGE_DATE_LONG,
    FOP_PAGE_SUMMARY,
    FOP_PAGE_EXCERPT,
    FOP_PAGE_READING_MIN,
    FOP_PAGE_SLUG,
    FOP_PAGE_CONTENT,
    FOP_PAGE_TAGS,
    FOP_PAGE_TOC,
    FOP_PAGE_NEWER,
    FOP_PAGE_OLDER,
    FOP_PAGE_NEWER_URL,
    FOP_PAGE_NEWER_TITLE,
    FOP_PAGE_OLDER_URL,
    FOP_PAGE_OLDER_TITLE,
    FOP_PAGE_IS_POST,
    // Root: posts, pager.*, tag.*
    FOP_POSTS,
    FOP_PAGER_NUMBER,
    FOP_PAGER_TOTAL,
    FOP_PAGER_URL,
    FOP_PAGER_NEWER_URL,
    FOP_PAGER_OLDER_URL,
    FOP_TAG_OBJ,
    FOP_TAG_NAME,
    FOP_TAG_SLUG,
    FOP_TAG_URL,
    FOP_TAG_COUNT,
    FOP_TAG_POSTS,
    // Loop var: CARD (p.*)
    FOP_LV_CARD_TITLE,
    FOP_LV_CARD_URL,
    FOP_LV_CARD_DATE,
    FOP_LV_CARD_DATE_LONG,
    FOP_LV_CARD_SUMMARY,
    FOP_LV_CARD_EXCERPT,
    FOP_LV_CARD_READING_MIN,
    FOP_LV_CARD_TAGS,
    // Loop var: TAG_REF / TAG_SUMMARY (t.*)
    FOP_LV_TAG_NAME,
    FOP_LV_TAG_SLUG,
    FOP_LV_TAG_URL,
    FOP_LV_TAG_COUNT,
    // Loop var: TOC_ITEM (h.*)
    FOP_LV_TOC_LEVEL,
    FOP_LV_TOC_ID,
    FOP_LV_TOC_HTML,
    // Loop metadata (loop.*)
    FOP_LOOP_INDEX,
    FOP_LOOP_FIRST,
    FOP_LOOP_LAST,
    FOP_LOOP_LENGTH,
};

typedef struct {
    uint8_t kind;
    uint8_t b;
    int32_t i;
    uint32_t arr_len;
    Str s;
    const void *ptr;
} Val;

typedef struct Scope Scope;
struct Scope {
    const Scope *parent;
    // If parent == NULL: root scope (uses RenderCtx)
    // If parent != NULL: for-loop scope (var_name -> var_val, "loop" -> loop_obj)
    Str var_name;
    Val var_val;
    LoopObj loop_obj;
};

typedef struct {
    const SiteObj *site;
    const TagObj *all_tags;
    uint32_t n_all_tags;
    SourceObj *const *recent;
    uint32_t n_recent;
    // Dynamic per-render root vars (NULL if absent in this render)
    const SourceObj *page;
    SourceObj *const *posts;
    uint32_t n_posts;
    uint8_t has_posts;
    const PagerObj *pager;
    const TagObj *tag;
    // Parsed templates
    const StrMap *tpl_map;
    const TNodeList *tpl_list;
    Arena *arena;
    uint8_t md_is_json; // 1 = page.html, excerpt, toc[].html are already JSON-escaped!
} RenderCtx;

static inline Val val_undef(void) { Val v; xmemset(&v, 0, sizeof(v)); return v; }
static inline Val val_null(void) { Val v; xmemset(&v, 0, sizeof(v)); v.kind = VAL_NULL; return v; }
static inline Val val_bool(int b) { Val v; xmemset(&v, 0, sizeof(v)); v.kind = VAL_BOOL; v.b = (uint8_t)(b ? 1 : 0); return v; }
static inline Val val_int(int32_t i) { Val v; xmemset(&v, 0, sizeof(v)); v.kind = VAL_INT; v.i = i; return v; }
static inline Val val_str(Str s) { Val v; xmemset(&v, 0, sizeof(v)); v.kind = VAL_STR; v.s = s; return v; }
static inline Val val_str_md(Str s, uint8_t md_is_json) {
    Val v; xmemset(&v, 0, sizeof(v)); v.kind = md_is_json ? VAL_STR_JSON : VAL_STR; v.s = s; return v;
}

static int parse_arr_idx(Str k, uint32_t *out) {
    if (k.len == 0 || k.len > 9) return 0;
    if (k.len > 1 && k.p[0] == '0') return 0;
    uint32_t v = 0;
    for (uint32_t i = 0; i < k.len; i++) {
        if (k.p[i] < '0' || k.p[i] > '9') return 0;
        v = v * 10 + (uint32_t)(k.p[i] - '0');
    }
    *out = v;
    return 1;
}

static Val val_get_prop(Val v, Str k, uint8_t md_is_json) {
    switch (v.kind) {
        case VAL_SITE: {
            const SiteObj *s = (const SiteObj *)v.ptr;
            if (str_eq_lit(k, "title", 5)) return val_str(s->title);
            if (str_eq_lit(k, "base_url", 8)) return val_str(s->base_url);
            return val_undef();
        }
        case VAL_PAGE: {
            const SourceObj *p = (const SourceObj *)v.ptr;
            if (str_eq_lit(k, "title", 5)) return val_str(p->title);
            if (str_eq_lit(k, "url", 3)) return val_str(p->url);
            if (str_eq_lit(k, "date", 4)) return val_str(p->date);
            if (str_eq_lit(k, "date_long", 9)) return val_str(p->date_long);
            if (str_eq_lit(k, "summary", 7)) return val_str(p->summary);
            if (str_eq_lit(k, "excerpt", 7)) return val_str_md(p->excerpt, md_is_json);
            if (str_eq_lit(k, "reading_minutes", 15)) return val_int(p->reading_minutes);
            if (str_eq_lit(k, "tags", 4)) {
                Val r = val_undef(); r.kind = VAL_ARR_TAG_REFS; r.ptr = p->tag_refs; r.arr_len = p->n_tag_refs; return r;
            }
            if (str_eq_lit(k, "slug", 4)) return val_str(p->slug);
            if (str_eq_lit(k, "content", 7)) return val_str_md(p->html, md_is_json);
            if (str_eq_lit(k, "toc", 3)) {
                Val r = val_undef(); r.kind = VAL_ARR_TOC; r.ptr = p->toc; r.arr_len = p->n_toc; return r;
            }
            if (str_eq_lit(k, "newer", 5)) {
                if (!p->newer) return val_null();
                Val r = val_undef(); r.kind = VAL_LINK; r.ptr = p->newer; return r;
            }
            if (str_eq_lit(k, "older", 5)) {
                if (!p->older) return val_null();
                Val r = val_undef(); r.kind = VAL_LINK; r.ptr = p->older; return r;
            }
            if (str_eq_lit(k, "is_post", 7)) return val_bool(p->is_post);
            return val_undef();
        }
        case VAL_CARD: {
            const SourceObj *p = (const SourceObj *)v.ptr;
            if (str_eq_lit(k, "title", 5)) return val_str(p->title);
            if (str_eq_lit(k, "url", 3)) return val_str(p->url);
            if (str_eq_lit(k, "date", 4)) return val_str(p->date);
            if (str_eq_lit(k, "date_long", 9)) return val_str(p->date_long);
            if (str_eq_lit(k, "summary", 7)) return val_str(p->summary);
            if (str_eq_lit(k, "excerpt", 7)) return val_str_md(p->excerpt, md_is_json);
            if (str_eq_lit(k, "reading_minutes", 15)) return val_int(p->reading_minutes);
            if (str_eq_lit(k, "tags", 4)) {
                Val r = val_undef(); r.kind = VAL_ARR_TAG_REFS; r.ptr = p->tag_refs; r.arr_len = p->n_tag_refs; return r;
            }
            return val_undef();
        }
        case VAL_TAG_FULL: {
            const TagObj *t = (const TagObj *)v.ptr;
            if (str_eq_lit(k, "name", 4)) return val_str(t->name);
            if (str_eq_lit(k, "slug", 4)) return val_str(t->slug);
            if (str_eq_lit(k, "url", 3)) return val_str(t->url);
            if (str_eq_lit(k, "count", 5)) return val_int((int32_t)t->n_posts);
            if (str_eq_lit(k, "posts", 5)) {
                Val r = val_undef(); r.kind = VAL_ARR_CARDS; r.ptr = t->posts; r.arr_len = t->n_posts; return r;
            }
            return val_undef();
        }
        case VAL_TAG_SUMMARY: {
            const TagObj *t = (const TagObj *)v.ptr;
            if (str_eq_lit(k, "name", 4)) return val_str(t->name);
            if (str_eq_lit(k, "slug", 4)) return val_str(t->slug);
            if (str_eq_lit(k, "url", 3)) return val_str(t->url);
            if (str_eq_lit(k, "count", 5)) return val_int((int32_t)t->n_posts);
            return val_undef();
        }
        case VAL_TAG_REF: {
            const TagObj *t = (const TagObj *)v.ptr;
            if (str_eq_lit(k, "name", 4)) return val_str(t->name);
            if (str_eq_lit(k, "slug", 4)) return val_str(t->slug);
            if (str_eq_lit(k, "url", 3)) return val_str(t->url);
            return val_undef();
        }
        case VAL_LINK: {
            const SourceObj *p = (const SourceObj *)v.ptr;
            if (str_eq_lit(k, "title", 5)) return val_str(p->title);
            if (str_eq_lit(k, "url", 3)) return val_str(p->url);
            if (str_eq_lit(k, "date", 4)) return val_str(p->date);
            if (str_eq_lit(k, "date_long", 9)) return val_str(p->date_long);
            return val_undef();
        }
        case VAL_TOC_ITEM: {
            const TocItem *h = (const TocItem *)v.ptr;
            if (str_eq_lit(k, "level", 5)) return val_int(h->level);
            if (str_eq_lit(k, "id", 2)) return val_str(h->id);
            if (str_eq_lit(k, "html", 4)) return val_str_md(h->html, md_is_json);
            return val_undef();
        }
        case VAL_PAGER: {
            const PagerObj *pg = (const PagerObj *)v.ptr;
            if (str_eq_lit(k, "number", 6)) return val_int(pg->number);
            if (str_eq_lit(k, "total", 5)) return val_int(pg->total);
            if (str_eq_lit(k, "url", 3)) return val_str(pg->url);
            if (str_eq_lit(k, "newer_url", 9)) return val_str(pg->newer_url);
            if (str_eq_lit(k, "older_url", 9)) return val_str(pg->older_url);
            return val_undef();
        }
        case VAL_LOOP: {
            const LoopObj *lp = (const LoopObj *)v.ptr;
            if (str_eq_lit(k, "index", 5)) return val_int(lp->index);
            if (str_eq_lit(k, "first", 5)) return val_bool(lp->first);
            if (str_eq_lit(k, "last", 4)) return val_bool(lp->last);
            if (str_eq_lit(k, "length", 6)) return val_int(lp->length);
            return val_undef();
        }
        case VAL_ARR_TAGS:
        case VAL_ARR_CARDS:
        case VAL_ARR_TAG_REFS:
        case VAL_ARR_TOC: {
            if (str_eq_lit(k, "length", 6)) return val_int((int32_t)v.arr_len);
            uint32_t idx;
            if (parse_arr_idx(k, &idx) && idx < v.arr_len) {
                Val r = val_undef();
                if (v.kind == VAL_ARR_TAGS) { r.kind = VAL_TAG_SUMMARY; r.ptr = &((const TagObj *)v.ptr)[idx]; }
                else if (v.kind == VAL_ARR_CARDS) { r.kind = VAL_CARD; r.ptr = ((SourceObj *const *)v.ptr)[idx]; }
                else if (v.kind == VAL_ARR_TAG_REFS) { r.kind = VAL_TAG_REF; r.ptr = ((TagObj *const *)v.ptr)[idx]; }
                else { r.kind = VAL_TOC_ITEM; r.ptr = &((const TocItem *)v.ptr)[idx]; }
                return r;
            }
            return val_undef();
        }
        default:
            return val_undef();
    }
}

static Val scope_lookup(const RenderCtx *ctx, const Scope *sc, const Str *path, uint32_t plen) {
    Str p0 = path[0];
    Val v = val_undef();
    int found = 0;
    const Scope *cur = sc;
    while (cur && cur->parent != NULL) {
        if (str_eq_lit(p0, "loop", 4)) {
            v.kind = VAL_LOOP; v.ptr = &cur->loop_obj; found = 1; break;
        }
        if (str_eq(p0, cur->var_name)) {
            v = cur->var_val; found = 1; break;
        }
        cur = cur->parent;
    }
    if (!found) {
        if (str_eq_lit(p0, "site", 4)) { v.kind = VAL_SITE; v.ptr = ctx->site; }
        else if (str_eq_lit(p0, "tags", 4)) { v.kind = VAL_ARR_TAGS; v.ptr = ctx->all_tags; v.arr_len = ctx->n_all_tags; }
        else if (str_eq_lit(p0, "recent", 6)) { v.kind = VAL_ARR_CARDS; v.ptr = ctx->recent; v.arr_len = ctx->n_recent; }
        else if (ctx->page && str_eq_lit(p0, "page", 4)) { v.kind = VAL_PAGE; v.ptr = ctx->page; }
        else if (ctx->has_posts && str_eq_lit(p0, "posts", 5)) { v.kind = VAL_ARR_CARDS; v.ptr = ctx->posts; v.arr_len = ctx->n_posts; }
        else if (ctx->pager && str_eq_lit(p0, "pager", 5)) { v.kind = VAL_PAGER; v.ptr = ctx->pager; }
        else if (ctx->tag && str_eq_lit(p0, "tag", 3)) { v.kind = VAL_TAG_FULL; v.ptr = ctx->tag; }
        else return val_undef();
    }
    for (uint32_t k = 1; k < plen; k++) {
        v = val_get_prop(v, path[k], ctx->md_is_json);
        if (v.kind == VAL_UNDEF) return v;
    }
    return v;
}

static inline Val fast_eval(const RenderCtx *ctx, const Scope *sc, const TNode *n) {
    uint8_t op = n->fast_op;
    if (__builtin_expect(op == FOP_GENERIC, 0)) return scope_lookup(ctx, sc, n->path, n->path_len);
    const Scope *cur = sc;
    uint8_t d = n->fast_depth;
    while (d--) cur = cur->parent;
    switch (op) {
        case FOP_PAGE_OBJ: { Val r = val_undef(); r.kind = VAL_PAGE; r.ptr = ctx->page; return r; }
        case FOP_PAGE_TITLE: return val_str(ctx->page->title);
        case FOP_PAGE_URL: return val_str(ctx->page->url);
        case FOP_PAGE_DATE: return val_str_md(ctx->page->date, 1);
        case FOP_PAGE_DATE_LONG: return val_str_md(ctx->page->date_long, 1);
        case FOP_PAGE_SUMMARY: return val_str(ctx->page->summary);
        case FOP_PAGE_EXCERPT: return val_str_md(ctx->page->excerpt, ctx->md_is_json);
        case FOP_PAGE_READING_MIN: return val_int(ctx->page->reading_minutes);
        case FOP_PAGE_SLUG: return val_str_md(ctx->page->slug, 1);
        case FOP_PAGE_CONTENT: return val_str_md(ctx->page->html, ctx->md_is_json);
        case FOP_PAGE_TAGS: { Val r = val_undef(); r.kind = VAL_ARR_TAG_REFS; r.ptr = ctx->page->tag_refs; r.arr_len = ctx->page->n_tag_refs; return r; }
        case FOP_PAGE_TOC: { Val r = val_undef(); r.kind = VAL_ARR_TOC; r.ptr = ctx->page->toc; r.arr_len = ctx->page->n_toc; return r; }
        case FOP_PAGE_NEWER: if (!ctx->page->newer) return val_null(); { Val r = val_undef(); r.kind = VAL_LINK; r.ptr = ctx->page->newer; return r; }
        case FOP_PAGE_OLDER: if (!ctx->page->older) return val_null(); { Val r = val_undef(); r.kind = VAL_LINK; r.ptr = ctx->page->older; return r; }
        case FOP_PAGE_NEWER_URL: return ctx->page->newer ? val_str(ctx->page->newer->url) : val_undef();
        case FOP_PAGE_NEWER_TITLE: return ctx->page->newer ? val_str(ctx->page->newer->title) : val_undef();
        case FOP_PAGE_OLDER_URL: return ctx->page->older ? val_str(ctx->page->older->url) : val_undef();
        case FOP_PAGE_OLDER_TITLE: return ctx->page->older ? val_str(ctx->page->older->title) : val_undef();
        case FOP_PAGE_IS_POST: return val_bool(ctx->page->is_post);
        case FOP_POSTS: { Val r = val_undef(); r.kind = VAL_ARR_CARDS; r.ptr = ctx->posts; r.arr_len = ctx->n_posts; return r; }
        case FOP_PAGER_NUMBER: return val_int(ctx->pager->number);
        case FOP_PAGER_TOTAL: return val_int(ctx->pager->total);
        case FOP_PAGER_URL: return val_str(ctx->pager->url);
        case FOP_PAGER_NEWER_URL: return val_str(ctx->pager->newer_url);
        case FOP_PAGER_OLDER_URL: return val_str(ctx->pager->older_url);
        case FOP_TAG_OBJ: { Val r = val_undef(); r.kind = VAL_TAG_FULL; r.ptr = ctx->tag; return r; }
        case FOP_TAG_NAME: return val_str(ctx->tag->name);
        case FOP_TAG_SLUG: return val_str(ctx->tag->slug);
        case FOP_TAG_URL: return val_str(ctx->tag->url);
        case FOP_TAG_COUNT: return val_int((int32_t)ctx->tag->n_posts);
        case FOP_TAG_POSTS: { Val r = val_undef(); r.kind = VAL_ARR_CARDS; r.ptr = ctx->tag->posts; r.arr_len = ctx->tag->n_posts; return r; }
        case FOP_LV_CARD_TITLE: return val_str(((const SourceObj *)cur->var_val.ptr)->title);
        case FOP_LV_CARD_URL: return val_str(((const SourceObj *)cur->var_val.ptr)->url);
        case FOP_LV_CARD_DATE: return val_str_md(((const SourceObj *)cur->var_val.ptr)->date, 1);
        case FOP_LV_CARD_DATE_LONG: return val_str_md(((const SourceObj *)cur->var_val.ptr)->date_long, 1);
        case FOP_LV_CARD_SUMMARY: return val_str(((const SourceObj *)cur->var_val.ptr)->summary);
        case FOP_LV_CARD_EXCERPT: return val_str_md(((const SourceObj *)cur->var_val.ptr)->excerpt, ctx->md_is_json);
        case FOP_LV_CARD_READING_MIN: return val_int(((const SourceObj *)cur->var_val.ptr)->reading_minutes);
        case FOP_LV_CARD_TAGS: {
            const SourceObj *p = (const SourceObj *)cur->var_val.ptr;
            Val r = val_undef(); r.kind = VAL_ARR_TAG_REFS; r.ptr = p->tag_refs; r.arr_len = p->n_tag_refs; return r;
        }
        case FOP_LV_TAG_NAME: return val_str(((const TagObj *)cur->var_val.ptr)->name);
        case FOP_LV_TAG_SLUG: return val_str_md(((const TagObj *)cur->var_val.ptr)->slug, 1);
        case FOP_LV_TAG_URL: return val_str(((const TagObj *)cur->var_val.ptr)->url);
        case FOP_LV_TAG_COUNT: return val_int((int32_t)((const TagObj *)cur->var_val.ptr)->n_posts);
        case FOP_LV_TOC_LEVEL: return val_int(((const TocItem *)cur->var_val.ptr)->level);
        case FOP_LV_TOC_ID: return val_str_md(((const TocItem *)cur->var_val.ptr)->id, 1);
        case FOP_LV_TOC_HTML: return val_str_md(((const TocItem *)cur->var_val.ptr)->html, ctx->md_is_json);
        case FOP_LOOP_INDEX: return val_int(cur->loop_obj.index);
        case FOP_LOOP_FIRST: return val_bool(cur->loop_obj.first);
        case FOP_LOOP_LAST: return val_bool(cur->loop_obj.last);
        case FOP_LOOP_LENGTH: return val_int(cur->loop_obj.length);
        default: return scope_lookup(ctx, sc, n->path, n->path_len);
    }
}

static inline int val_truthy(Val v) {
    switch (v.kind) {
        case VAL_UNDEF:
        case VAL_NULL: return 0;
        case VAL_BOOL: return v.b != 0;
        case VAL_INT: return v.i != 0;
        case VAL_STR:
        case VAL_STR_JSON: return v.s.len > 0;
        case VAL_ARR_TAGS:
        case VAL_ARR_CARDS:
        case VAL_ARR_TAG_REFS:
        case VAL_ARR_TOC: return v.arr_len > 0;
        default: return 1;
    }
}

static inline void emit_val_out(OutBuf *o, Val v, int html_esc) {
    switch (v.kind) {
        case VAL_STR_JSON:
            out_raw(o, v.s.p, v.s.len);
            break;
        case VAL_STR:
            if (html_esc) out_json_html_body(o, v.s.p, v.s.len);
            else out_json_body(o, v.s.p, v.s.len);
            break;
        case VAL_INT: {
            int32_t x = v.i;
            if (x < 0) { out_putc(o, '-'); out_putu32(o, (uint32_t)(-(int64_t)x)); }
            else out_putu32(o, (uint32_t)x);
            break;
        }
        case VAL_BOOL:
            if (v.b) out_raw(o, "true", 4);
            else out_raw(o, "false", 5);
            break;
        case VAL_ARR_TAGS:
        case VAL_ARR_CARDS:
        case VAL_ARR_TAG_REFS:
        case VAL_ARR_TOC:
            for (uint32_t k = 1; k < v.arr_len; k++) out_raw(o, ", ", 2);
            break;
        default: break;
    }
}

static inline void emit_val_sb(SB *sb, Val v, int html_esc) {
    switch (v.kind) {
        case VAL_STR_JSON:
            sb_puts(sb, v.s.p, v.s.len);
            break;
        case VAL_STR:
            if (html_esc) sb_put_json_html_body(sb, v.s.p, v.s.len);
            else sb_put_json_body(sb, v.s.p, v.s.len);
            break;
        case VAL_INT: {
            int32_t x = v.i;
            if (x < 0) { sb_putc(sb, '-'); sb_putu32(sb, (uint32_t)(-(int64_t)x)); }
            else sb_putu32(sb, (uint32_t)x);
            break;
        }
        case VAL_BOOL:
            if (v.b) sb_puts(sb, "true", 4);
            else sb_puts(sb, "false", 5);
            break;
        case VAL_ARR_TAGS:
        case VAL_ARR_CARDS:
        case VAL_ARR_TAG_REFS:
        case VAL_ARR_TOC:
            for (uint32_t k = 1; k < v.arr_len; k++) sb_puts(sb, ", ", 2);
            break;
        default: break;
    }
}

// Render compiled/raw TNodeList into SB (pre-JSON-escaped bytes)
static void render_nodes_sb(SB *sb, const TNodeList *list, const Scope *sc, const RenderCtx *ctx, int depth) {
    for (uint32_t i = 0; i < list->len; i++) {
        const TNode *n = &list->items[i];
        switch (n->kind) {
            case TNODE_TEXT:
                if (n->is_json_lit) sb_puts(sb, n->text.p, n->text.len);
                else sb_put_json_body(sb, n->text.p, n->text.len);
                break;
            case TNODE_VAR:
                emit_val_sb(sb, fast_eval(ctx, sc, n), 1);
                break;
            case TNODE_RAW:
                emit_val_sb(sb, fast_eval(ctx, sc, n), 0);
                break;
            case TNODE_IF: {
                int tv = val_truthy(fast_eval(ctx, sc, n));
                if (tv != (int)n->is_not) render_nodes_sb(sb, &n->body, sc, ctx, depth);
                else if (n->has_else) render_nodes_sb(sb, &n->else_body, sc, ctx, depth);
                break;
            }
            case TNODE_FOR: {
                Val xs = fast_eval(ctx, sc, n);
                if (xs.kind < VAL_ARR_TAGS) break;
                Scope child;
                child.parent = sc;
                child.var_name = n->text;
                child.loop_obj.length = (int32_t)xs.arr_len;
                for (uint32_t k = 0; k < xs.arr_len; k++) {
                    child.loop_obj.index = (int32_t)(k + 1);
                    child.loop_obj.first = (k == 0);
                    child.loop_obj.last = (k + 1 == xs.arr_len);
                    Val item = val_undef();
                    if (xs.kind == VAL_ARR_TAGS) { item.kind = VAL_TAG_SUMMARY; item.ptr = &((const TagObj *)xs.ptr)[k]; }
                    else if (xs.kind == VAL_ARR_CARDS) { item.kind = VAL_CARD; item.ptr = ((SourceObj *const *)xs.ptr)[k]; }
                    else if (xs.kind == VAL_ARR_TAG_REFS) { item.kind = VAL_TAG_REF; item.ptr = ((TagObj *const *)xs.ptr)[k]; }
                    else { item.kind = VAL_TOC_ITEM; item.ptr = &((const TocItem *)xs.ptr)[k]; }
                    child.var_val = item;
                    render_nodes_sb(sb, &n->body, &child, ctx, depth);
                }
                break;
            }
            case TNODE_INCLUDE: {
                if (depth < 8) {
                    int32_t tid = smap_get(ctx->tpl_map, n->text);
                    if (tid >= 0) render_nodes_sb(sb, &ctx->tpl_list[tid], sc, ctx, depth + 1);
                }
                break;
            }
        }
    }
}

// Render compiled TNodeList directly into streaming OutBuf (with shared card cache!)
static void render_nodes_out(OutBuf *o, const TNodeList *list, const Scope *sc, const RenderCtx *ctx, int depth) {
    for (uint32_t i = 0; i < list->len; i++) {
        const TNode *n = &list->items[i];
        switch (n->kind) {
            case TNODE_TEXT:
                if (n->is_json_lit) out_raw(o, n->text.p, n->text.len);
                else out_json_body(o, n->text.p, n->text.len);
                break;
            case TNODE_VAR:
                emit_val_out(o, fast_eval(ctx, sc, n), 1);
                break;
            case TNODE_RAW:
                emit_val_out(o, fast_eval(ctx, sc, n), 0);
                break;
            case TNODE_IF: {
                int tv = val_truthy(fast_eval(ctx, sc, n));
                if (tv != (int)n->is_not) render_nodes_out(o, &n->body, sc, ctx, depth);
                else if (n->has_else) render_nodes_out(o, &n->else_body, sc, ctx, depth);
                break;
            }
            case TNODE_FOR: {
                Val xs = fast_eval(ctx, sc, n);
                if (xs.kind < VAL_ARR_TAGS) break;
                if (n->cacheable_card_loop && xs.kind == VAL_ARR_CARDS) {
                    SourceObj *const *arr = (SourceObj *const *)xs.ptr;
                    uint64_t ckey = n->body_hash;
                    Scope child;
                    child.parent = sc;
                    child.var_name = n->text;
                    child.loop_obj.length = (int32_t)xs.arr_len;
                    for (uint32_t k = 0; k < xs.arr_len; k++) {
                        SourceObj *post = arr[k];
                        if (post->card_cache_key == ckey) {
                            out_raw(o, post->card_cache.p, post->card_cache.len);
                        } else {
                            SB csb; sb_init(&csb, ctx->arena, 512);
                            child.loop_obj.index = (int32_t)(k + 1);
                            child.loop_obj.first = (k == 0);
                            child.loop_obj.last = (k + 1 == xs.arr_len);
                            Val item = val_undef();
                            item.kind = VAL_CARD; item.ptr = post;
                            child.var_val = item;
                            render_nodes_sb(&csb, &n->body, &child, ctx, depth);
                            Str rendered = sb_finish(&csb);
                            post->card_cache = rendered;
                            post->card_cache_key = ckey;
                            out_raw(o, rendered.p, rendered.len);
                        }
                    }
                    break;
                }
                Scope child;
                child.parent = sc;
                child.var_name = n->text;
                child.loop_obj.length = (int32_t)xs.arr_len;
                for (uint32_t k = 0; k < xs.arr_len; k++) {
                    child.loop_obj.index = (int32_t)(k + 1);
                    child.loop_obj.first = (k == 0);
                    child.loop_obj.last = (k + 1 == xs.arr_len);
                    Val item = val_undef();
                    if (xs.kind == VAL_ARR_TAGS) { item.kind = VAL_TAG_SUMMARY; item.ptr = &((const TagObj *)xs.ptr)[k]; }
                    else if (xs.kind == VAL_ARR_CARDS) { item.kind = VAL_CARD; item.ptr = ((SourceObj *const *)xs.ptr)[k]; }
                    else if (xs.kind == VAL_ARR_TAG_REFS) { item.kind = VAL_TAG_REF; item.ptr = ((TagObj *const *)xs.ptr)[k]; }
                    else { item.kind = VAL_TOC_ITEM; item.ptr = &((const TocItem *)xs.ptr)[k]; }
                    child.var_val = item;
                    render_nodes_out(o, &n->body, &child, ctx, depth);
                }
                break;
            }
            case TNODE_INCLUDE: {
                if (depth < 8) {
                    int32_t tid = smap_get(ctx->tpl_map, n->text);
                    if (tid >= 0) render_nodes_out(o, &ctx->tpl_list[tid], sc, ctx, depth + 1);
                }
                break;
            }
        }
    }
}

// ---- Template Specializer: Include Inlining + Site-Invariant Constant Folding + Literal Merging + Card Loop Detection ----
enum {
    ROOT_MASK_PAGE  = 1 << 0,
    ROOT_MASK_POSTS = 1 << 1,
    ROOT_MASK_PAGER = 1 << 2,
    ROOT_MASK_TAG   = 1 << 3,
};

// Check if root variable `p0` is dynamic (not known at site-compile time) given `dyn_mask` and active local bound vars
static int is_var_dynamic(Str p0, uint32_t dyn_mask, const Str *locals, int n_locals) {
    if (n_locals > 0 && str_eq_lit(p0, "loop", 4)) return 1;
    for (int i = n_locals - 1; i >= 0; i--) {
        if (str_eq(p0, locals[i])) return 1;
    }
    if ((dyn_mask & ROOT_MASK_PAGE) && str_eq_lit(p0, "page", 4)) return 1;
    if ((dyn_mask & ROOT_MASK_POSTS) && str_eq_lit(p0, "posts", 5)) return 1;
    if ((dyn_mask & ROOT_MASK_PAGER) && str_eq_lit(p0, "pager", 5)) return 1;
    if ((dyn_mask & ROOT_MASK_TAG) && str_eq_lit(p0, "tag", 3)) return 1;
    return 0; // site, tags, recent, or guaranteed undefined!
}

// Check if a specialized subtree (already inlined!) depends ONLY on `allowed_var` (not loop, not any dynamic root var)
static int subtree_only_uses_var(const TNodeList *list, Str allowed_var, const Str *inner_locals, int n_inner) {
    for (uint32_t i = 0; i < list->len; i++) {
        const TNode *n = &list->items[i];
        if (n->kind == TNODE_TEXT) continue;
        if (n->kind == TNODE_INCLUDE) return 0; // shouldn't exist after inlining
        Str p0 = n->path[0];
        // Is p0 shadowed by an inner loop?
        int is_inner = 0;
        if (n_inner > 0 && str_eq_lit(p0, "loop", 4)) is_inner = 1;
        for (int j = 0; j < n_inner; j++) if (str_eq(p0, inner_locals[j])) is_inner = 1;
        if (!is_inner) {
            if (!str_eq(p0, allowed_var)) return 0; // uses `loop` or dynamic root var!
        }
        if (n->kind == TNODE_IF) {
            if (!subtree_only_uses_var(&n->body, allowed_var, inner_locals, n_inner)) return 0;
            if (n->has_else && !subtree_only_uses_var(&n->else_body, allowed_var, inner_locals, n_inner)) return 0;
        } else if (n->kind == TNODE_FOR) {
            if (n_inner >= 16) return 0;
            Str next_locals[16];
            for (int j = 0; j < n_inner; j++) next_locals[j] = inner_locals[j];
            next_locals[n_inner] = n->text;
            if (!subtree_only_uses_var(&n->body, allowed_var, next_locals, n_inner + 1)) return 0;
        }
    }
    return 1;
}

static uint64_t hash_tnodelist(const TNodeList *list, Str loop_var) {
    uint64_t h = 0x517cc1b727220a95ULL ^ list->len;
    for (uint32_t i = 0; i < list->len; i++) {
        const TNode *n = &list->items[i];
        h = (h * 0x100000001b3ULL) ^ ((uint64_t)n->kind << 8) ^ n->is_not ^ ((uint64_t)n->has_else << 4);
        if (n->kind == TNODE_TEXT) {
            h ^= hash_bytes(n->text.p, n->text.len);
        } else {
            if (n->kind == TNODE_FOR) {
                h ^= hash_bytes(n->text.p, n->text.len);
            }
            for (uint32_t k = 0; k < n->path_len; k++) {
                // Canonicalize loop_var to a fixed sentinel so `for p in posts` and `for q in tag.posts` even match if identical!
                if (k == 0 && str_eq(n->path[0], loop_var)) h = (h * 0x100000001b3ULL) ^ 0xdeadbeefcafebabeULL;
                else h = (h * 0x100000001b3ULL) ^ hash_bytes(n->path[k].p, n->path[k].len);
            }
            if (n->kind == TNODE_IF) {
                h ^= hash_tnodelist(&n->body, loop_var) * 0x9e3779b97f4a7c15ULL;
                if (n->has_else) h ^= hash_tnodelist(&n->else_body, loop_var) * 0xbf58476d1ce4e5b9ULL;
            } else if (n->kind == TNODE_FOR) {
                h ^= hash_tnodelist(&n->body, loop_var) * 0x94d049bb133111ebULL;
            }
        }
    }
    return h ? h : 1;
}

// Check if a raw/inlined AST subtree has ANY reference to dynamic vars (outside inner loops)
static int subtree_is_pure_const(const TNodeList *list, const RenderCtx *ctx, uint32_t dyn_mask,
                                 const Str *locals, int n_locals, int depth) {
    for (uint32_t i = 0; i < list->len; i++) {
        const TNode *n = &list->items[i];
        if (n->kind == TNODE_TEXT) continue;
        if (n->kind == TNODE_INCLUDE) {
            if (depth < 8) {
                int32_t tid = smap_get(ctx->tpl_map, n->text);
                if (tid >= 0 && !subtree_is_pure_const(&ctx->tpl_list[tid], ctx, dyn_mask, locals, n_locals, depth + 1))
                    return 0;
            }
            continue;
        }
        if (is_var_dynamic(n->path[0], dyn_mask, locals, n_locals)) return 0;
        if (n->kind == TNODE_IF) {
            if (!subtree_is_pure_const(&n->body, ctx, dyn_mask, locals, n_locals, depth)) return 0;
            if (n->has_else && !subtree_is_pure_const(&n->else_body, ctx, dyn_mask, locals, n_locals, depth)) return 0;
        } else if (n->kind == TNODE_FOR) {
            if (n_locals >= 16) return 0;
            Str next_locals[16];
            for (int j = 0; j < n_locals; j++) next_locals[j] = locals[j];
            next_locals[n_locals] = n->text;
            // Inside a pure-const FOR loop, the loop var IS bound by the loop itself, so check with local_var NOT dynamic!
            // Wait: we want to know if the loop body references any OUTER dynamic var!
            // Since `n->text` and `loop` are bound by THIS loop, they are local to the fold.
            // Pass a separate check: does body use any `dyn_mask` var NOT shadowed by `n->text` or `inner`?
            // Let's handle that cleanly: check if any path[0] hits outer locals or dyn_mask!
            // Easiest: only fold FOR loops when n_locals == 0 (top-level site-invariant loops like nav/tags/recent)!
            if (n_locals > 0) return 0;
            // Check body does not use dyn_mask vars (unless shadowed by n->text)
            // We'll use a helper:
            // Wait: if n_locals == 0, locals is empty, so we just check that body uses NO unshadowed dyn_mask vars!
            // Passing dyn_mask with the shadowed bit cleared if n->text == page/posts/pager/tag:
            uint32_t sub_mask = dyn_mask;
            if (str_eq_lit(n->text, "page", 4)) sub_mask &= ~(uint32_t)ROOT_MASK_PAGE;
            if (str_eq_lit(n->text, "posts", 5)) sub_mask &= ~(uint32_t)ROOT_MASK_POSTS;
            if (str_eq_lit(n->text, "pager", 5)) sub_mask &= ~(uint32_t)ROOT_MASK_PAGER;
            if (str_eq_lit(n->text, "tag", 3)) sub_mask &= ~(uint32_t)ROOT_MASK_TAG;
            if (!subtree_is_pure_const(&n->body, ctx, sub_mask, NULL, 0, depth)) return 0;
        }
    }
    return 1;
}

static void specialize_list(Arena *a, const TNodeList *in, TNodeList *out, const RenderCtx *ctx,
                            uint32_t dyn_mask, const Str *locals, int n_locals, int depth, SB *lit_acc) {
    Scope root_sc; xmemset(&root_sc, 0, sizeof(root_sc));

    for (uint32_t i = 0; i < in->len; i++) {
        const TNode *n = &in->items[i];
        switch (n->kind) {
            case TNODE_TEXT:
                if (n->is_json_lit) sb_puts(lit_acc, n->text.p, n->text.len);
                else sb_put_json_body(lit_acc, n->text.p, n->text.len);
                break;

            case TNODE_INCLUDE:
                if (depth < 8) {
                    int32_t tid = smap_get(ctx->tpl_map, n->text);
                    if (tid >= 0) {
                        specialize_list(a, &ctx->tpl_list[tid], out, ctx, dyn_mask, locals, n_locals, depth + 1, lit_acc);
                    }
                }
                break;

            case TNODE_VAR:
            case TNODE_RAW:
                if (!is_var_dynamic(n->path[0], dyn_mask, locals, n_locals)) {
                    Val v = scope_lookup(ctx, &root_sc, n->path, n->path_len);
                    emit_val_sb(lit_acc, v, n->kind == TNODE_VAR);
                } else {
                    if (lit_acc->len > 0) {
                        TNode tn; xmemset(&tn, 0, sizeof(tn));
                        tn.kind = TNODE_TEXT; tn.is_json_lit = 1;
                        tn.text = arena_dup(a, lit_acc->buf, lit_acc->len);
                        tlist_push(a, out, tn);
                        lit_acc->len = 0;
                    }
                    tlist_push(a, out, *n);
                }
                break;

            case TNODE_IF:
                if (!is_var_dynamic(n->path[0], dyn_mask, locals, n_locals)) {
                    int tv = val_truthy(scope_lookup(ctx, &root_sc, n->path, n->path_len));
                    if (tv != (int)n->is_not) {
                        specialize_list(a, &n->body, out, ctx, dyn_mask, locals, n_locals, depth, lit_acc);
                    } else if (n->has_else) {
                        specialize_list(a, &n->else_body, out, ctx, dyn_mask, locals, n_locals, depth, lit_acc);
                    }
                } else {
                    if (lit_acc->len > 0) {
                        TNode tn; xmemset(&tn, 0, sizeof(tn));
                        tn.kind = TNODE_TEXT; tn.is_json_lit = 1;
                        tn.text = arena_dup(a, lit_acc->buf, lit_acc->len);
                        tlist_push(a, out, tn);
                        lit_acc->len = 0;
                    }
                    TNode fn; xmemset(&fn, 0, sizeof(fn));
                    fn.kind = TNODE_IF;
                    fn.is_not = n->is_not;
                    fn.path = n->path;
                    fn.path_len = n->path_len;
                    fn.has_else = n->has_else;
                    SB sub_acc; sb_init(&sub_acc, a, 256);
                    specialize_list(a, &n->body, &fn.body, ctx, dyn_mask, locals, n_locals, depth, &sub_acc);
                    if (sub_acc.len > 0) {
                        TNode tn; xmemset(&tn, 0, sizeof(tn));
                        tn.kind = TNODE_TEXT; tn.is_json_lit = 1;
                        tn.text = arena_dup(a, sub_acc.buf, sub_acc.len);
                        tlist_push(a, &fn.body, tn);
                    }
                    if (n->has_else) {
                        sub_acc.len = 0;
                        specialize_list(a, &n->else_body, &fn.else_body, ctx, dyn_mask, locals, n_locals, depth, &sub_acc);
                        if (sub_acc.len > 0) {
                            TNode tn; xmemset(&tn, 0, sizeof(tn));
                            tn.kind = TNODE_TEXT; tn.is_json_lit = 1;
                            tn.text = arena_dup(a, sub_acc.buf, sub_acc.len);
                            tlist_push(a, &fn.else_body, tn);
                        }
                    }
                    tlist_push(a, out, fn);
                }
                break;

            case TNODE_FOR: {
                // Can we completely constant-fold this FOR loop?
                if (n_locals == 0 && !is_var_dynamic(n->path[0], dyn_mask, locals, 0)) {
                    uint32_t sub_mask = dyn_mask;
                    if (str_eq_lit(n->text, "page", 4)) sub_mask &= ~(uint32_t)ROOT_MASK_PAGE;
                    if (str_eq_lit(n->text, "posts", 5)) sub_mask &= ~(uint32_t)ROOT_MASK_POSTS;
                    if (str_eq_lit(n->text, "pager", 5)) sub_mask &= ~(uint32_t)ROOT_MASK_PAGER;
                    if (str_eq_lit(n->text, "tag", 3)) sub_mask &= ~(uint32_t)ROOT_MASK_TAG;
                    if (subtree_is_pure_const(&n->body, ctx, sub_mask, NULL, 0, depth)) {
                        // Evaluate the entire loop right now into lit_acc!
                        TNodeList single; single.items = (TNode *)n; single.len = 1; single.cap = 1;
                        render_nodes_sb(lit_acc, &single, &root_sc, ctx, depth);
                        break;
                    }
                }
                if (lit_acc->len > 0) {
                    TNode tn; xmemset(&tn, 0, sizeof(tn));
                    tn.kind = TNODE_TEXT; tn.is_json_lit = 1;
                    tn.text = arena_dup(a, lit_acc->buf, lit_acc->len);
                    tlist_push(a, out, tn);
                    lit_acc->len = 0;
                }
                TNode fn; xmemset(&fn, 0, sizeof(fn));
                fn.kind = TNODE_FOR;
                fn.text = n->text;
                fn.path = n->path;
                fn.path_len = n->path_len;
                Str next_locals[16];
                int nn = n_locals < 15 ? n_locals : 15;
                for (int j = 0; j < nn; j++) next_locals[j] = locals[j];
                next_locals[nn] = n->text;
                SB sub_acc; sb_init(&sub_acc, a, 256);
                specialize_list(a, &n->body, &fn.body, ctx, dyn_mask, next_locals, nn + 1, depth, &sub_acc);
                if (sub_acc.len > 0) {
                    TNode tn; xmemset(&tn, 0, sizeof(tn));
                    tn.kind = TNODE_TEXT; tn.is_json_lit = 1;
                    tn.text = arena_dup(a, sub_acc.buf, sub_acc.len);
                    tlist_push(a, &fn.body, tn);
                }
                // Detect cacheable card loop: top-level `for x in posts` or `for x in tag.posts` whose body only uses x (and folded site constants)!
                if (n_locals == 0) {
                    int is_posts_loop = (fn.path_len == 1 && str_eq_lit(fn.path[0], "posts", 5)) ||
                                        (fn.path_len == 2 && str_eq_lit(fn.path[0], "tag", 3) && str_eq_lit(fn.path[1], "posts", 5));
                    if (is_posts_loop && subtree_only_uses_var(&fn.body, fn.text, NULL, 0)) {
                        fn.cacheable_card_loop = 1;
                        fn.body_hash = hash_tnodelist(&fn.body, fn.text);
                    }
                }
                tlist_push(a, out, fn);
                break;
            }
        }
    }
}

static int tlist_uses_escaped_md(const TNodeList *list) {
    for (uint32_t i = 0; i < list->len; i++) {
        const TNode *n = &list->items[i];
        if (n->kind == TNODE_VAR && n->path_len >= 2) {
            Str last = n->path[n->path_len - 1];
            if (str_eq_lit(last, "content", 7) || str_eq_lit(last, "excerpt", 7) || str_eq_lit(last, "html", 4))
                return 1;
        }
        if (n->kind == TNODE_IF) {
            if (tlist_uses_escaped_md(&n->body)) return 1;
            if (n->has_else && tlist_uses_escaped_md(&n->else_body)) return 1;
        } else if (n->kind == TNODE_FOR) {
            if (tlist_uses_escaped_md(&n->body)) return 1;
        }
    }
    return 0;
}

static int templates_use_escaped_md(const TNodeList *tpl_list, uint32_t n_tpls) {
    for (uint32_t i = 0; i < n_tpls; i++) {
        if (tlist_uses_escaped_md(&tpl_list[i])) return 1;
    }
    return 0;
}

// Resolve static FastOp opcode + scope depth for a compiled TNode (returns elem_kind for FOR loops)
static uint8_t resolve_fast_op(TNode *n, uint32_t dyn_mask, const Str *locals, const uint8_t *local_kinds, int n_locals) {
    n->fast_op = FOP_GENERIC;
    n->fast_depth = 0;
    if (n->path_len == 0) return 0;
    Str p0 = n->path[0];

    // Check loop scopes from innermost outwards
    if (n_locals > 0) {
        if (str_eq_lit(p0, "loop", 4)) {
            if (n->path_len == 2) {
                Str p1 = n->path[1];
                if (str_eq_lit(p1, "index", 5)) n->fast_op = FOP_LOOP_INDEX;
                else if (str_eq_lit(p1, "first", 5)) n->fast_op = FOP_LOOP_FIRST;
                else if (str_eq_lit(p1, "last", 4)) n->fast_op = FOP_LOOP_LAST;
                else if (str_eq_lit(p1, "length", 6)) n->fast_op = FOP_LOOP_LENGTH;
            }
            return 0;
        }
        for (int i = n_locals - 1; i >= 0; i--) {
            if (str_eq(p0, locals[i])) {
                uint8_t k = local_kinds[i];
                uint8_t depth = (uint8_t)(n_locals - 1 - i);
                n->fast_depth = depth;
                if (n->path_len == 2) {
                    Str p1 = n->path[1];
                    if (k == VAL_CARD) {
                        if (str_eq_lit(p1, "title", 5)) n->fast_op = FOP_LV_CARD_TITLE;
                        else if (str_eq_lit(p1, "url", 3)) n->fast_op = FOP_LV_CARD_URL;
                        else if (str_eq_lit(p1, "date", 4)) n->fast_op = FOP_LV_CARD_DATE;
                        else if (str_eq_lit(p1, "date_long", 9)) n->fast_op = FOP_LV_CARD_DATE_LONG;
                        else if (str_eq_lit(p1, "summary", 7)) n->fast_op = FOP_LV_CARD_SUMMARY;
                        else if (str_eq_lit(p1, "excerpt", 7)) n->fast_op = FOP_LV_CARD_EXCERPT;
                        else if (str_eq_lit(p1, "reading_minutes", 15)) n->fast_op = FOP_LV_CARD_READING_MIN;
                        else if (str_eq_lit(p1, "tags", 4)) { n->fast_op = FOP_LV_CARD_TAGS; return VAL_TAG_REF; }
                    } else if (k == VAL_TAG_REF || k == VAL_TAG_SUMMARY) {
                        if (str_eq_lit(p1, "name", 4)) n->fast_op = FOP_LV_TAG_NAME;
                        else if (str_eq_lit(p1, "slug", 4)) n->fast_op = FOP_LV_TAG_SLUG;
                        else if (str_eq_lit(p1, "url", 3)) n->fast_op = FOP_LV_TAG_URL;
                        else if (k == VAL_TAG_SUMMARY && str_eq_lit(p1, "count", 5)) n->fast_op = FOP_LV_TAG_COUNT;
                    } else if (k == VAL_TOC_ITEM) {
                        if (str_eq_lit(p1, "level", 5)) n->fast_op = FOP_LV_TOC_LEVEL;
                        else if (str_eq_lit(p1, "id", 2)) n->fast_op = FOP_LV_TOC_ID;
                        else if (str_eq_lit(p1, "html", 4)) n->fast_op = FOP_LV_TOC_HTML;
                    }
                }
                return 0;
            }
        }
    }

    // Root dynamic variables
    if ((dyn_mask & ROOT_MASK_PAGE) && str_eq_lit(p0, "page", 4)) {
        if (n->path_len == 1) { n->fast_op = FOP_PAGE_OBJ; return 0; }
        Str p1 = n->path[1];
        if (n->path_len == 2) {
            if (str_eq_lit(p1, "title", 5)) n->fast_op = FOP_PAGE_TITLE;
            else if (str_eq_lit(p1, "url", 3)) n->fast_op = FOP_PAGE_URL;
            else if (str_eq_lit(p1, "date", 4)) n->fast_op = FOP_PAGE_DATE;
            else if (str_eq_lit(p1, "date_long", 9)) n->fast_op = FOP_PAGE_DATE_LONG;
            else if (str_eq_lit(p1, "summary", 7)) n->fast_op = FOP_PAGE_SUMMARY;
            else if (str_eq_lit(p1, "excerpt", 7)) n->fast_op = FOP_PAGE_EXCERPT;
            else if (str_eq_lit(p1, "reading_minutes", 15)) n->fast_op = FOP_PAGE_READING_MIN;
            else if (str_eq_lit(p1, "slug", 4)) n->fast_op = FOP_PAGE_SLUG;
            else if (str_eq_lit(p1, "content", 7)) n->fast_op = FOP_PAGE_CONTENT;
            else if (str_eq_lit(p1, "tags", 4)) { n->fast_op = FOP_PAGE_TAGS; return VAL_TAG_REF; }
            else if (str_eq_lit(p1, "toc", 3)) { n->fast_op = FOP_PAGE_TOC; return VAL_TOC_ITEM; }
            else if (str_eq_lit(p1, "newer", 5)) n->fast_op = FOP_PAGE_NEWER;
            else if (str_eq_lit(p1, "older", 5)) n->fast_op = FOP_PAGE_OLDER;
            else if (str_eq_lit(p1, "is_post", 7)) n->fast_op = FOP_PAGE_IS_POST;
        } else if (n->path_len == 3) {
            Str p2 = n->path[2];
            if (str_eq_lit(p1, "newer", 5)) {
                if (str_eq_lit(p2, "url", 3)) n->fast_op = FOP_PAGE_NEWER_URL;
                else if (str_eq_lit(p2, "title", 5)) n->fast_op = FOP_PAGE_NEWER_TITLE;
            } else if (str_eq_lit(p1, "older", 5)) {
                if (str_eq_lit(p2, "url", 3)) n->fast_op = FOP_PAGE_OLDER_URL;
                else if (str_eq_lit(p2, "title", 5)) n->fast_op = FOP_PAGE_OLDER_TITLE;
            }
        }
    } else if ((dyn_mask & ROOT_MASK_POSTS) && str_eq_lit(p0, "posts", 5) && n->path_len == 1) {
        n->fast_op = FOP_POSTS; return VAL_CARD;
    } else if ((dyn_mask & ROOT_MASK_PAGER) && str_eq_lit(p0, "pager", 5) && n->path_len == 2) {
        Str p1 = n->path[1];
        if (str_eq_lit(p1, "number", 6)) n->fast_op = FOP_PAGER_NUMBER;
        else if (str_eq_lit(p1, "total", 5)) n->fast_op = FOP_PAGER_TOTAL;
        else if (str_eq_lit(p1, "url", 3)) n->fast_op = FOP_PAGER_URL;
        else if (str_eq_lit(p1, "newer_url", 9)) n->fast_op = FOP_PAGER_NEWER_URL;
        else if (str_eq_lit(p1, "older_url", 9)) n->fast_op = FOP_PAGER_OLDER_URL;
    } else if ((dyn_mask & ROOT_MASK_TAG) && str_eq_lit(p0, "tag", 3)) {
        if (n->path_len == 1) { n->fast_op = FOP_TAG_OBJ; return 0; }
        if (n->path_len == 2) {
            Str p1 = n->path[1];
            if (str_eq_lit(p1, "name", 4)) n->fast_op = FOP_TAG_NAME;
            else if (str_eq_lit(p1, "slug", 4)) n->fast_op = FOP_TAG_SLUG;
            else if (str_eq_lit(p1, "url", 3)) n->fast_op = FOP_TAG_URL;
            else if (str_eq_lit(p1, "count", 5)) n->fast_op = FOP_TAG_COUNT;
            else if (str_eq_lit(p1, "posts", 5)) { n->fast_op = FOP_TAG_POSTS; return VAL_CARD; }
        }
    } else if (str_eq_lit(p0, "tags", 4) && n->path_len == 1) {
        return VAL_TAG_SUMMARY;
    } else if (str_eq_lit(p0, "recent", 6) && n->path_len == 1) {
        return VAL_CARD;
    }
    return 0;
}

static void annotate_fast_ops(TNodeList *list, uint32_t dyn_mask, const Str *locals, const uint8_t *local_kinds, int n_locals) {
    for (uint32_t i = 0; i < list->len; i++) {
        TNode *n = &list->items[i];
        if (n->kind == TNODE_TEXT || n->kind == TNODE_INCLUDE) continue;
        uint8_t elem_kind = resolve_fast_op(n, dyn_mask, locals, local_kinds, n_locals);
        if (n->kind == TNODE_IF) {
            annotate_fast_ops(&n->body, dyn_mask, locals, local_kinds, n_locals);
            if (n->has_else) annotate_fast_ops(&n->else_body, dyn_mask, locals, local_kinds, n_locals);
        } else if (n->kind == TNODE_FOR) {
            Str nl[16]; uint8_t nk[16];
            int nn = n_locals < 15 ? n_locals : 15;
            for (int j = 0; j < nn; j++) { nl[j] = locals[j]; nk[j] = local_kinds[j]; }
            nl[nn] = n->text;
            nk[nn] = elem_kind;
            annotate_fast_ops(&n->body, dyn_mask, nl, nk, nn + 1);
        }
    }
}

static TNodeList compile_template_for(Arena *a, const TNodeList *raw, const RenderCtx *ctx, uint32_t dyn_mask) {
    TNodeList out; xmemset(&out, 0, sizeof(out));
    SB lit_acc; sb_init(&lit_acc, a, 1024);
    specialize_list(a, raw, &out, ctx, dyn_mask, NULL, 0, 0, &lit_acc);
    if (lit_acc.len > 0) {
        TNode tn; xmemset(&tn, 0, sizeof(tn));
        tn.kind = TNODE_TEXT; tn.is_json_lit = 1;
        tn.text = arena_dup(a, lit_acc.buf, lit_acc.len);
        tlist_push(a, &out, tn);
    }
    annotate_fast_ops(&out, dyn_mask, NULL, NULL, 0);
    return out;
}
