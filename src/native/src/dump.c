#include "ahk_bridge.h"
#include "ahk_error.h"

#include <yaml.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
    int oom;
} outbuf_t;

/* libyaml calls this for every chunk written. Returning 0 signals write error. */
static int outbuf_write_handler(void *user, unsigned char *buf, size_t size)
{
    outbuf_t *b = (outbuf_t *)user;
    if (b->size + size > b->capacity) {
        size_t newcap = b->capacity ? b->capacity : 256;
        while (newcap < b->size + size) newcap *= 2;
        unsigned char *nd = (unsigned char *)realloc(b->data, newcap);
        if (!nd) { b->oom = 1; return 0; }
        b->data = nd;
        b->capacity = newcap;
    }
    memcpy(b->data + b->size, buf, size);
    b->size += size;
    return 1;
}

#pragma region Parsing / Dispatch

/**
 * Return 1 if a plain UTF-8 scalar would round-trip back to a non-string
 * (null, bool, int, float) per our YAML 1.1 rules, in which case we must
 * emit the string in double-quoted style. 
 */
static int is_ambiguous_plain(const char *s, size_t n)
{
    if (n == 0) return 1;
    /* null set */
    if (n == 1 && s[0] == '~') return 1;
    if ((n == 4 && (memcmp(s, "null", 4) == 0 || memcmp(s, "Null", 4) == 0 || memcmp(s, "NULL", 4) == 0)))
        return 1;
    /* bool set */
    static const char *bools[] = {
        "true","True","TRUE","false","False","FALSE",
        "yes","Yes","YES","no","No","NO",
        "on","On","ON","off","Off","OFF",
        "y","Y","n","N"
    };
    for (size_t i = 0; i < sizeof(bools)/sizeof(*bools); i++) {
        size_t ln = 0; while (bools[i][ln]) ln++;
        if (ln == n && memcmp(s, bools[i], n) == 0) return 1;
    }
    /* pure-numeric look: sign? digits/./e */
    size_t p = 0;
    if (s[0] == '+' || s[0] == '-') p++;
    int has_digit = 0;
    int numeric_like = 1;
    for (size_t i = p; i < n; i++) {
        char c = s[i];
        if ((c >= '0' && c <= '9')) { has_digit = 1; continue; }
        if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-' ||
            c == 'x' || c == 'X' || c == 'o' || c == 'O') continue;
        numeric_like = 0; break;
    }
    if (numeric_like && has_digit) return 1;
    /* Leading/trailing whitespace or newline-bearing: force quoting */
    if (s[0] == ' ' || s[0] == '\t') return 1;
    if (s[n - 1] == ' ' || s[n - 1] == '\t') return 1;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\n' || s[i] == '\r' || s[i] == '\0') return 1;
    }
    return 0;
}

#pragma endregion
#pragma region Pass 1 (Ref Table)

/**
 *
 * YAML aliases require that a shared container be marked with an anchor on
 * first emission and referenced via `*name` thereafter. We learn which
 * containers are shared by walking the tree once up-front, counting how
 * many times each IDispatch pointer is reached.
 *
 * A count > 1 means the object is shared (multiple reachable paths) or
 * cyclic (reached via itself). Either way, pass 2 will emit an anchor on
 * the first visit and aliases on subsequent visits. count == 1 emits as
 * before with no anchor.
 *
 * Sentinels (objNull / objTrue / objFalse) are skipped - they're global
 * singletons, and anchoring every `null` would be noisy. 
 */
typedef struct {
    IDispatch *obj;
    int count;
    int anchor_id;  /* 0 until assigned at first emission */
    int emitted;    /* 1 once we've emitted the anchored definition */
} ref_entry_t;

typedef struct {
    ref_entry_t *items;
    int count;
    int capacity;
    int next_id;
} ref_table_t;

static ref_entry_t *reftab_find(ref_table_t *t, IDispatch *obj)
{
    for (int i = 0; i < t->count; i++)
        if (t->items[i].obj == obj) return &t->items[i];
    return NULL;
}

static ref_entry_t *reftab_insert(ref_table_t *t, IDispatch *obj)
{
    if (t->count >= t->capacity) {
        int newcap = t->capacity ? t->capacity * 2 : 16;
        ref_entry_t *ni = (ref_entry_t *)realloc(t->items,
                            (size_t)newcap * sizeof(ref_entry_t));
        if (!ni) return NULL;
        t->items = ni;
        t->capacity = newcap;
    }
    ref_entry_t *e = &t->items[t->count++];
    e->obj = obj;
    e->count = 1;
    e->anchor_id = 0;
    e->emitted = 0;
    return e;
}

static void reftab_free(ref_table_t *t)
{
    if (t->items) free(t->items);
    t->items = NULL;
    t->count = 0;
    t->capacity = 0;
    t->next_id = 0;
}

static int count_value(ref_table_t *t, VARIANT *v);

static int count_enum(ref_table_t *t, IDispatch *obj)
{
    VARIANT en;
    if (get_enum2(obj, &en) != 0) return -1;
    int rc = 0;
    for (;;) {
        VARIANT k, val;
        if (!enum_next(en.pdispVal, &k, &val)) break;
        if (count_value(t, &k) != 0) rc = -1;
        if (rc == 0 && count_value(t, &val) != 0) rc = -1;
        variant_release(&k);
        variant_release(&val);
        if (rc != 0) break;
    }
    en.pdispVal->lpVtbl->Release(en.pdispVal);
    return rc;
}

static int count_dispatch(ref_table_t *t, IDispatch *obj)
{
    if (obj == objNull || obj == objTrue || obj == objFalse) return 0;

    ref_entry_t *e = reftab_find(t, obj);
    if (e) {
        e->count++;
        return 0;  /* already walked this subtree; stops cycles. */
    }
    if (!reftab_insert(t, obj)) return -1;

    /* Recurse into children only on first visit. */
    if (call_has_method(obj, s_bstrPush.szData))
        return count_enum(t, obj);
    if (call_has_method(obj, s_bstrSet.szData))
        return count_enum(t, obj);
    return 0;  /* unknown object - the emitter will error; don't here */
}

static int count_value(ref_table_t *t, VARIANT *v)
{
    if (v->vt == VT_DISPATCH && v->pdispVal)
        return count_dispatch(t, v->pdispVal);
    return 0;
}

#pragma endregion
#pragma region Pass 2

static int emit_value(yaml_emitter_t *em, ref_table_t *t, VARIANT *v);

/** 
 * Emit a scalar with an explicit style choice. Numbers/bools/nulls pass
 * YAML_PLAIN_SCALAR_STYLE directly; strings route through emit_bstr which
 * applies the ambiguity check. 
 */
static int emit_scalar_styled(yaml_emitter_t *em, const char *s, size_t n,
                               yaml_scalar_style_t style)
{
    yaml_event_t ev;
    if (!yaml_scalar_event_initialize(&ev, NULL, NULL,
                                       (yaml_char_t *)s, (int)n, 1, 1, style))
        return -1;
    if (!yaml_emitter_emit(em, &ev)) return -1;
    return 0;
}

static int emit_plain(yaml_emitter_t *em, const char *s, size_t n)
{
    return emit_scalar_styled(em, s, n, YAML_PLAIN_SCALAR_STYLE);
}

static int emit_bstr(yaml_emitter_t *em, BSTR b)
{
    int wlen = 0;
    if (b) while (b[wlen]) wlen++;
    int need = WideCharToMultiByte(CP_UTF8, 0, (const WCHAR *)b, wlen,
                                    NULL, 0, NULL, NULL);
    if (need < 0) need = 0;
    char *tmp = (char *)malloc((size_t)need + 1);
    if (!tmp) return -1;
    if (need > 0)
        WideCharToMultiByte(CP_UTF8, 0, (const WCHAR *)b, wlen, tmp, need, NULL, NULL);
    tmp[need] = 0;
    yaml_scalar_style_t style = is_ambiguous_plain(tmp, (size_t)need)
        ? YAML_DOUBLE_QUOTED_SCALAR_STYLE
        : YAML_PLAIN_SCALAR_STYLE;
    int rc = emit_scalar_styled(em, tmp, (size_t)need, style);
    free(tmp);
    return rc;
}

static int emit_i64(yaml_emitter_t *em, int64_t n)
{
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lld", (long long)n);
    return emit_plain(em, buf, (size_t)len);
}

static int emit_r8(yaml_emitter_t *em, double d)
{
    char buf[64];
    /* Prefer round-trippable repr; %.17g covers double precision. */
    int len = snprintf(buf, sizeof(buf), "%.17g", d);
    /* Nudge toward YAML-conventional forms. */
    int is_numeric = 0;
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '.' || c == 'e' || c == 'E' || c == 'n' || c == 'i') { is_numeric = 1; break; }
    }
    if (!is_numeric) {
        /* integer-looking; add trailing .0 */
        if (len + 2 < (int)sizeof(buf)) {
            buf[len++] = '.'; buf[len++] = '0'; buf[len] = 0;
        }
    }
    return emit_plain(em, buf, (size_t)len);
}

static int emit_map(yaml_emitter_t *em, ref_table_t *t, IDispatch *obj,
                    const char *anchor)
{
    yaml_event_t ev;
    if (!yaml_mapping_start_event_initialize(&ev,
            (yaml_char_t *)anchor, NULL, 1,
            YAML_BLOCK_MAPPING_STYLE))
        return -1;
    if (!yaml_emitter_emit(em, &ev)) return -1;

    VARIANT en;
    if (get_enum2(obj, &en) != 0) {
        set_err("Failed to obtain Map enumerator", NULL, 0, 0);
        return -1;
    }
    int rc = 0;
    for (;;) {
        VARIANT k, v;
        if (!enum_next(en.pdispVal, &k, &v)) break;
        if (emit_value(em, t, &k) != 0) { rc = -1; }
        if (rc == 0 && emit_value(em, t, &v) != 0) { rc = -1; }
        variant_release(&k);
        variant_release(&v);
        if (rc != 0) break;
    }
    en.pdispVal->lpVtbl->Release(en.pdispVal);
    if (rc != 0) return rc;

    if (!yaml_mapping_end_event_initialize(&ev)) return -1;
    if (!yaml_emitter_emit(em, &ev)) return -1;
    return 0;
}

static int emit_array(yaml_emitter_t *em, ref_table_t *t, IDispatch *obj,
                      const char *anchor)
{
    yaml_event_t ev;
    if (!yaml_sequence_start_event_initialize(&ev,
            (yaml_char_t *)anchor, NULL, 1,
            YAML_BLOCK_SEQUENCE_STYLE))
        return -1;
    if (!yaml_emitter_emit(em, &ev)) return -1;

    VARIANT en;
    if (get_enum2(obj, &en) != 0) {
        set_err("Failed to obtain Array enumerator", NULL, 0, 0);
        return -1;
    }
    int rc = 0;
    for (;;) {
        VARIANT k, v;
        if (!enum_next(en.pdispVal, &k, &v)) break;
        /* For arrays, __Enum(2) yields (index, value). We only need value. */
        if (emit_value(em, t, &v) != 0) rc = -1;
        variant_release(&k);
        variant_release(&v);
        if (rc != 0) break;
    }
    en.pdispVal->lpVtbl->Release(en.pdispVal);
    if (rc != 0) return rc;

    if (!yaml_sequence_end_event_initialize(&ev)) return -1;
    if (!yaml_emitter_emit(em, &ev)) return -1;
    return 0;
}

static int emit_dispatch(yaml_emitter_t *em, ref_table_t *t, IDispatch *obj)
{
    /* Sentinels first (pointer identity). */
    if (obj == objNull)  return emit_plain(em, "null", 4);
    if (obj == objTrue)  return emit_plain(em, "true",  4);
    if (obj == objFalse) return emit_plain(em, "false", 5);

    /* Anchor/alias bookkeeping. Objects with count > 1 are shared or cyclic
     * and need an anchor on the first emission, aliases on the rest. */
    char anchor_buf[16];
    const char *anchor = NULL;
    ref_entry_t *e = reftab_find(t, obj);
    if (e && e->count > 1) {
        if (e->emitted) {
            snprintf(anchor_buf, sizeof(anchor_buf), "a%d", e->anchor_id);
            yaml_event_t ev;
            if (!yaml_alias_event_initialize(&ev, (yaml_char_t *)anchor_buf))
                return -1;
            if (!yaml_emitter_emit(em, &ev)) return -1;
            return 0;
        }
        e->anchor_id = ++t->next_id;
        e->emitted = 1;
        snprintf(anchor_buf, sizeof(anchor_buf), "a%d", e->anchor_id);
        anchor = anchor_buf;
    }

    /* Detect Array vs Map via HasMethod. Order matters: Map also has .Set,
     * Array has .Push; Arrays don't have Set, Maps don't have Push. */
    if (call_has_method(obj, s_bstrPush.szData))
        return emit_array(em, t, obj, anchor);
    if (call_has_method(obj, s_bstrSet.szData))
        return emit_map(em, t, obj, anchor);

    // TODO probe for a __Class property and include its value in the error message
    set_err("Unsupported object type (not Map or Array)", NULL, 0, 0);
    return -1;
}

static int emit_value(yaml_emitter_t *em, ref_table_t *t, VARIANT *v)
{
    switch (v->vt) {
    case VT_EMPTY:
    case VT_NULL:
        return emit_plain(em, "null", 4);
    case VT_BOOL:
        return v->boolVal ? emit_plain(em, "true", 4)
                          : emit_plain(em, "false", 5);
    case VT_I4:
        return emit_i64(em, v->intVal);
    case VT_I8:
        return emit_i64(em, variant_get_i64(v));
    case VT_R4:
        return emit_r8(em, (double)v->fltVal);
    case VT_R8:
        return emit_r8(em, v->dblVal);
    case VT_BSTR:
        return emit_bstr(em, v->bstrVal);
    case VT_DISPATCH:
        if (!v->pdispVal)
            return emit_plain(em, "null", 4);
        return emit_dispatch(em, t, v->pdispVal);
    default:
        set_err("Unsupported VARIANT type", NULL, 0, 0);
        return -1;
    }
}

#pragma endregion
#pragma region Entry Point

MCL_EXPORT(dumps, Ptr, pIn, Int, bPretty, Ptr, ppOut, Ptr, pOutSize, CDecl_Int);
int dumps(VARIANT *pIn, int bPretty, unsigned char **ppOut, int64_t *pOutSize)
{
    clear_err();
    *ppOut = NULL;
    *pOutSize = 0;

    outbuf_t ob = { .data = NULL, .size = 0, .capacity = 0, .oom = 0 };
    ref_table_t rt = { NULL, 0, 0, 0 };

    /* Pass 1: count references so we know which objects need anchors. */
    if (count_value(&rt, pIn) != 0) {
        set_err("Reference table overflow", NULL, 0, 0);
        reftab_free(&rt);
        return -1;
    }

    yaml_emitter_t em;
    if (!yaml_emitter_initialize(&em)) {
        set_err("Emitter init failed", NULL, 0, 0);
        return -1;
    }
    yaml_emitter_set_output(&em, outbuf_write_handler, &ob);
    yaml_emitter_set_unicode(&em, 1);
    yaml_emitter_set_width(&em, bPretty ? 80 : -1);
    yaml_emitter_set_indent(&em, 2);

    int rc = 0;
    yaml_event_t ev;

    if (!yaml_stream_start_event_initialize(&ev, YAML_UTF8_ENCODING) ||
        !yaml_emitter_emit(&em, &ev)) { rc = -1; goto done; }

    if (!yaml_document_start_event_initialize(&ev, NULL, NULL, NULL, 1) ||
        !yaml_emitter_emit(&em, &ev)) { rc = -1; goto done; }

    if (emit_value(&em, &rt, pIn) != 0) { rc = -1; goto done; }

    if (!yaml_document_end_event_initialize(&ev, 1) ||
        !yaml_emitter_emit(&em, &ev)) { rc = -1; goto done; }

    if (!yaml_stream_end_event_initialize(&ev) ||
        !yaml_emitter_emit(&em, &ev)) { rc = -1; goto done; }

done:
    if (rc != 0 && g_err_message[0] == 0) {
        set_err(em.problem ? em.problem : "Unknown emitter error", NULL, 0, 0);
    }
    yaml_emitter_delete(&em);
    reftab_free(&rt);

    if (rc == 0) {
        *ppOut = ob.data;
        *pOutSize = (int64_t)ob.size;
    } else {
        if (ob.data) free(ob.data);
    }
    return rc;
}

MCL_EXPORT(dump_free, Ptr, buf, CDecl_Int);
int dump_free(unsigned char *buf)
{
    if (buf) free(buf);
    return 0;
}
