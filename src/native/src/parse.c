#include "ahk_bridge.h"

#include <yaml.h>
#include <stdlib.h>
#include <string.h>

/* strtoll/strtod live in msvcrt as _strtoi64 / strtod. */
MCL_IMPORT(int64_t, msvcrt, _strtoi64, (const char *, char **, int));
MCL_IMPORT(double,  msvcrt, strtod,    (const char *, char **));

#pragma region Errors
// Error info exposed to AHK

static int  g_err_line;    MCL_EXPORT_GLOBAL(g_err_line, Int);
static int  g_err_column;  MCL_EXPORT_GLOBAL(g_err_column, Int);
static char g_err_message[256];

MCL_EXPORT(get_err_message, CDecl_Ptr);
const char *get_err_message(void) { return g_err_message; }

static void set_err(const char *msg, int line, int col)
{
    g_err_line = line;
    g_err_column = col;
    int i = 0;
    while (msg[i] && i < (int)sizeof(g_err_message) - 1) {
        g_err_message[i] = msg[i];
        i++;
    }
    g_err_message[i] = 0;
}

#pragma endregion
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

static int try_parse_bool(const char *s, size_t n, int *out)
{
    static const char *trues[]  = {
        "true","True","TRUE","yes","Yes","YES","on","On","ON","y","Y"
    };
    static const char *falses[] = {
        "false","False","FALSE","no","No","NO","off","Off","OFF","n","N"
    };
    for (size_t i = 0; i < sizeof(trues)/sizeof(*trues); i++)
        if (buf_eq(s, n, trues[i])) { *out = 1; return 1; }
    for (size_t i = 0; i < sizeof(falses)/sizeof(*falses); i++)
        if (buf_eq(s, n, falses[i])) { *out = 0; return 1; }
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

/* Fill `out` from a YAML scalar event, honoring NullsAsStrings / BoolsAsInts. */
static int emit_scalar(yaml_event_t *ev, VARIANT *out)
{
    const char *s = (const char *)ev->data.scalar.value;
    size_t n = ev->data.scalar.length;
    yaml_scalar_style_t style = ev->data.scalar.style;

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
    if (!b) { set_err("out of memory", 0, 0); return -1; }
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
} frame_t;

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
        set_err("Failed to enumerate merge source mapping",
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
                    set_err("Map set failed", g_cur_line, g_cur_col);
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
                g_cur_line, g_cur_col);
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
                    g_cur_line, g_cur_col);
            return -1;
        }
        int rc = 0;
        for (;;) {
            VARIANT idx, item;
            if (!enum_next(en.pdispVal, &idx, &item)) break;
            if (item.vt != VT_DISPATCH || !item.pdispVal ||
                !call_has_method(item.pdispVal, s_bstrSet.szData)) {
                set_err("Merge sequence items must all be mappings",
                        g_cur_line, g_cur_col);
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
            g_cur_line, g_cur_col);
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
        if (hr != S_OK) { set_err("Array.Push failed", 0, 0); return -1; }
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
            set_err(msg, g_cur_line, g_cur_col);
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
        if (hr != S_OK) { set_err("Map set failed", 0, 0); rc = -1; }
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

MCL_EXPORT(loads, Ptr, utf8, Int64, len, Ptr, pOut, CDecl_Int);
int loads(const char *utf8, int64_t len, VARIANT *pOut)
{
    g_err_line = 0;
    g_err_column = 0;
    g_err_message[0] = 0;
    pOut->vt = VT_EMPTY;

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        set_err("parser init failed", 0, 0);
        return -1;
    }
    yaml_parser_set_input_string(&parser, (const unsigned char *)utf8, (size_t)len);

    // TODO move stack to heap and remove -mno-stack-arg-probe from compiler flags for safety
    frame_t stack[MAX_DEPTH];
    int depth = 0;

    anchor_registry_t registry = { NULL, 0, 0 };

    int doc_count = 0;
    VARIANT doc_root;
    doc_root.vt = VT_EMPTY;
    int have_doc_root = 0;

    int rc = 0;

    for (;;) {
        yaml_event_t ev;
        if (!yaml_parser_parse(&parser, &ev)) {
            set_err(parser.problem ? parser.problem : "parse error",
                    (int)parser.problem_mark.line + 1,
                    (int)parser.problem_mark.column + 1);
            rc = -1;
            break;
        }

        int done = 0;
        g_cur_line = (int)ev.start_mark.line + 1;
        g_cur_col  = (int)ev.start_mark.column + 1;

        switch (ev.type) {
        case YAML_STREAM_START_EVENT:
        case YAML_DOCUMENT_START_EVENT:
            break;

        case YAML_DOCUMENT_END_EVENT:
            doc_count++;
            if (doc_count > 1) {
                set_err("Multiple documents in stream (use ParseAll)", 0, 0);
                rc = -2;
                done = 1;
            }
            break;

        case YAML_STREAM_END_EVENT:
            done = 1;
            break;

        case YAML_ALIAS_EVENT: {
            VARIANT v;
            if (registry_get(&registry, ev.data.alias.anchor, &v) != 0) {
                set_err("undefined anchor",
                        (int)ev.start_mark.line + 1,
                        (int)ev.start_mark.column + 1);
                rc = -1; done = 1; break;
            }
            if (depth == 0) {
                doc_root = v;
                have_doc_root = 1;
            } else {
                if (assign_to_top(&stack[depth - 1], &v) != 0) { rc = -1; done = 1; }
            }
            break;
        }

        case YAML_SCALAR_EVENT: {
            VARIANT v;
            if (emit_scalar(&ev, &v) != 0) { rc = -1; done = 1; break; }
            if (ev.data.scalar.anchor) {
                if (registry_set(&registry, ev.data.scalar.anchor, &v) != 0) {
                    set_err("anchor registration failed", 0, 0);
                    variant_release(&v);
                    rc = -1; done = 1; break;
                }
            }
            if (depth == 0) {
                doc_root = v;
                have_doc_root = 1;
            } else {
                if (assign_to_top(&stack[depth - 1], &v) != 0) { rc = -1; done = 1; }
            }
            break;
        }

        case YAML_MAPPING_START_EVENT:
        case YAML_SEQUENCE_START_EVENT: {
            if (depth >= MAX_DEPTH) {
                set_err("YAML nesting too deep", 0, 0);
                rc = -1; done = 1; break;
            }
            int is_map = (ev.type == YAML_MAPPING_START_EVENT);
            IDispatch *obj = is_map ? ahk_new_map() : ahk_new_array();
            if (!obj) {
                set_err("Failed to construct Map/Array", 0, 0);
                rc = -1; done = 1; break;
            }
            const unsigned char *anchor = is_map
                ? ev.data.mapping_start.anchor
                : ev.data.sequence_start.anchor;
            if (anchor) {
                VARIANT vd;
                vd.vt = VT_DISPATCH;
                vd.pdispVal = obj;   /* registry_set AddRefs via dup_variant */
                if (registry_set(&registry, anchor, &vd) != 0) {
                    set_err("anchor registration failed", 0, 0);
                    obj->lpVtbl->Release(obj);
                    rc = -1; done = 1; break;
                }
            }
            stack[depth].kind = is_map ? FRAME_MAP : FRAME_ARRAY;
            stack[depth].obj = obj;
            stack[depth].pending_key = NULL;
            stack[depth].has_key = 0;
            depth++;
            break;
        }

        case YAML_MAPPING_END_EVENT:
        case YAML_SEQUENCE_END_EVENT: {
            depth--;
            VARIANT v;
            variant_set_dispatch(&v, stack[depth].obj);
            if (depth == 0) {
                doc_root = v;
                have_doc_root = 1;
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

    yaml_parser_delete(&parser);
    registry_free(&registry);

    if (rc == 0) {
        if (have_doc_root) {
            *pOut = doc_root;
        } else {
            /* Empty stream -> empty string, matches Null-ish behavior. */
            variant_set_empty_string(pOut);
        }
    } else {
        /* Clean up any partially-built structures. */
        for (int i = 0; i < depth; i++) {
            if (stack[i].pending_key) SysFreeString(stack[i].pending_key);
            if (stack[i].obj) stack[i].obj->lpVtbl->Release(stack[i].obj);
        }
        if (have_doc_root && doc_root.vt == VT_DISPATCH && doc_root.pdispVal) {
            doc_root.pdispVal->lpVtbl->Release(doc_root.pdispVal);
        } else if (have_doc_root && doc_root.vt == VT_BSTR && doc_root.bstrVal) {
            SysFreeString(doc_root.bstrVal);
        }
    }

    return rc;
}

#pragma endregion