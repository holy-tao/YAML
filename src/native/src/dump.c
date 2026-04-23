#include "ahk_bridge.h"

#include <yaml.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- Dynamic output buffer ---------------- */

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

/* ---------------- Error info (shared pattern with parse.c) ---------------- */

static int  g_dump_err_line;   MCL_EXPORT_GLOBAL(g_dump_err_line, Int);
static int  g_dump_err_column; MCL_EXPORT_GLOBAL(g_dump_err_column, Int);
static char g_dump_err_message[256];

MCL_EXPORT(get_dump_err_message, CDecl_Ptr);
const char *get_dump_err_message(void) { return g_dump_err_message; }

static void set_dump_err(const char *msg)
{
    g_dump_err_line = 0;
    g_dump_err_column = 0;
    int i = 0;
    while (msg[i] && i < (int)sizeof(g_dump_err_message) - 1) {
        g_dump_err_message[i] = msg[i];
        i++;
    }
    g_dump_err_message[i] = 0;
}

/* ---------------- Number formatting ---------------- */

MCL_IMPORT(int, msvcrt, _snprintf, (char *, size_t, const char *, ...));
#define snprintf _snprintf

/* ---------------- Dispatch helpers ---------------- */

static int call_has_method(IDispatch *obj, void *nameBstr)
{
    LPOLESTR nm = (LPOLESTR)L"HasMethod";
    DISPID dispid = 0;
    if (obj->lpVtbl->GetIDsOfNames(obj, g_IID_NULL, &nm, 1, 0, &dispid) != S_OK)
        return 0;

    VARIANT arg = { .vt = VT_BSTR, .bstrVal = (BSTR)nameBstr };
    DISPPARAMS dp = { .cArgs = 1, .cNamedArgs = 0, .rgvarg = &arg };
    VARIANT r = { .vt = VT_EMPTY };
    if (obj->lpVtbl->Invoke(obj, dispid, g_IID_NULL, 0, DISPATCH_METHOD,
                             &dp, &r, NULL, NULL) != S_OK)
        return 0;
    return (r.vt == VT_I4 && r.intVal != 0) ||
           (r.vt == VT_I8 && r.llVal != 0) ||
           (r.vt == VT_BOOL && r.boolVal != 0);
}

/** 
 * Obtain a 2-arg enumerator from an AHK object, analagous to `enum := obj.__Enum(2)`.
 * Returns VT_DISPATCH or VT_EMPTY.
 */
static int get_enum2(IDispatch *obj, VARIANT *outEnum)
{
    LPOLESTR nm = (LPOLESTR)L"__Enum";
    DISPID dispid = 0;
    if (obj->lpVtbl->GetIDsOfNames(obj, g_IID_NULL, &nm, 1, 0, &dispid) != S_OK)
        return -1;

    VARIANT two = { .vt = VT_I4, .intVal = 2 };
    DISPPARAMS dp = { .cArgs = 1, .cNamedArgs = 0, .rgvarg = &two };
    outEnum->vt = VT_EMPTY;
    HRESULT hr = obj->lpVtbl->Invoke(obj, dispid, g_IID_NULL, 0,
                                      DISPATCH_METHOD | DISPATCH_PROPERTYGET,
                                      &dp, outEnum, NULL, NULL);
    if (hr != S_OK || outEnum->vt != VT_DISPATCH) return -1;
    return 0;
}

/* Call enum.Call(&k, &v). Returns 1 if a pair was produced, 0 on end. */
static int enum_next(IDispatch *en, VARIANT *pk, VARIANT *pv)
{
    VARIANT k_ = { .vt = VT_EMPTY };
    VARIANT v_ = { .vt = VT_EMPTY };
    VARIANT ak = { .vt = VT_BYREF | VT_VARIANT, .pvarVal = &k_ };
    VARIANT av = { .vt = VT_BYREF | VT_VARIANT, .pvarVal = &v_ };

    /* rgvarg is reversed: last formal is first in array */
    VARIANT args[2] = { av, ak };
    DISPPARAMS dp = { .cArgs = 2, .cNamedArgs = 0, .rgvarg = args };
    VARIANT r = { .vt = VT_EMPTY };
    HRESULT hr = en->lpVtbl->Invoke(en, 0, g_IID_NULL, 0, DISPATCH_METHOD,
                                     &dp, &r, NULL, NULL);
    if (hr != S_OK) return 0;
    if (!(r.vt == VT_I4 && r.intVal != 0) &&
        !(r.vt == VT_I8 && r.llVal != 0) &&
        !(r.vt == VT_BOOL && r.boolVal != 0)) return 0;
    *pk = k_;
    *pv = v_;
    return 1;
}

/* ---------------- Scalar string detection ---------------- */

/**
 * Return 1 if a plain UTF-8 scalar would round-trip back to a non-string
 * (null, bool, int, float) per our YAML 1.1 rules â€” in which case we must
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

/* ---------------- Emitter recursion ---------------- */

static int emit_value(yaml_emitter_t *em, VARIANT *v);

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

static int emit_map(yaml_emitter_t *em, IDispatch *obj)
{
    yaml_event_t ev;
    if (!yaml_mapping_start_event_initialize(&ev, NULL, NULL, 1,
                                              YAML_BLOCK_MAPPING_STYLE))
        return -1;
    if (!yaml_emitter_emit(em, &ev)) return -1;

    VARIANT en;
    if (get_enum2(obj, &en) != 0) {
        set_dump_err("failed to obtain Map enumerator");
        return -1;
    }
    int rc = 0;
    for (;;) {
        VARIANT k, v;
        if (!enum_next(en.pdispVal, &k, &v)) break;
        if (emit_value(em, &k) != 0) { rc = -1; }
        if (rc == 0 && emit_value(em, &v) != 0) { rc = -1; }
        if (k.vt == VT_DISPATCH && k.pdispVal) k.pdispVal->lpVtbl->Release(k.pdispVal);
        else if (k.vt == VT_BSTR && k.bstrVal) SysFreeString(k.bstrVal);
        if (v.vt == VT_DISPATCH && v.pdispVal) v.pdispVal->lpVtbl->Release(v.pdispVal);
        else if (v.vt == VT_BSTR && v.bstrVal) SysFreeString(v.bstrVal);
        if (rc != 0) break;
    }
    en.pdispVal->lpVtbl->Release(en.pdispVal);
    if (rc != 0) return rc;

    if (!yaml_mapping_end_event_initialize(&ev)) return -1;
    if (!yaml_emitter_emit(em, &ev)) return -1;
    return 0;
}

static int emit_array(yaml_emitter_t *em, IDispatch *obj)
{
    yaml_event_t ev;
    if (!yaml_sequence_start_event_initialize(&ev, NULL, NULL, 1,
                                                YAML_BLOCK_SEQUENCE_STYLE))
        return -1;
    if (!yaml_emitter_emit(em, &ev)) return -1;

    VARIANT en;
    if (get_enum2(obj, &en) != 0) {
        set_dump_err("failed to obtain Array enumerator");
        return -1;
    }
    int rc = 0;
    for (;;) {
        VARIANT k, v;
        if (!enum_next(en.pdispVal, &k, &v)) break;
        /* For arrays, __Enum(2) yields (index, value). We only need value. */
        if (emit_value(em, &v) != 0) rc = -1;
        if (k.vt == VT_DISPATCH && k.pdispVal) k.pdispVal->lpVtbl->Release(k.pdispVal);
        else if (k.vt == VT_BSTR && k.bstrVal) SysFreeString(k.bstrVal);
        if (v.vt == VT_DISPATCH && v.pdispVal) v.pdispVal->lpVtbl->Release(v.pdispVal);
        else if (v.vt == VT_BSTR && v.bstrVal) SysFreeString(v.bstrVal);
        if (rc != 0) break;
    }
    en.pdispVal->lpVtbl->Release(en.pdispVal);
    if (rc != 0) return rc;

    if (!yaml_sequence_end_event_initialize(&ev)) return -1;
    if (!yaml_emitter_emit(em, &ev)) return -1;
    return 0;
}

static int emit_dispatch(yaml_emitter_t *em, IDispatch *obj)
{
    /* Sentinels first (pointer identity). */
    if (obj == objNull)  return emit_plain(em, "null", 4);
    if (obj == objTrue)  return emit_plain(em, "true",  4);
    if (obj == objFalse) return emit_plain(em, "false", 5);

    /* Detect Array vs Map via HasMethod. Order matters: Map also has .Set,
     * Array has .Push; Arrays don't have Set, Maps don't have Push. */
    if (call_has_method(obj, s_bstrPush.szData))
        return emit_array(em, obj);
    if (call_has_method(obj, s_bstrSet.szData))
        return emit_map(em, obj);

    set_dump_err("Unsupported object type (not Map or Array)");
    return -1;
}

static int emit_value(yaml_emitter_t *em, VARIANT *v)
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
        return emit_dispatch(em, v->pdispVal);
    default:
        set_dump_err("Unsupported VARIANT type");
        return -1;
    }
}

/* ---------------- Entry points ---------------- */

MCL_EXPORT(dumps, Ptr, pIn, Int, bPretty, Ptr, ppOut, Ptr, pOutSize, CDecl_Int);
int dumps(VARIANT *pIn, int bPretty, unsigned char **ppOut, int64_t *pOutSize)
{
    g_dump_err_message[0] = 0;
    *ppOut = NULL;
    *pOutSize = 0;

    outbuf_t ob = { .data = NULL, .size = 0, .capacity = 0, .oom = 0 };

    yaml_emitter_t em;
    if (!yaml_emitter_initialize(&em)) {
        set_dump_err("emitter init failed");
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

    if (emit_value(&em, pIn) != 0) { rc = -1; goto done; }

    if (!yaml_document_end_event_initialize(&ev, 1) ||
        !yaml_emitter_emit(&em, &ev)) { rc = -1; goto done; }

    if (!yaml_stream_end_event_initialize(&ev) ||
        !yaml_emitter_emit(&em, &ev)) { rc = -1; goto done; }

done:
    if (rc != 0 && g_dump_err_message[0] == 0) {
        set_dump_err(em.problem ? em.problem : "emitter failed");
    }
    yaml_emitter_delete(&em);

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
