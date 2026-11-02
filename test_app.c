// test_app.c - cputemp.c 內部函式的單元測試；失敗時回傳非 0
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>

// 換掉送給 Explorer 的呼叫，才數得到次數
static int notify_calls;
static BOOL fake_notify(DWORD msg, NOTIFYICONDATAW *data) { notify_calls++; return TRUE; }
#define Shell_NotifyIconW fake_notify

#define wWinMain cputemp_main // 只要內部函式，不要程式進入點
#include "cputemp.c"
#include <stdio.h>

static int fails;

static void check(const char *name, int ok)
{
    if (!ok) {
        printf("FAIL %s\n", name);
        fails++;
    }
}

// 環境變數被改掉時，DLL 路徑不可以跟著變
static void test_pawnio_path(void)
{
    static const wchar_t tail[] = L"\\PawnIO\\PawnIOLib.dll";
    const size_t tail_len = wcslen(tail);
    wchar_t path[MAX_PATH], tiny[8];

    SetEnvironmentVariableW(L"ProgramFiles", L"C:\\fake");
    check("path found", pawnio_dll_path(path, MAX_PATH));
    check("path ignores %ProgramFiles%", !wcsstr(path, L"fake"));
    check("path ends with the DLL", wcslen(path) > tail_len && !wcscmp(path + wcslen(path) - tail_len, tail));
    check("path too long for the buffer fails", !pawnio_dll_path(tiny, 8));
}

// 假的 WMI 物件：Next 依序回傳 script 裡的結果，用不到的方法留空
static HRESULT script[3];
static int script_pos;

static HRESULT STDMETHODCALLTYPE obj_get(IWbemClassObject *o, LPCWSTR name, LONG flags, VARIANT *v, CIMTYPE *type, LONG *flavor)
{
    if (!wcscmp(name, L"__RELPATH")) {
        V_VT(v) = VT_BSTR;
        v->bstrVal = SysAllocString(L"fan");
    } else {
        V_VT(v) = VT_I4;
        v->lVal = wcscmp(name, L"CurrentReading") ? 0 : 2000;
    }
    return S_OK;
}

static ULONG STDMETHODCALLTYPE obj_release(IWbemClassObject *o) { return 0; }
static IWbemClassObjectVtbl obj_vtbl = { .Get = (void *)obj_get, .Release = (void *)obj_release };
static IWbemClassObject fake_obj = { &obj_vtbl };

static HRESULT STDMETHODCALLTYPE enum_next(IEnumWbemClassObject *e, LONG timeout, ULONG count, IWbemClassObject **objs, ULONG *got)
{
    HRESULT hr = script[script_pos++];
    *got = hr == WBEM_S_NO_ERROR;
    if (*got) objs[0] = &fake_obj;
    return hr;
}

static ULONG STDMETHODCALLTYPE enum_release(IEnumWbemClassObject *e) { return 0; }
static IEnumWbemClassObjectVtbl enum_vtbl = { .Next = (void *)enum_next, .Release = (void *)enum_release };
static IEnumWbemClassObject fake_enum = { &enum_vtbl };

static HRESULT STDMETHODCALLTYPE svc_query(IWbemServices *s, const BSTR lang, const BSTR q, LONG flags, IWbemContext *ctx, IEnumWbemClassObject **out)
{
    *out = &fake_enum;
    return S_OK;
}

static IWbemServicesVtbl svc_vtbl = { .ExecQuery = (void *)svc_query };
static IWbemServices fake_svc = { &svc_vtbl };

static void query(const char *name, HRESULT a, HRESULT b, HRESULT c, int want, int want_paths)
{
    int rpm[MAX_FANS];
    script[0] = a;
    script[1] = b;
    script[2] = c;
    script_pos = 0;
    int got = query_fans(&fake_svc, rpm);
    if (got != want || fan_paths_n != want_paths) {
        printf("FAIL %s: query_fans = %d with %d paths, want %d with %d\n", name, got, fan_paths_n, want, want_paths);
        fails++;
    }
}

static void test_query_fans(void)
{
    query("no fans", WBEM_S_FALSE, 0, 0, 0, 0);
    query("two fans", WBEM_S_NO_ERROR, WBEM_S_NO_ERROR, WBEM_S_FALSE, 2, 2);
    query("timeout before any fan", WBEM_S_TIMEDOUT, 0, 0, -1, 0);
    query("timeout after one fan", WBEM_S_NO_ERROR, WBEM_S_TIMEDOUT, 0, -1, 0);
    query("failure after one fan", WBEM_S_NO_ERROR, WBEM_E_FAILED, 0, -1, 0);
}

// 假的 PawnIO：MSR 回傳 fake_temp 對應的讀值
static int fake_temp = 50;

static HRESULT WINAPI fake_exec(HANDLE h, PCSTR name, const ULONG64 *in, SIZE_T in_n, PULONG64 out, SIZE_T out_n, PSIZE_T got)
{
    *out = 0x80000000ULL | ((ULONG64)(tjmax - fake_temp) << 16);
    *got = 1;
    return S_OK;
}

// 不用真的驅動就能跑 update()
static HWND setup_app(void)
{
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, GetModuleHandleW(NULL), NULL);
    p_exec = fake_exec;
    pio = (HANDLE)1;
    tjmax = 100;
    ncores = 2;
    core_mask[0] = core_mask[1] = 1;
    show_usage = 0;
    nid.hWnd = hwnd;
    InitializeCriticalSection(&fan_lock);
    init_icon_canvas(16);
    return hwnd;
}

static void test_tooltip_sent_when_changed(HWND hwnd)
{
    wchar_t title[128];
    fake_temp = 50;
    update(1);
    int sent = notify_calls;
    update(0);
    update(0);
    check("same tooltip is not sent again", notify_calls == sent);
    fake_temp = 51;
    update(0);
    check("changed tooltip is sent once", notify_calls == sent + 1);
    GetWindowTextW(hwnd, title, 128);
    check("window title mirrors the tooltip", !wcscmp(title, nid.szTip));
    update(1);
    check("forced update is sent", notify_calls == sent + 2);
}

// 安裝後由 smoke test 執行：路徑要真的載入得到 PawnIO。程式本身在 AMD 的 CI runner 上不會載入它，這裡和 CPU 廠牌無關
static void test_pawnio_installed(void)
{
    wchar_t path[MAX_PATH];
    check("path found", pawnio_dll_path(path, MAX_PATH));
    HMODULE lib = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    check("PawnIOLib.dll loads", lib != NULL);
    check("PawnIOLib.dll has pawnio_open", lib && GetProcAddress(lib, "pawnio_open"));
}

int main(int argc, char **argv)
{
    if (argc > 1) test_pawnio_installed(); // test_app.exe installed
    else {
        test_pawnio_path();
        test_query_fans();
        HWND hwnd = setup_app();
        test_tooltip_sent_when_changed(hwnd);
    }
    if (!fails) puts("all app tests passed");
    return fails != 0;
}
