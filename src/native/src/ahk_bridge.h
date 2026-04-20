#ifndef AHK_BRIDGE_H
#define AHK_BRIDGE_H

#define UNICODE

#include <MCL.h>
#include <oaidl.h>
#include <stdint.h>
#include <stdbool.h>

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

/* ---------- COM helpers ---------- */

static IID g_IID_NULL[16] = {0};

/* Pre-built BSTRs for methods we invoke by name. AHK's BSTRs use a 4-byte
 * length prefix immediately before the string data; DECLARE_BSTR mirrors
 * cjson_dumps.c's pattern. Must be used read-only; never SysFreeString. */
#define DECLARE_BSTR(Variable, String) \
struct {                               \
    uint32_t uLength;                  \
    wchar_t  szData[sizeof(String)];   \
} Variable = { sizeof(String) - sizeof(wchar_t), String };

DECLARE_BSTR(static s_bstrPush,      L"Push")
DECLARE_BSTR(static s_bstrSet,       L"Set")
DECLARE_BSTR(static s_bstrHasMethod, L"HasMethod")
DECLARE_BSTR(static s_bstrEnum,      L"__Enum")

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

/* map[key] = *value  (DISPID_VALUE + DISPATCH_PROPERTYPUT, named arg). */
static HRESULT ahk_map_set(IDispatch *map, BSTR key, VARIANT *value)
{
    VARIANT args[2];
    /* rgvarg is in reverse order: args[0] = put-value, args[1] = key */
    args[0] = *value;
    args[1].vt = VT_BSTR;
    args[1].bstrVal = key;

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
    LPOLESTR name = (LPOLESTR)s_bstrPush.szData;
    DISPID dispidPush = 0;
    HRESULT hr = arr->lpVtbl->GetIDsOfNames(arr, g_IID_NULL, &name, 1, 0,
                                             &dispidPush);
    if (hr != S_OK) return hr;

    DISPPARAMS dp = {.cArgs = 1, .cNamedArgs = 0, .rgvarg = value};
    return arr->lpVtbl->Invoke(arr, dispidPush, g_IID_NULL, 0,
                                DISPATCH_METHOD, &dp, NULL, NULL, NULL);
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

/* Populate a VARIANT as a VT_DISPATCH, bumping refcount expectations are
 * left to the caller - ObjRelease is handled on the AHK side after unwrap. */
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

static inline void variant_set_i64(VARIANT *v, int64_t n)
{
    v->vt = VT_I8;
    v->llVal = n;
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
