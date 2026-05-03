#ifndef AHK_BRIDGE_H
#define AHK_BRIDGE_H

#define UNICODE

#include <MCL.h>
#include <oaidl.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifndef NULL
#define NULL 0
#endif

/* ---------- Factories & sentinels supplied by AHK at load time ---------- */

static IDispatch *fnGetMap;
MCL_EXPORT_GLOBAL(fnGetMap, Ptr);
static IDispatch *fnGetArray;
MCL_EXPORT_GLOBAL(fnGetArray, Ptr);

static IDispatch *objNull;
MCL_EXPORT_GLOBAL(objNull, Ptr);
static IDispatch *objTrue;
MCL_EXPORT_GLOBAL(objTrue, Ptr);
static IDispatch *objFalse;
MCL_EXPORT_GLOBAL(objFalse, Ptr);

static bool bNullsAsStrings = true;
MCL_EXPORT_GLOBAL(bNullsAsStrings, Int);
static bool bBoolsAsInts = true;
MCL_EXPORT_GLOBAL(bBoolsAsInts, Int);
static bool bResolveMergeKeys = true;
MCL_EXPORT_GLOBAL(bResolveMergeKeys, Int);
static bool bStrictBools = false;
MCL_EXPORT_GLOBAL(bStrictBools, Int);
static bool bStrictTags = true;
MCL_EXPORT_GLOBAL(bStrictTags, Int);

/* AHK-side callback function pointers (CallbackCreate). Registered at load
 * time alongside fnGetMap / fnGetArray. The function-pointer typedefs match
 * the cdecl ABI used by CallbackCreate(..., "C F", 3). */
static void *pObjFromYAML;
MCL_EXPORT_GLOBAL(pObjFromYAML, Ptr);
static void *pObjToYAML;
MCL_EXPORT_GLOBAL(pObjToYAML, Ptr);

typedef int (*pfn_obj_from_yaml)(const char *tag_utf8, VARIANT *in, VARIANT *out);
typedef int (*pfn_obj_to_yaml)(IDispatch *obj, char *tag_out_256, VARIANT *repl_out);

/* ---------- OleAut32 imports ---------- */

MCL_IMPORT(BSTR, OleAut32, SysAllocString, (const WCHAR *psz));
MCL_IMPORT(BSTR, OleAut32, SysAllocStringLen, (const WCHAR *psz, UINT ui));
MCL_IMPORT(void, OleAut32, SysFreeString, (BSTR bstrString));

/* ---------- Kernel32 UTF-8 <-> UTF-16 ---------- */

MCL_IMPORT(int, Kernel32, MultiByteToWideChar,
           (UINT CodePage, DWORD dwFlags, const char *lpMultiByteStr,
            int cbMultiByte, WCHAR *lpWideCharStr, int cchWideChar));
MCL_IMPORT(int, Kernel32, WideCharToMultiByte,
           (UINT CodePage, DWORD dwFlags, const WCHAR *lpWideCharStr,
            int cchWideChar, char *lpMultiByteStr, int cbMultiByte,
            const char *lpDefaultChar, int *lpUsedDefaultChar));

#define CP_UTF8 65001

/* ---------- C Runtime functions ---------- */

MCL_IMPORT(int, msvcrt, _snprintf, (char *, size_t, const char *, ...));
#define snprintf _snprintf

/* ---------- COM helpers ---------- */

static IID g_IID_NULL[16] = {0};

/**
 * Pre-built BSTRs for methods we invoke by name. AHK's BSTRs use a 4-byte
 * length prefix immediately before the string data; DECLARE_BSTR mirrors
 * cjson_dumps.c's pattern. Must be used read-only; never SysFreeString.
 */
#define DECLARE_BSTR(Variable, String) \
struct {                               \
    uint32_t uLength;                  \
    wchar_t  szData[sizeof(String)];   \
} Variable = { sizeof(String) - sizeof(wchar_t), String };

DECLARE_BSTR(static s_bstrPush,      L"Push")
DECLARE_BSTR(static s_bstrSet,       L"Set")
DECLARE_BSTR(static s_bstrHasMethod, L"HasMethod")
DECLARE_BSTR(static s_bstrEnum,      L"__Enum")
DECLARE_BSTR(static s_bstrHas,       L"Has")
DECLARE_BSTR(static s_bstrToYAML,    L"ToYAML")

/* Construct a fresh AHK Map via fnGetMap(). */
static IDispatch *ahk_new_map(void)
{
    DISPPARAMS dp = {.cArgs = 0, .cNamedArgs = 0};
    VARIANT out;
    HRESULT hr = fnGetMap->lpVtbl->Invoke(fnGetMap, 0, g_IID_NULL, 0,
                                           DISPATCH_METHOD, &dp, &out,
                                           NULL, NULL);
    if (hr != S_OK || out.vt != VT_DISPATCH) return NULL;
    return out.pdispVal;
}

/* Construct a fresh AHK Array via fnGetArray(). */
static IDispatch *ahk_new_array(void)
{
    DISPPARAMS dp = {.cArgs = 0, .cNamedArgs = 0};
    VARIANT out;
    HRESULT hr = fnGetArray->lpVtbl->Invoke(fnGetArray, 0, g_IID_NULL, 0,
                                             DISPATCH_METHOD, &dp, &out,
                                             NULL, NULL);
    if (hr != S_OK || out.vt != VT_DISPATCH) return NULL;
    return out.pdispVal;
}

static DISPID dispatch_get_dispid(IDispatch *obj, wchar_t *strName) {
    LPOLESTR name = (LPOLESTR)strName;
    DISPID out = 0;
    HRESULT hr = obj->lpVtbl->GetIDsOfNames(obj, g_IID_NULL, &name, 1, 0,
                                            &out);

    return hr == S_OK ? out : DISPID_UNKNOWN;
}

/* map[*key] = *value  (DISPID_VALUE + DISPATCH_PROPERTYPUT, named arg).
 * Key may be any VARIANT type; AHK Map handles arbitrary keys. */
static HRESULT ahk_map_set(IDispatch *map, VARIANT *key, VARIANT *value)
{
    VARIANT args[2];
    /* rgvarg is in reverse order: args[0] = put-value, args[1] = key */
    args[0] = *value;
    args[1] = *key;

    DISPID dispidPut = DISPID_PROPERTYPUT;
    DISPPARAMS dp = {
        .cArgs = 2,
        .cNamedArgs = 1,
        .rgvarg = args,
        .rgdispidNamedArgs = &dispidPut
    };
    return map->lpVtbl->Invoke(map, DISPID_VALUE, g_IID_NULL, 0,
                                DISPATCH_PROPERTYPUT, &dp, NULL, NULL, NULL);
}

/* arr.Push(*value) */
static HRESULT ahk_array_push(IDispatch *arr, VARIANT *value)
{
    DISPID dispid = dispatch_get_dispid(arr, s_bstrPush.szData);
    if (dispid == DISPID_UNKNOWN) return DISP_E_UNKNOWNNAME;

    DISPPARAMS dp = {.cArgs = 1, .cNamedArgs = 0, .rgvarg = value};
    return arr->lpVtbl->Invoke(arr, dispid, g_IID_NULL, 0,
                                DISPATCH_METHOD, &dp, NULL, NULL, NULL);
}

/* obj.HasMethod(nameBstr) -> boolean. nameBstr is a DECLARE_BSTR szData pointer. */
static int call_has_method(IDispatch *obj, void *nameBstr)
{
    DISPID dispid = dispatch_get_dispid(obj, s_bstrHasMethod.szData);
    if (dispid == DISPID_UNKNOWN) return DISP_E_UNKNOWNNAME;

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

/* map.Has(*key) -> 1 if present, 0 otherwise (also 0 on dispatch failure). */
static int ahk_map_has(IDispatch *map, VARIANT *key)
{
    DISPID dispid = dispatch_get_dispid(map, s_bstrHas.szData);
    if (dispid == DISPID_UNKNOWN) return DISP_E_UNKNOWNNAME;

    VARIANT arg = *key;
    DISPPARAMS dp = { .cArgs = 1, .cNamedArgs = 0, .rgvarg = &arg };
    VARIANT r = { .vt = VT_EMPTY };
    if (map->lpVtbl->Invoke(map, dispid, g_IID_NULL, 0, DISPATCH_METHOD,
                             &dp, &r, NULL, NULL) != S_OK)
        return 0;
    return (r.vt == VT_I4 && r.intVal != 0) ||
           (r.vt == VT_I8 && r.llVal != 0) ||
           (r.vt == VT_BOOL && r.boolVal != 0);
}

/**
 * Obtain a 2-arg enumerator from an AHK object, analagous to `enum := obj.__Enum(2)`.
 * Returns 0 on success with outEnum=VT_DISPATCH; -1 on failure with outEnum=VT_EMPTY.
 */
static int get_enum2(IDispatch *obj, VARIANT *outEnum)
{
    DISPID dispid = dispatch_get_dispid(obj, s_bstrEnum.szData);
    if (dispid == DISPID_UNKNOWN) return DISP_E_UNKNOWNNAME;

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

/* Convert a UTF-8 buffer (possibly non-null-terminated) to a fresh BSTR. */
static BSTR bstr_from_utf8(const char *src, int cb)
{
    if (cb <= 0) return SysAllocString(L"");
    int cch = MultiByteToWideChar(CP_UTF8, 0, src, cb, NULL, 0);
    if (cch <= 0) return SysAllocString(L"");
    BSTR out = SysAllocStringLen(NULL, cch);
    if (!out) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, src, cb, out, cch);
    return out;
}

static void variant_release(VARIANT *v)
{
    if (v->vt == VT_BSTR && v->bstrVal) {
        SysFreeString(v->bstrVal);
    } else if (v->vt == VT_DISPATCH && v->pdispVal) {
        v->pdispVal->lpVtbl->Release(v->pdispVal);
    }
    v->vt = VT_EMPTY;
}

/**
 * Bitwise copy with AddRef on IDispatch / SysAllocStringLen on BSTR.
 */
static int variant_dupe(VARIANT *dst, const VARIANT *src)
{
    *dst = *src;
    if (src->vt == VT_BSTR) {
        if (src->bstrVal) {
            UINT len = 0;
            while (src->bstrVal[len]) len++;
            dst->bstrVal = SysAllocStringLen(src->bstrVal, len);
            if (!dst->bstrVal) { dst->vt = VT_EMPTY; return -1; }
        }
    } else if (src->vt == VT_DISPATCH) {
        if (src->pdispVal)
            src->pdispVal->lpVtbl->AddRef(src->pdispVal);
    }
    return 0;
}

/**
 * Populate a VARIANT as a VT_DISPATCH, bumping refcount expectations are
 * left to the caller - ObjRelease is handled on the AHK side after unwrap. 
 */
static inline void variant_set_dispatch(VARIANT *v, IDispatch *p)
{
    v->vt = VT_DISPATCH;
    v->pdispVal = p;
}

static inline void variant_set_bstr(VARIANT *v, BSTR b)
{
    v->vt = VT_BSTR;
    v->bstrVal = b;
}

/**
 * MCL's windows.h shim typedefs LONGLONG to `double` on 32-bit (it mirrors an
 * ancient DDK alignment hack). VARIANT.llVal therefore has type `double` in
 * this TU, so a direct `v->llVal = n` silently converts through float. Read
 * and write the 8 bytes at &v->llVal via memcpy to bypass the bad typedef. 
 * */
static inline void variant_set_i64(VARIANT *v, int64_t n)
{
    v->vt = VT_I8;
    memcpy(&v->llVal, &n, sizeof(n));
}

static inline int64_t variant_get_i64(const VARIANT *v)
{
    int64_t n;
    memcpy(&n, &v->llVal, sizeof(n));
    return n;
}

static inline void variant_set_r8(VARIANT *v, double d)
{
    v->vt = VT_R8;
    v->dblVal = d;
}

static inline void variant_set_empty_string(VARIANT *v)
{
    v->vt = VT_BSTR;
    v->bstrVal = SysAllocString(L"");
}

#endif /* AHK_BRIDGE_H */
