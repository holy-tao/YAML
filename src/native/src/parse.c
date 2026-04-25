#include "ahk_bridge.h"
#include "ahk_error.h"

#include <yaml.h>
#include <stdlib.h>
#include <string.h>

/* strtoll/strtod live in msvcrt as _strtoi64 / strtod. */
MCL_IMPORT(int64_t, msvcrt, _strtoi64, (const char *, char **, int));
MCL_IMPORT(double,  msvcrt, strtod,    (const char *, char **));

#pragma region Scalar Resolution

static int buf_eq(const char *s, size_t n, const char *lit)
{
    size_t ln = 0;
    while (lit[ln]) ln++;
    return (ln == n) && (memcmp(s, lit, n) == 0);
}

static int is_null_scalar(const char *s, size_t n)
{
    if (n == 0) return 1;
    static const char *nulls[] = {"null", "Null", "NULL", "~"};
    for (int i = 0; i < 4; i++)
        if (buf_eq(s, n, nulls[i])) return 1;
    return 0;
}

static inline int buf_eq_any(const char *s, size_t n, const char **arr, size_t arr_len) {
    for (size_t i = 0; i < arr_len; i++) {
        if (buf_eq(s, n, arr[i]))
            return 1;
    }
    return 0;
}

static int try_parse_bool(const char *s, size_t n, int *out)
{
    static const char *trues_11[] = {"true","True","TRUE","yes","Yes","YES","on","On","ON","y","Y"};
    static const char *trues_12[] = {"true","True","TRUE"};
    static const char *falses_11[] = {"false","False","FALSE","no","No","NO","off","Off","OFF","n","N"};
    static const char *falses_12[] = {"false","False","FALSE"};

    if(bStrictBools) {
        // Compare against YAML 1.2 spec
        if (buf_eq_any(s, n, trues_12, sizeof(trues_12)/sizeof(*trues_12))) { *out = 1; return 1; }
        if (buf_eq_any(s, n, falses_12, sizeof(falses_12)/sizeof(*falses_12))) { *out = 0; return 1; }
    } else {
        // Compare against YAML 1.1 spec
        if (buf_eq_any(s, n, trues_11, sizeof(trues_11)/sizeof(*trues_11))) { *out = 1; return 1; }
        if (buf_eq_any(s, n, falses_11, sizeof(falses_11)/sizeof(*falses_11))) { *out = 0; return 1; }
    }
    
    return 0;
}

static int try_parse_int(const char *s, size_t n, int64_t *out)
{
    if (n == 0 || n > 63) return 0;
    char buf[64];
    memcpy(buf, s, n);
    buf[n] = 0;

    /* strtoll base 0 handles 0x (hex) and leading 0 (octal).
     * We add YAML 1.2 "0o" octal by hand: rewrite "0o..." to "0..." */
    char *p = buf;
    if (n >= 3 && buf[0] == '0' && (buf[1] == 'o' || buf[1] == 'O')) {
        buf[1] = '0';
        p = buf + 1;
    }

    char *end = NULL;
    int64_t v = _strtoi64(p, &end, 0);
    if (!end || *end != 0) return 0;
    *out = v;
    return 1;
}

static int try_parse_float(const char *s, size_t n, double *out)
{
    if (n == 0) return 0;

    /* YAML special floats. */
    if (buf_eq(s, n, ".inf")  || buf_eq(s, n, ".Inf")  || buf_eq(s, n, ".INF")) {
        *out = 1e300 * 1e300; return 1;
    }
    if (buf_eq(s, n, "-.inf") || buf_eq(s, n, "-.Inf") || buf_eq(s, n, "-.INF")) {
        *out = -(1e300 * 1e300); return 1;
    }
    if (buf_eq(s, n, "+.inf") || buf_eq(s, n, "+.Inf") || buf_eq(s, n, "+.INF")) {
        *out = 1e300 * 1e300; return 1;
    }
    if (buf_eq(s, n, ".nan")  || buf_eq(s, n, ".NaN")  || buf_eq(s, n, ".NAN")) {
        double zero = 0.0; *out = zero / zero; return 1;
    }

    if (n > 63) return 0;
    char buf[64];
    memcpy(buf, s, n);
    buf[n] = 0;

    /* Require a '.' or exponent to call it a float, otherwise leave it to
     * try_parse_int or the string fallback. */
    int has_dot_or_exp = 0;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '.' || buf[i] == 'e' || buf[i] == 'E') {
            has_dot_or_exp = 1; break;
        }
    }
    if (!has_dot_or_exp) return 0;

    char *end = NULL;
    double v = strtod(buf, &end);
    if (!end || *end != 0) return 0;
    *out = v;
    return 1;
}

/* "tag:yaml.org,2002:" - libyaml expands the !! shorthand to this URI. */
#define STD_TAG_PREFIX "tag:yaml.org,2002:"
#define STD_TAG_PREFIX_LEN 18

static int tag_is_std(const char *tag)
{
    if (!tag) return 0;
    for (int i = 0; i < STD_TAG_PREFIX_LEN; i++) {
        if (tag[i] != STD_TAG_PREFIX[i]) return 0;
    }
    return 1;
}

/* Fill `out` from a YAML scalar event, honoring NullsAsStrings / BoolsAsInts.
 * If the event carries an explicit tag:yaml.org,2002:* tag, that forces the
 * type interpretation (and a coercion failure becomes a parse error). */
static int emit_scalar(yaml_event_t *ev, VARIANT *out)
{
    const char *s = (const char *)ev->data.scalar.value;
    size_t n = ev->data.scalar.length;
    yaml_scalar_style_t style = ev->data.scalar.style;
    const char *tag = (const char *)ev->data.scalar.tag;
    int line = (int)ev->start_mark.line + 1;
    int col  = (int)ev->start_mark.column + 1;

    /* Standard YAML 1.1 type tags: force the type, ignore inference. */
    if (tag_is_std(tag)) {
        const char *type = tag + STD_TAG_PREFIX_LEN;
        if (strcmp(type, "str") == 0) {
            BSTR b = bstr_from_utf8(s, (int)n);
            if (!b) { set_err("Out of memory", NULL, 0, 0); return -1; }
            variant_set_bstr(out, b);
            return 0;
        }
        if (strcmp(type, "null") == 0) {
            if (bNullsAsStrings) {
                variant_set_empty_string(out);
            } else {
                objNull->lpVtbl->AddRef(objNull);
                variant_set_dispatch(out, objNull);
            }
            return 0;
        }
        if (strcmp(type, "bool") == 0) {
            int bv;
            if (!try_parse_bool(s, n, &bv)) {
                set_err("Scalar with !!bool tag is not a valid boolean", tag, line, col);
                return -1;
            }
            if (bBoolsAsInts) {
                variant_set_i64(out, bv ? 1 : 0);
            } else {
                IDispatch *p = bv ? objTrue : objFalse;
                p->lpVtbl->AddRef(p);
                variant_set_dispatch(out, p);
            }
            return 0;
        }
        if (strcmp(type, "int") == 0) {
            int64_t iv;
            if (!try_parse_int(s, n, &iv)) {
                set_err("Scalar with !!int tag is not a valid integer", tag, line, col);
                return -1;
            }
            variant_set_i64(out, iv);
            return 0;
        }
        if (strcmp(type, "float") == 0) {
            double dv;
            if (try_parse_float(s, n, &dv)) {
                variant_set_r8(out, dv);
                return 0;
            }
            int64_t iv;
            if (try_parse_int(s, n, &iv)) {
                variant_set_r8(out, (double)iv);
                return 0;
            }
            set_err("Scalar with !!float tag is not a valid float", tag, line, col);
            return -1;
        }
        /* Unknown :type under the std prefix - fall through to inference. */
    }

    /* Quoted scalars are always strings. */
    int plain = (style == YAML_PLAIN_SCALAR_STYLE);

    if (plain) {
        if (is_null_scalar(s, n)) {
            if (bNullsAsStrings) {
                variant_set_empty_string(out);
            } else {
                objNull->lpVtbl->AddRef(objNull);
                variant_set_dispatch(out, objNull);
            }
            return 0;
        }

        int bv;
        if (try_parse_bool(s, n, &bv)) {
            if (bBoolsAsInts) {
                variant_set_i64(out, bv ? 1 : 0);
            } else {
                IDispatch *p = bv ? objTrue : objFalse;
                p->lpVtbl->AddRef(p);
                variant_set_dispatch(out, p);
            }
            return 0;
        }

        int64_t iv;
        if (try_parse_int(s, n, &iv)) {
            variant_set_i64(out, iv);
            return 0;
        }

        double dv;
        if (try_parse_float(s, n, &dv)) {
            variant_set_r8(out, dv);
            return 0;
        }
    }

    /* Fallback: string. */
    BSTR b = bstr_from_utf8(s, (int)n);
    if (!b) { set_err("Out of memory", NULL, 0, 0); return -1; }
    variant_set_bstr(out, b);
    return 0;
}

#pragma region Container Stack

typedef enum { FRAME_MAP, FRAME_ARRAY } frame_kind_t;

typedef struct {
    frame_kind_t kind;
    IDispatch *obj;
    BSTR pending_key;   /* maps: buffered key awaiting its value */
    int  has_key;
    char *tag;          /* owned dup of START event tag, or NULL */
    char *anchor;       /* owned dup of START event anchor, or NULL */
    int   start_line;   /* for error reporting on END */
    int   start_col;
} frame_t;

/* Duplicate a NUL-terminated string with malloc; returns NULL on alloc failure
 * or for empty/NULL input. */
static char *dup_cstr(const unsigned char *s)
{
    if (!s || !*s) return NULL;
    size_t n = 0;
    while (s[n]) n++;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

/* Dispatch a non-standard tag to the AHK pObjFromYAML callback.
 * Returns:
 *    1 - dispatched; *v released and replaced with the callback's result
 *    0 - not dispatched (NULL/std/unknown-prefix tag, or non-strict failure);
 *        *v left as-is
 *   -1 - strict-mode failure; err globals populated. *v left as-is. */
static int dispatch_custom_tag(const char *tag, VARIANT *v, int line, int col)
{
    if (!tag || !*tag || !pObjFromYAML) return 0;
    if (tag_is_std(tag)) return 0;

    pfn_obj_from_yaml fn = (pfn_obj_from_yaml)pObjFromYAML;
    VARIANT out = { .vt = VT_EMPTY };
    int rc = fn(tag, v, &out);
    if (rc == 0) {
        *v = out;
        return 1;
    }
    if (rc == 1) {
        variant_release(&out);
        return 0;
    }
    /* Strict failure - AHK side wrote message/extra. Backfill line/col. */
    variant_release(&out);
    if (g_err_line == 0) g_err_line = line;
    if (g_err_column == 0) g_err_column = col;
    return -1;
}

#define MAX_DEPTH 256

/* Current event position, refreshed at the top of each event. Used by helpers
 * that don't otherwise have the yaml_event_t in scope (e.g. merge-key errors
 * raised from deep inside assign_to_top). */
static int g_cur_line;
static int g_cur_col;

/* True iff `key` is exactly the two-character BSTR "<<". */
static int is_merge_key(BSTR key)
{
    return key && key[0] == L'<' && key[1] == L'<' && key[2] == 0;
}

/**
 * Merge entries from `src` into `dst`, skipping keys already present in `dst`.
 * Only string keys are considered; non-string keys are ignored (merge sources
 * built from YAML always have BSTR keys, matching the parser's invariant).
 */
static int merge_one(IDispatch *dst, IDispatch *src)
{
    VARIANT en;
    if (get_enum2(src, &en) != 0) {
        set_err("Failed to enumerate merge source mapping", NULL,
                g_cur_line, g_cur_col);
        return -1;
    }
    int rc = 0;
    for (;;) {
        VARIANT k, v;
        if (!enum_next(en.pdispVal, &k, &v)) break;
        if (k.vt == VT_BSTR && k.bstrVal) {
            if (!ahk_map_has(dst, k.bstrVal)) {
                if (ahk_map_set(dst, k.bstrVal, &v) != S_OK) {
                    set_err("Map set failed", NULL, g_cur_line, g_cur_col);
                    rc = -1;
                }
            }
        }
        variant_release(&k);
        variant_release(&v);
        if (rc != 0) break;
    }
    en.pdispVal->lpVtbl->Release(en.pdispVal);
    return rc;
}

/**
 * Handle a `<<:` value. Accepts:
 *   - a Map (single merge source)
 *   - an Array of Maps (earlier items override later ones)
 * Anything else raises an error.
 */
static int perform_merge(IDispatch *dst, VARIANT *src)
{
    if (src->vt != VT_DISPATCH || !src->pdispVal) {
        set_err("Merge value must be a mapping or sequence of mappings",
                NULL, g_cur_line, g_cur_col);
        return -1;
    }
    IDispatch *obj = src->pdispVal;
    if (call_has_method(obj, s_bstrSet.szData)) {
        return merge_one(dst, obj);
    }
    if (call_has_method(obj, s_bstrPush.szData)) {
        VARIANT en;
        if (get_enum2(obj, &en) != 0) {
            set_err("Failed to enumerate merge source sequence",
                    NULL, g_cur_line, g_cur_col);
            return -1;
        }
        int rc = 0;
        for (;;) {
            VARIANT idx, item;
            if (!enum_next(en.pdispVal, &idx, &item)) break;
            if (item.vt != VT_DISPATCH || !item.pdispVal ||
                !call_has_method(item.pdispVal, s_bstrSet.szData)) {
                set_err("Merge sequence items must all be mappings",
                        NULL, g_cur_line, g_cur_col);
                rc = -1;
            } else if (merge_one(dst, item.pdispVal) != 0) {
                rc = -1;
            }
            variant_release(&idx);
            variant_release(&item);
            if (rc != 0) break;
        }
        en.pdispVal->lpVtbl->Release(en.pdispVal);
        return rc;
    }
    set_err("Merge value must be a mapping or sequence of mappings",
            NULL, g_cur_line, g_cur_col);
    return -1;
}

/**
 * Assign `value` into the top-of-stack container.
 * For maps, alternates between capturing a key and binding key->value.
 */
static int assign_to_top(frame_t *top, VARIANT *value)
{
    if (top->kind == FRAME_ARRAY) {
        HRESULT hr = ahk_array_push(top->obj, value);
        if (hr != S_OK) { set_err("Array.Push failed", NULL, 0, 0); return -1; }
        /* Release our ref on objects - Push AddRefs internally via AHK. */
        if (value->vt == VT_DISPATCH && value->pdispVal) {
            value->pdispVal->lpVtbl->Release(value->pdispVal);
        } else if (value->vt == VT_BSTR && value->bstrVal) {
            SysFreeString(value->bstrVal);
        }
        return 0;
    }

    /* Map: first scalar is the key. */
    if (!top->has_key) {
        /* Key must be a BSTR (YAML allows complex keys; we coerce or reject). */
        if (value->vt != VT_BSTR) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Non-string map keys not supported yet (vt=%d)", (int)value->vt);                                             
            set_err(msg, NULL, g_cur_line, g_cur_col);
            return -1;
        }
        top->pending_key = value->bstrVal;
        top->has_key = 1;
        return 0;
    }

    /* Have key = this value completes the pair. */
    int rc = 0;
    if (bResolveMergeKeys && is_merge_key(top->pending_key)) {
        rc = perform_merge(top->obj, value);
    } else {
        HRESULT hr = ahk_map_set(top->obj, top->pending_key, value);
        if (hr != S_OK) { set_err("Map set failed", NULL, 0, 0); rc = -1; }
    }

    /* Release key BSTR and our ref on the value. */
    SysFreeString(top->pending_key);
    top->pending_key = NULL;
    top->has_key = 0;
    if (value->vt == VT_DISPATCH && value->pdispVal) {
        value->pdispVal->lpVtbl->Release(value->pdispVal);
    } else if (value->vt == VT_BSTR && value->bstrVal) {
        SysFreeString(value->bstrVal);
    }
    return rc;
}

#pragma endregion
#pragma region Anchors / Aliases

/**
 *
 * An anchor definition registers its value under a name; a later alias event
 * retrieves and reuses that value. For containers the registered VARIANT
 * holds an AddRef'd IDispatch, so the resulting tree shares the same AHK
 * object across every alias site (mutating one mutates all).
 *
 * Registration happens on the *start* event for containers, so recursive
 * anchors like `&a [*a]` find themselves already in the table. */

typedef struct {
    char   *name;   /* owned, NUL-terminated UTF-8 */
    VARIANT value;  /* owns its AddRef / BSTR allocation */
} anchor_entry_t;

typedef struct {
    anchor_entry_t *items;
    int count;
    int capacity;
} anchor_registry_t;

#define MAX_ANCHORS 1024

static int anchor_name_eq(const char *a, const unsigned char *b)
{
    size_t i = 0;
    while (a[i] && b[i] && a[i] == (char)b[i]) i++;
    return a[i] == 0 && b[i] == 0;
}

static int registry_set(anchor_registry_t *r, const unsigned char *name,
                        const VARIANT *value)
{
    /* Overwrite: YAML permits anchor redefinition. */
    for (int i = 0; i < r->count; i++) {
        if (anchor_name_eq(r->items[i].name, name)) {
            variant_release(&r->items[i].value);
            return variant_dupe(&r->items[i].value, value);
        }
    }
    if (r->count >= MAX_ANCHORS) return -1;
    if (r->count >= r->capacity) {
        int newcap = r->capacity ? r->capacity * 2 : 16;
        anchor_entry_t *ni = (anchor_entry_t *)realloc(r->items,
                                (size_t)newcap * sizeof(anchor_entry_t));
        if (!ni) return -1;
        r->items = ni;
        r->capacity = newcap;
    }

    size_t nlen = 0;
    while (name[nlen]) nlen++;
    char *nm = (char *)malloc(nlen + 1);
    if (!nm) return -1;
    memcpy(nm, name, nlen + 1);

    r->items[r->count].name = nm;
    r->items[r->count].value.vt = VT_EMPTY;
    if (variant_dupe(&r->items[r->count].value, value) != 0) {
        free(nm);
        return -1;
    }
    r->count++;
    return 0;
}

static int registry_get(anchor_registry_t *r, const unsigned char *name,
                        VARIANT *out)
{
    for (int i = 0; i < r->count; i++) {
        if (anchor_name_eq(r->items[i].name, name))
            return variant_dupe(out, &r->items[i].value);
    }
    out->vt = VT_EMPTY;
    return -1;
}

static void registry_free(anchor_registry_t *r)
{
    for (int i = 0; i < r->count; i++) {
        if (r->items[i].name) free(r->items[i].name);
        variant_release(&r->items[i].value);
    }
    if (r->items) free(r->items);
    r->items = NULL;
    r->count = 0;
    r->capacity = 0;
}

#pragma endregion
#pragma region Parse Loop

/* Break out of the parse loop with an error code */
#define parse_break_err() { rc = -1; done = 1; break; }

/* Break out of the parse loop after setting an error message */
#define parse_exit_err(msg, extra) { set_err(msg, extra, 0, 0); parse_break_err(); }

/**
 * Parse events from `parser` until the next DOCUMENT_END or STREAM_END.
 *
 * The frame stack and anchor registry are document-local: anchors do not
 * carry across document boundaries (YAML 1.1 Â§3.2.2.2), so the registry
 * lives in this function.
 *
 * Out-params:
 *   doc_root        - filled with the document's value (only valid if *have_doc_root)
 *   have_doc_root   - 1 if a content event populated doc_root
 *   saw_doc_end     - 1 if a DOCUMENT_END_EVENT was consumed
 *   saw_stream_end  - 1 if a STREAM_END_EVENT was consumed (no more docs)
 *
 * Returns 0 on success, -1 on parse error. On error, partial state is
 * cleaned up internally and *have_doc_root is left as 0.
 */
static int parse_one_doc(yaml_parser_t *parser,
                         VARIANT *doc_root, int *have_doc_root,
                         int *saw_doc_end, int *saw_stream_end)
{
    *have_doc_root = 0;
    *saw_doc_end = 0;
    *saw_stream_end = 0;
    doc_root->vt = VT_EMPTY;

    // TODO move stack to heap and remove -mno-stack-arg-probe from compiler flags for safety
    frame_t stack[MAX_DEPTH];
    int depth = 0;

    anchor_registry_t registry = { NULL, 0, 0 };
    int rc = 0;

    for (;;) {
        int done = 0;

        yaml_event_t ev;
        if (!yaml_parser_parse(parser, &ev)) {
            set_err(parser->problem ? parser->problem : "Unknown parse error",
                    NULL,
                    (int)parser->problem_mark.line + 1,
                    (int)parser->problem_mark.column + 1);
            parse_break_err();
        }

        g_cur_line = (int)ev.start_mark.line + 1;
        g_cur_col  = (int)ev.start_mark.column + 1;

        switch (ev.type) {
        case YAML_STREAM_START_EVENT:
        case YAML_DOCUMENT_START_EVENT:
            break;

        case YAML_DOCUMENT_END_EVENT:
            *saw_doc_end = 1;
            done = 1;
            break;

        case YAML_STREAM_END_EVENT:
            *saw_stream_end = 1;
            done = 1;
            break;

        case YAML_ALIAS_EVENT: {
            VARIANT v;
            if (registry_get(&registry, ev.data.alias.anchor, &v) != 0) {
                set_err("Undefined anchor",
                        (char*)ev.data.alias.anchor,
                        (int)ev.start_mark.line + 1,
                        (int)ev.start_mark.column + 1);
                parse_break_err();
            }
            if (depth == 0) {
                *doc_root = v;
                *have_doc_root = 1;
            } else {
                if (assign_to_top(&stack[depth - 1], &v) != 0) { rc = -1; done = 1; }
            }
            break;
        }

        case YAML_SCALAR_EVENT: {
            VARIANT v;
            if (emit_scalar(&ev, &v) != 0) { parse_break_err(); }
            int dt = dispatch_custom_tag((const char *)ev.data.scalar.tag, &v,
                                          g_cur_line, g_cur_col);
            if (dt < 0) { parse_break_err(); }
            if (ev.data.scalar.anchor) {
                if (registry_set(&registry, ev.data.scalar.anchor, &v) != 0) {
                    variant_release(&v);
                    parse_exit_err("Anchor registration failed", (char*)ev.data.alias.anchor);
                }
            }
            if (depth == 0) {
                *doc_root = v;
                *have_doc_root = 1;
            } else {
                if (assign_to_top(&stack[depth - 1], &v) != 0) { rc = -1; done = 1; }
            }
            break;
        }

        case YAML_MAPPING_START_EVENT:
        case YAML_SEQUENCE_START_EVENT: {
            if (depth >= MAX_DEPTH) {
                parse_exit_err("YAML nesting too deep", NULL);
            }
            int is_map = (ev.type == YAML_MAPPING_START_EVENT);
            IDispatch *obj = is_map ? ahk_new_map() : ahk_new_array();
            if (!obj) {
                parse_exit_err("Failed to construct Map/Array", NULL);
            }
            const unsigned char *anchor = is_map
                ? ev.data.mapping_start.anchor
                : ev.data.sequence_start.anchor;
            if (anchor) {
                VARIANT vd;
                vd.vt = VT_DISPATCH;
                vd.pdispVal = obj;   /* registry_set AddRefs via dup_variant */
                if (registry_set(&registry, anchor, &vd) != 0) {
                    obj->lpVtbl->Release(obj);
                    parse_exit_err("Anchor registration failed", (char*)anchor);
                }
            }
            stack[depth].kind = is_map ? FRAME_MAP : FRAME_ARRAY;
            stack[depth].obj = obj;
            stack[depth].pending_key = NULL;
            stack[depth].has_key = 0;
            stack[depth].tag = dup_cstr(is_map
                ? ev.data.mapping_start.tag
                : ev.data.sequence_start.tag);
            stack[depth].anchor = anchor ? dup_cstr(anchor) : NULL;
            stack[depth].start_line = g_cur_line;
            stack[depth].start_col  = g_cur_col;
            depth++;
            break;
        }

        case YAML_MAPPING_END_EVENT:
        case YAML_SEQUENCE_END_EVENT: {
            depth--;
            VARIANT v;
            variant_set_dispatch(&v, stack[depth].obj);

            int dt = dispatch_custom_tag(stack[depth].tag, &v,
                                          stack[depth].start_line,
                                          stack[depth].start_col);
            if (dt < 0) {
                variant_release(&v);
                if (stack[depth].tag) free(stack[depth].tag);
                if (stack[depth].anchor) free(stack[depth].anchor);
                stack[depth].tag = NULL;
                stack[depth].anchor = NULL;
                parse_break_err();
            }
            if (dt == 1 && stack[depth].anchor) {
                /* Re-register the anchor with the replacement so later aliases
                 * resolve to the FromYAML result, not the raw container. */
                if (registry_set(&registry, (const unsigned char *)stack[depth].anchor, &v) != 0) {
                    set_err("Anchor registration failed", stack[depth].anchor, 0, 0);
                    variant_release(&v);
                    if (stack[depth].tag) free(stack[depth].tag);
                    if (stack[depth].anchor) free(stack[depth].anchor);
                    stack[depth].tag = NULL;
                    stack[depth].anchor = NULL;
                    parse_break_err();
                }
            }
            if (stack[depth].tag) free(stack[depth].tag);
            if (stack[depth].anchor) free(stack[depth].anchor);
            stack[depth].tag = NULL;
            stack[depth].anchor = NULL;

            if (depth == 0) {
                *doc_root = v;
                *have_doc_root = 1;
            } else {
                if (assign_to_top(&stack[depth - 1], &v) != 0) { rc = -1; done = 1; }
            }
            break;
        }

        default:
            break;
        }

        yaml_event_delete(&ev);
        if (done) break;
    }

    registry_free(&registry);

    if (rc != 0) {
        for (int i = 0; i < depth; i++) {
            if (stack[i].pending_key) SysFreeString(stack[i].pending_key);
            if (stack[i].obj) stack[i].obj->lpVtbl->Release(stack[i].obj);
            if (stack[i].tag) free(stack[i].tag);
            if (stack[i].anchor) free(stack[i].anchor);
        }
        if (*have_doc_root) {
            variant_release(doc_root);
            *have_doc_root = 0;
        }
    }

    return rc;
}

/* Initialize a yaml_parser_t with a string and a length. Returns NULL on failure. */
int parser_init(const char* utf8, size_t len, yaml_parser_t* parser) {
    if (!yaml_parser_initialize(parser)) {
        set_err("Parser init failed", NULL, 0, 0);
        return 0;
    }
    yaml_parser_set_input_string(parser, (const unsigned char *)utf8, (size_t)len);
    return 1;
}

MCL_EXPORT(loads, Ptr, utf8, Int64, len, Ptr, pOut, CDecl_Int);
int loads(const char *utf8, int64_t len, VARIANT *pOut)
{
    clear_err();
    pOut->vt = VT_EMPTY;

    yaml_parser_t parser;
    if (parser_init(utf8, len, &parser) == 0) { return -1; }

    VARIANT doc_root;
    int have_doc_root = 0;
    int saw_doc_end = 0;
    int saw_stream_end = 0;
    int rc = parse_one_doc(&parser, &doc_root, &have_doc_root,
                           &saw_doc_end, &saw_stream_end);

    /* If the first call ended on DOCUMENT_END (not STREAM_END), there may
     * be a second document. Drain one more to detect multi-doc streams. 
     */
    if (rc == 0 && saw_doc_end && !saw_stream_end) {
        VARIANT doc2;
        int have2 = 0, saw_doc_end2 = 0, saw_stream_end2 = 0;
        int rc2 = parse_one_doc(&parser, &doc2, &have2,
                                &saw_doc_end2, &saw_stream_end2);
        if (rc2 != 0) {
            if (have_doc_root) variant_release(&doc_root);
            have_doc_root = 0;
            rc = rc2;
        } else if (saw_doc_end2) {
            set_err("Multiple documents in stream (use ParseAll)", NULL, 0, 0);
            if (have2) variant_release(&doc2);
            if (have_doc_root) variant_release(&doc_root);
            have_doc_root = 0;
            rc = -2;
        }
        /* else: parser hit STREAM_END without another doc - single doc. */
    }

    yaml_parser_delete(&parser);

    if (rc == 0) {
        if (have_doc_root) {
            *pOut = doc_root;
        } else {
            /* Empty stream -> empty string, matches Null-ish behavior. */
            variant_set_empty_string(pOut);
        }
    }

    return rc;
}

MCL_EXPORT(loads_all, Ptr, utf8, Int64, len, Ptr, pOut, CDecl_Int);
int loads_all(const char *utf8, int64_t len, VARIANT *pOut)
{
    clear_err();
    pOut->vt = VT_EMPTY;

    yaml_parser_t parser;
    if (parser_init(utf8, len, &parser) == 0) { return -1; }

    IDispatch *result = ahk_new_array();
    if (!result) {
        set_err("Failed to construct Array", NULL, 0, 0);
        yaml_parser_delete(&parser);
        return -1;
    }

    int rc = 0;
    for (;;) {
        VARIANT doc_root;
        int have_doc_root = 0;
        int saw_doc_end = 0;
        int saw_stream_end = 0;
        rc = parse_one_doc(&parser, &doc_root, &have_doc_root,
                           &saw_doc_end, &saw_stream_end);
        if (rc != 0) break;

        if (saw_doc_end) {
            VARIANT v;
            if (have_doc_root) {
                v = doc_root;
            } else {
                /* Document with no content events - emit empty string,
                 * matching loads's empty-stream fallback. */
                variant_set_empty_string(&v);
            }
            HRESULT hr = ahk_array_push(result, &v);
            if (v.vt == VT_DISPATCH && v.pdispVal) {
                v.pdispVal->lpVtbl->Release(v.pdispVal);
            } else if (v.vt == VT_BSTR && v.bstrVal) {
                SysFreeString(v.bstrVal);
            }
            if (hr != S_OK) {
                set_err("Array.Push failed", NULL, 0, 0);
                rc = -1;
                break;
            }
        }

        if (saw_stream_end) break;
    }

    yaml_parser_delete(&parser);

    if (rc == 0) {
        variant_set_dispatch(pOut, result);
    } else {
        result->lpVtbl->Release(result);
    }
    return rc;
}

#pragma endregion