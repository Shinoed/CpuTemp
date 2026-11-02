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

// 換掉 WMI 連線，背景執行緒才能跑假的物件；讀取間隔也改短，測試不必等那麼久
#define COBJMACROS
#include <objbase.h>
#include <wbemidl.h>
static HRESULT fake_create(const void *c, void *o, DWORD ctx, const void *iid, void **out);
static HRESULT fake_blanket(void *u, DWORD a, DWORD b, void *c, DWORD d, DWORD e, void *f, DWORD g);
#define CoCreateInstance fake_create
#define CoSetProxyBlanket fake_blanket
#define FAN_PERIOD_MS 200
#define FAN_SLOW_MS 1000

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
static int script_pos, fake_rpm = 2000;
static volatile int fake_flip;  // 每次讀值都變，轉速就不穩定
static volatile LONG wmi_reads;

static HRESULT STDMETHODCALLTYPE obj_get(IWbemClassObject *o, LPCWSTR name, LONG flags, VARIANT *v, CIMTYPE *type, LONG *flavor)
{
    if (!wcscmp(name, L"__RELPATH")) {
        V_VT(v) = VT_BSTR;
        v->bstrVal = SysAllocString(L"fan");
    } else if (!wcscmp(name, L"CurrentReading")) {
        if (fake_flip) fake_rpm = fake_rpm == 2000 ? 3000 : 2000;
        else fake_rpm = 2000;
        V_VT(v) = VT_I4;
        v->lVal = fake_rpm;
    } else {
        V_VT(v) = VT_I4;
        v->lVal = 0;
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
    InterlockedIncrement(&wmi_reads);
    script_pos = 0;
    *out = &fake_enum;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE svc_get(IWbemServices *s, const BSTR path, LONG flags, IWbemContext *ctx, IWbemClassObject **out, IWbemCallResult **result)
{
    InterlockedIncrement(&wmi_reads);
    *out = &fake_obj;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE svc_release(IWbemServices *s) { return 0; }
static IWbemServicesVtbl svc_vtbl = { .GetObject = (void *)svc_get, .ExecQuery = (void *)svc_query, .Release = (void *)svc_release };
static IWbemServices fake_svc = { &svc_vtbl };

static HRESULT STDMETHODCALLTYPE loc_connect(IWbemLocator *l, BSTR a, BSTR b, BSTR c, BSTR d, LONG e, BSTR f, IWbemContext *g, IWbemServices **out)
{
    *out = &fake_svc;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE loc_release(IWbemLocator *l) { return 0; }
static IWbemLocatorVtbl loc_vtbl = { .ConnectServer = (void *)loc_connect, .Release = (void *)loc_release };
static IWbemLocator fake_loc = { &loc_vtbl };

static HRESULT fake_create(const void *c, void *o, DWORD ctx, const void *iid, void **out)
{
    *out = &fake_loc;
    return S_OK;
}

static HRESULT fake_blanket(void *u, DWORD a, DWORD b, void *c, DWORD d, DWORD e, void *f, DWORD g) { return S_OK; }

static void query(const char *name, HRESULT a, HRESULT b, HRESULT c, int want, int want_paths)
{
    int rpm[MAX_FANS];
    script[0] = a;
    script[1] = b;
    script[2] = c;
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
static int fake_temp = 50, core_reads;

static HRESULT WINAPI fake_exec(HANDLE h, PCSTR name, const ULONG64 *in, SIZE_T in_n, PULONG64 out, SIZE_T out_n, PSIZE_T got)
{
    if (in[0] == 0x19C) core_reads++;
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

static void test_cores_read_on_hover(HWND hwnd)
{
    hover_until = 0;
    core_reads = 0;
    update(0);
    update(1);
    check("no core reads without hover", core_reads == 0);
    check("no core lines without hover", !wcsstr(nid.szTip, L"核心"));

    wnd_proc(hwnd, WM_TRAY, 1, WM_MOUSEMOVE);
    check("hovering another icon does not read cores", core_reads == 0);

    wnd_proc(hwnd, WM_TRAY, 0, WM_MOUSEMOVE);
    check("hover reads cores at once", core_reads == ncores);
    check("hover shows core lines", wcsstr(nid.szTip, L"核心") != NULL);

    wnd_proc(hwnd, WM_TRAY, 0, WM_MOUSEMOVE);
    check("moving again does not read again", core_reads == ncores);
    update(0);
    check("updates during hover read cores", core_reads == 2 * ncores);

    hover_until = 1; // 已過期
    update(0);
    check("no core reads after hover", core_reads == 2 * ncores);
    check("core lines go away after hover", !wcsstr(nid.szTip, L"核心"));
}

static void test_kick_fans(void)
{
    fan_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    fan_state = 1;
    fan_rpms[0] = 2000;
    fake_temp = 50;
    update(0);
    fake_temp = 52;
    update(0);
    check("small rise does not kick", WaitForSingleObject(fan_wake, 0) == WAIT_TIMEOUT);
    fake_temp = 53;
    update(0);
    check("rise of 3 degrees kicks", WaitForSingleObject(fan_wake, 0) == WAIT_OBJECT_0);
    fake_temp = 50;
    update(0);
    check("falling does not kick", WaitForSingleObject(fan_wake, 0) == WAIT_TIMEOUT);
    fake_temp = 53;
    update(0);
    check("rise from the new low kicks", WaitForSingleObject(fan_wake, 0) == WAIT_OBJECT_0);
    fan_state = -1;
    fake_temp = 70;
    update(0);
    check("no kick without fans", WaitForSingleObject(fan_wake, 0) == WAIT_TIMEOUT);
    CloseHandle(fan_wake);
}

static int reads_during(int ms)
{
    LONG before = wmi_reads;
    Sleep(ms);
    return wmi_reads - before;
}

// 跑真正的背景執行緒；間隔在這個檔案開頭改成 200 / 1000 ms
static void test_fan_worker(HWND hwnd)
{
    script[0] = WBEM_S_NO_ERROR;
    script[1] = WBEM_S_FALSE;
    DeleteCriticalSection(&fan_lock);

    fake_flip = 1;
    start_fans(hwnd);
    check("changing fans are read at the short interval", reads_during(1000) >= 3);

    fake_flip = 0;
    Sleep(500);
    check("stable fans are read at the long interval", reads_during(1500) <= 2);

    LONG before = wmi_reads;
    ULONGLONG end = GetTickCount64() + 1000;
    while (GetTickCount64() < end) {
        SetEvent(fan_wake);
        Sleep(10);
    }
    int kicked = wmi_reads - before;
    check("kicks read early", kicked >= 3);
    check("kicks keep the minimum interval", kicked <= 7);

    stop_fans();
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
        test_cores_read_on_hover(hwnd);
        test_kick_fans();
        test_fan_worker(hwnd);
    }
    if (!fails) puts("all app tests passed");
    return fails != 0;
}
