// cputemp.c - 原生 Win32 CPU 溫度工作列工具 (Intel)
// 透過 PawnIO 驅動 + IntelMSR 模組 (內嵌於 exe 資源) 直接讀 MSR：
//   IA32_TEMPERATURE_TARGET (0x1A2) bits 23:16 = TjMax
//   IA32_PACKAGE_THERM_STATUS (0x1B1) bits 22:16 = 封裝距 TjMax 的度數
//   IA32_THERM_STATUS (0x19C)         bits 22:16 = 各核心距 TjMax 的度數 (需切到該核心讀)
// 另有第二個圖示顯示 CPU 使用率 (GetSystemTimes)，可從右鍵選單關閉
// 有裝 Dell Command | Monitor 時，提示文字加上風扇轉速 (WMI root\dcim\sysman)
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <imm.h>
#include <wchar.h>
#define COBJMACROS
#include <objbase.h>
#include <wbemidl.h>
#include "usage.h"
#include "fan.h"

#define WM_TRAY      (WM_APP + 1)
#define ID_TIMER     1
#define ID_EXIT      100
#define ID_USAGE     101
#define ID_FAN       102
#define REG_KEY      L"Software\\CpuTemp"
#define ID_INTERVAL  200   // 200 + 秒數
static const int intervals[] = { 1, 2, 5, 10 }; // 選單上的更新頻率 (秒)；只接受這幾個值

static int valid_interval(int s)
{
    for (int i = 0; i < (int)(sizeof intervals / sizeof *intervals); i++)
        if (intervals[i] == s) return 1;
    return 0;
}
#define RES_MODULE   1
#define MAX_CORES    64

typedef HRESULT (WINAPI *open_fn)(PHANDLE);
typedef HRESULT (WINAPI *load_fn)(HANDLE, const UCHAR *, SIZE_T);
typedef HRESULT (WINAPI *exec_fn)(HANDLE, PCSTR, const ULONG64 *, SIZE_T, PULONG64, SIZE_T, PSIZE_T);
typedef HRESULT (WINAPI *close_fn)(HANDLE);

static exec_fn  p_exec;
static close_fn p_close;
static HANDLE   pio;
static int      tjmax;
static const wchar_t *err_msg = L"";

static KAFFINITY core_mask[MAX_CORES];
static int      ncores;

static NOTIFYICONDATAW nid, nid_use, nid_fan;  // 溫度 / CPU 使用率 / 風扇轉速圖示
static UINT     wm_taskbar_created;
static int      icon_size, interval_sec = 2, last_shown = -9999, light_theme;
static int      show_usage = 1, last_usage = -9999;
static int      show_fan = 1, fan_icon_on;
static wchar_t  fan_txt[8], fan_tip[64];   // 風扇圖示目前顯示的文字和提示，沒變就不重畫
static ULONGLONG prev_idle, prev_total;
static HICON    cur_icon, cur_icon_use, cur_icon_fan;
static HFONT    font2, font3, font_dec; // 兩位數 / 三位數 / 一位小數 (風扇 "2.7") 用
static HDC      mem_dc;
static HBITMAP  dib, big_dib;
static DWORD   *pixels, *big_px;       // big = 2 倍大的草稿畫布，用來量字形實際範圍
static int      big;

static int read_msr(ULONG msr, ULONG64 *out)
{
    ULONG64 in = msr;
    SIZE_T n = 0;
    return SUCCEEDED(p_exec(pio, "ioctl_read_msr", &in, 1, out, 1, &n)) && n == 1;
}

static void init_cores(void)
{
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION info[256];
    DWORD len = sizeof info;
    if (!GetLogicalProcessorInformation(info, &len)) return;
    for (DWORD i = 0; i < len / sizeof *info && ncores < MAX_CORES; i++)
        if (info[i].Relationship == RelationProcessorCore)
            core_mask[ncores++] = info[i].ProcessorMask; // 每個實體核心一個遮罩 (超執行緒共用)
}

static int init_sensor(void)
{
    int r[4];
    __asm__("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(0));
    if (r[1] != 0x756e6547) { err_msg = L"目前只支援 Intel CPU"; return 0; } // "Genu"

    wchar_t path[MAX_PATH];
    ExpandEnvironmentStringsW(L"%ProgramFiles%\\PawnIO\\PawnIOLib.dll", path, MAX_PATH);
    HMODULE lib = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!lib) { err_msg = L"找不到 PawnIO，請先安裝"; return 0; }
    open_fn p_open = (open_fn)GetProcAddress(lib, "pawnio_open");
    load_fn p_load = (load_fn)GetProcAddress(lib, "pawnio_load");
    p_exec  = (exec_fn)GetProcAddress(lib, "pawnio_execute");
    p_close = (close_fn)GetProcAddress(lib, "pawnio_close");
    if (!p_open || !p_load || !p_exec || !p_close) { err_msg = L"PawnIOLib 版本不符"; return 0; }

    if (FAILED(p_open(&pio))) { err_msg = L"無法開啟 PawnIO (需系統管理員)"; return 0; }

    HRSRC res = FindResourceW(NULL, MAKEINTRESOURCEW(RES_MODULE), (LPCWSTR)RT_RCDATA);
    const UCHAR *blob = LockResource(LoadResource(NULL, res));
    if (!blob || FAILED(p_load(pio, blob, SizeofResource(NULL, res)))) { err_msg = L"載入 IntelMSR 模組失敗"; return 0; }

    ULONG64 v;
    if (!read_msr(0x1A2, &v)) { err_msg = L"讀取 TjMax 失敗"; return 0; }
    tjmax = (int)((v >> 16) & 0xFF);
    if (tjmax < 60 || tjmax > 130) tjmax = 100;
    init_cores();
    return 1;
}

static int msr_temp(ULONG msr)
{
    ULONG64 v;
    if (!read_msr(msr, &v) || !(v & 0x80000000ULL)) return -1; // bit 31 = 讀值有效
    return tjmax - (int)((v >> 16) & 0x7F);
}

static int read_temp(void) { return pio ? msr_temp(0x1B1) : -1; }

// 逐核心讀取：把本執行緒暫時綁到該核心，讀完還原
static void read_cores(int *out)
{
    HANDLE th = GetCurrentThread();
    KAFFINITY orig = 0;
    for (int i = 0; i < ncores; i++) {
        KAFFINITY prev = SetThreadAffinityMask(th, core_mask[i]);
        if (i == 0) orig = prev;
        out[i] = prev ? msr_temp(0x19C) : -1;
    }
    if (orig) SetThreadAffinityMask(th, orig);
}

static ULONGLONG ft64(FILETIME f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

// 兩次取樣之間的 CPU 使用率 (%)；kernel 時間已包含 idle
// 距上次取樣不到 500 ms 時沿用上次結果 (啟動、主題切換時會連續呼叫，間隔太短算不準)
static int read_usage(void)
{
    static int last = -1;
    static ULONGLONG last_tick;
    ULONGLONG now = GetTickCount64();
    if (prev_total && now - last_tick < 500) return last;

    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return -1;
    ULONGLONG i = ft64(idle), t = ft64(kernel) + ft64(user);
    ULONGLONG di = i - prev_idle, dt = t - prev_total;
    int first = prev_total == 0;
    prev_idle = i;
    prev_total = t;
    last_tick = now;
    return last = first ? -1 : usage_percent(di, dt);
}

// 風扇轉速：Dell Command | Monitor 提供的 DCIM_NumericSensor (SensorType 5 = 風扇)
// WMI 查詢跨行程、可能很慢，放在背景執行緒做，UI 執行緒只讀最新結果 (不會卡住選單，也不會在
// STA 的 COM 呼叫中被重入)。啟動時或斷線後 3 分鐘內每 15 秒重試：登入時 WMI 可能還沒好，
// WMI 服務重啟後也能自動接回；之後改成每分鐘一次 (沒有 DCM 時連線立刻失敗，幾乎不花資源)
#define WM_FANS        (WM_APP + 2)   // 背景執行緒通知 UI：風扇狀態變了
#define FAN_RETRY_MS   15000
#define FAN_SEARCH_MS  (3 * 60 * 1000)
#define FAN_IDLE_MS    60000

static CRITICAL_SECTION fan_lock;
static int      fan_state = -1;        // -1 = 沒有風扇感應器、0 = 曾經有但暫時讀不到、>0 = 風扇數
static int      fan_rpms[MAX_FANS];    // fan_state > 0 時有效；以上兩個都受 fan_lock 保護
static HANDLE   fan_thread, fan_stop;
static volatile LONG fan_period_ms = 2000;
static HWND     main_hwnd;

static IWbemServices *wmi_connect(void)
{
    IWbemLocator *loc = NULL;
    IWbemServices *svc = NULL;
    if (FAILED(CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (void **)&loc)))
        return NULL;
    BSTR ns = SysAllocString(L"ROOT\\DCIM\\SYSMAN");
    if (FAILED(IWbemLocator_ConnectServer(loc, ns, NULL, NULL, NULL, 0, NULL, NULL, &svc))) svc = NULL;
    // DCM 的提供者要模擬呼叫端身分，不然列舉時回 WBEM_E_ACCESS_DENIED。CoInitializeSecurity 是整個行程
    // 只能設一次的預設值，若別處已先初始化 COM 安全性就不生效 (實機上就是讀不到風扇)，所以直接設在這條連線上
    if (svc && FAILED(CoSetProxyBlanket((IUnknown *)svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL,
                                        RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE))) {
        IWbemServices_Release(svc);
        svc = NULL;
    }
    SysFreeString(ns);
    IWbemLocator_Release(loc);
    return svc;
}

// 回傳風扇數 (0 = 沒有風扇)，查詢失敗 (連線斷了、類別不存在) 回傳 -1
static int read_fans(IWbemServices *svc, int *rpm)
{
    IEnumWbemClassObject *e = NULL;
    BSTR lang = SysAllocString(L"WQL");
    BSTR q = SysAllocString(L"SELECT CurrentReading, UnitModifier FROM DCIM_NumericSensor WHERE SensorType = 5");
    HRESULT hr = IWbemServices_ExecQuery(svc, lang, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &e);
    SysFreeString(lang);
    SysFreeString(q);
    if (FAILED(hr)) return -1;

    int n = 0;
    IWbemClassObject *o;
    ULONG got;
    while (n < MAX_FANS && (hr = IEnumWbemClassObject_Next(e, 2000, 1, &o, &got)) == WBEM_S_NO_ERROR && got) {
        VARIANT r, m;
        VariantInit(&r);
        VariantInit(&m);
        long reading = -1, mod = 0;
        if (SUCCEEDED(IWbemClassObject_Get(o, L"CurrentReading", 0, &r, NULL, NULL)) &&
            SUCCEEDED(VariantChangeType(&r, &r, 0, VT_I4))) reading = r.lVal;
        if (SUCCEEDED(IWbemClassObject_Get(o, L"UnitModifier", 0, &m, NULL, NULL)) &&
            SUCCEEDED(VariantChangeType(&m, &m, 0, VT_I4))) mod = m.lVal;
        VariantClear(&r);
        VariantClear(&m);
        IWbemClassObject_Release(o);
        rpm[n++] = fan_rpm(reading, mod);
    }
    IEnumWbemClassObject_Release(e);
    return FAILED(hr) ? -1 : n;
}

static void publish_fans(int state, const int *rpm)
{
    EnterCriticalSection(&fan_lock);
    int changed = state != fan_state;
    for (int i = 0; i < state; i++) {
        changed |= fan_rpms[i] != rpm[i];
        fan_rpms[i] = rpm[i];
    }
    fan_state = state;
    LeaveCriticalSection(&fan_lock);
    if (changed) PostMessageW(main_hwnd, WM_FANS, 0, 0);
}

static DWORD WINAPI fan_worker(void *arg)
{
    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) return 0;
    CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);

    IWbemServices *svc = NULL;
    int had_fans = 0;
    ULONGLONG give_up = GetTickCount64() + FAN_SEARCH_MS;
    DWORD wait = 0;
    while (WaitForSingleObject(fan_stop, wait) == WAIT_TIMEOUT) {
        int rpm[MAX_FANS], n = -1;
        if (!svc) svc = wmi_connect();
        if (svc && (n = read_fans(svc, rpm)) < 0) { // 連線斷了 (例如 WMI 服務重啟)：丟掉，下一輪重連
            IWbemServices_Release(svc);
            svc = NULL;
        }
        if (n > 0) {
            had_fans = 1;
            give_up = GetTickCount64() + FAN_SEARCH_MS;
            publish_fans(n, rpm);
            wait = (DWORD)fan_period_ms;
            continue;
        }
        if (GetTickCount64() < give_up) {
            if (had_fans) publish_fans(0, rpm); // 剛讀不到：圖示留著顯示 "--"，很快重試
            wait = FAN_RETRY_MS;
        } else {
            publish_fans(-1, NULL);             // 很久都沒有風扇：拿掉圖示，偶爾看一下 (之後才裝 DCM 也能出現)
            wait = FAN_IDLE_MS;
        }
    }
    if (svc) IWbemServices_Release(svc);
    CoUninitialize();
    publish_fans(-1, NULL);
    return 0;
}

static void start_fans(HWND hwnd)
{
    main_hwnd = hwnd;
    InitializeCriticalSection(&fan_lock);
    fan_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    // 只做 WMI 查詢，不需要預設的 1 MB 堆疊
    fan_thread = CreateThread(NULL, 256 * 1024, fan_worker, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
}

static void stop_fans(void)
{
    if (!fan_thread) return;
    SetEvent(fan_stop);
    WaitForSingleObject(fan_thread, 5000); // 查詢中最多等 Next 的 2 秒逾時
    CloseHandle(fan_thread);
    fan_thread = NULL;
}

// 複製最新的風扇狀態 (UI 執行緒用)
static int get_fans(int *rpm)
{
    EnterCriticalSection(&fan_lock);
    int state = fan_state;
    for (int i = 0; i < state; i++) rpm[i] = fan_rpms[i];
    LeaveCriticalSection(&fan_lock);
    return state;
}

static int reg_get(const wchar_t *name, int def)
{
    DWORD v, sz = sizeof v;
    return RegGetValueW(HKEY_CURRENT_USER, REG_KEY, name, RRF_RT_REG_DWORD, NULL, &v, &sz) == ERROR_SUCCESS ? (int)v : def;
}

static void reg_set(const wchar_t *name, int v)
{
    DWORD d = (DWORD)v;
    RegSetKeyValueW(HKEY_CURRENT_USER, REG_KEY, name, REG_DWORD, &d, sizeof d);
}

static void read_theme(void)
{
    DWORD v = 0, sz = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz);
    light_theme = v != 0;
}

static HFONT make_font(int h, const wchar_t *face)
{
    return CreateFontW(-h, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                       ANTIALIASED_QUALITY, 0, face);
}

// 把文字畫到草稿畫布 (黑底白字)，回傳實際有墨水的範圍
static int render_text(const wchar_t *text, HFONT f, RECT *bb)
{
    for (int i = 0; i < big * big; i++) big_px[i] = 0;
    SelectObject(mem_dc, big_dib);
    SelectObject(mem_dc, f);
    RECT rc = { 0, 0, big, big };
    DrawTextW(mem_dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    GdiFlush();
    bb->left = bb->top = big; bb->right = bb->bottom = -1;
    for (int y = 0; y < big; y++)
        for (int x = 0; x < big; x++)
            if (big_px[y * big + x] & 0xFF) {
                if (x < bb->left) bb->left = x;
                if (x > bb->right) bb->right = x;
                if (y < bb->top) bb->top = y;
                if (y > bb->bottom) bb->bottom = y;
            }
    return bb->right >= 0;
}

// 找出能塞進圖示的最大字級 (只在啟動時做一次)
// Bahnschrift 是窄體數字，小尺寸也清楚；三個字以上改用更窄的 Condensed，字才不會縮太小
static HFONT fit_font(const wchar_t *sample, const wchar_t *face)
{
    for (int h = icon_size * 3 / 2; h > 6; h--) {
        HFONT f = make_font(h, face);
        RECT bb;
        if (render_text(sample, f, &bb) && bb.right - bb.left + 1 <= icon_size
            && bb.bottom - bb.top + 1 <= icon_size * 88 / 100)
            return f;
        DeleteObject(f);
    }
    return make_font(icon_size / 2, face);
}

static DWORD *make_dib(int w, HBITMAP *out)
{
    BITMAPINFO bi = { .bmiHeader = { sizeof(BITMAPINFOHEADER), w, -w, 1, 32, BI_RGB } };
    DWORD *p;
    *out = CreateDIBSection(mem_dc, &bi, DIB_RGB_COLORS, (void **)&p, NULL, 0);
    return p;
}

// size = 圖示邊長；已宣告 DPI aware，SM_CXSMICON 在 100%=16、150%=24、200%=32 (test_icons.c 會直接指定)
static void init_icon_canvas(int size)
{
    icon_size = size;
    big = icon_size * 2;
    mem_dc = CreateCompatibleDC(NULL);
    pixels = make_dib(icon_size, &dib);
    big_px = make_dib(big, &big_dib);
    SetBkMode(mem_dc, TRANSPARENT);
    SetTextColor(mem_dc, RGB(255, 255, 255));
    font2 = fit_font(L"88", L"Bahnschrift");
    font3 = fit_font(L"100", L"Bahnschrift Condensed");
    font_dec = fit_font(L"8.8", L"Bahnschrift Condensed");
}

// 量出字形實際範圍後置中貼到圖示，亮度轉成 alpha，得到反鋸齒的透明圖示
// bar >= 0：在底部畫一條 0–100% 的負載條 (使用率圖示用，也用來和溫度圖示區分)
static HICON make_icon(const wchar_t *text, COLORREF color, int bar)
{
    RECT bb;
    int bar_h = bar >= 0 ? (icon_size >= 24 ? 3 : 2) : 0;
    DWORD r = GetRValue(color), g = GetGValue(color), b = GetBValue(color);
    for (int i = 0; i < icon_size * icon_size; i++) pixels[i] = 0;
    if (bar_h) {
        int fill = icon_size * bar / 100;
        for (int y = icon_size - bar_h; y < icon_size; y++)
            for (int x = 0; x < icon_size; x++) {
                DWORD a = x < fill ? 255 : 70; // 未填滿的部分畫成淡色軌道
                pixels[y * icon_size + x] = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
            }
    }
    HFONT f = wcschr(text, L'.') ? font_dec : wcslen(text) > 2 ? font3 : font2;
    if (render_text(text, f, &bb)) {
        int w = bb.right - bb.left + 1, h = bb.bottom - bb.top + 1;
        int ox = (icon_size - w + 1) / 2 - bb.left, oy = (icon_size - bar_h - h + 1) / 2 - bb.top;
        for (int y = bb.top; y <= bb.bottom; y++)
            for (int x = bb.left; x <= bb.right; x++) {
                int dx = x + ox, dy = y + oy;
                if (dx < 0 || dy < 0 || dx >= icon_size || dy >= icon_size) continue;
                DWORD a = big_px[y * big + x] & 0xFF;
                pixels[dy * icon_size + dx] = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
            }
    }
    HBITMAP mask = CreateBitmap(icon_size, icon_size, 1, 1, NULL);
    ICONINFO ii = { TRUE, 0, 0, mask, dib };
    HICON ic = CreateIconIndirect(&ii); // 會複製點陣圖，dib 可以重複使用
    DeleteObject(mask);
    return ic;
}

static COLORREF temp_color(int t)
{
    if (t < 0)   return RGB(140, 140, 140);
    if (t >= 85) return light_theme ? RGB(200, 30, 30)  : RGB(255, 90, 90);
    if (t >= 70) return light_theme ? RGB(190, 110, 0)  : RGB(255, 200, 60);
    return light_theme ? RGB(20, 20, 20) : RGB(255, 255, 255);
}

static COLORREF usage_color(int u)
{
    if (u < 0)   return RGB(140, 140, 140);
    if (u >= 90) return light_theme ? RGB(200, 30, 30) : RGB(255, 90, 90);
    if (u >= 70) return light_theme ? RGB(190, 110, 0) : RGB(255, 200, 60);
    return light_theme ? RGB(0, 95, 184) : RGB(110, 200, 255); // 藍色，和溫度的白/黑色區分
}

static void update_usage(int force)
{
    int u = read_usage(); // 每次都要取樣，才能算出這段期間的使用率
    if (!show_usage || (u == last_usage && !force)) return;
    last_usage = u;

    wchar_t txt[8];
    if (u < 0) wcscpy(txt, L"--");
    else _snwprintf(txt, 8, L"%d", u);
    HICON old = cur_icon_use;
    cur_icon_use = make_icon(txt, usage_color(u), u < 0 ? 0 : u);
    nid_use.hIcon = cur_icon_use;
    if (u < 0) wcscpy(nid_use.szTip, L"CPU 使用率");
    else _snwprintf(nid_use.szTip, 128, L"CPU 使用率 %d%%", u);
    nid_use.uFlags = NIF_ICON | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid_use);
    if (old) DestroyIcon(old);
}

static void add_usage_icon(void)
{
    nid_use.uFlags = NIF_TIP | NIF_MESSAGE;
    Shell_NotifyIconW(NIM_ADD, &nid_use);
    update_usage(1);
}

// 綠色，和溫度的白/黑、使用率的藍區分；轉速沒有警戒值，讀不到時灰色
static COLORREF fan_color(int rpm)
{
    if (rpm < 0) return RGB(140, 140, 140);
    return light_theme ? RGB(0, 125, 90) : RGB(90, 225, 170);
}

// 依最新的風扇狀態加入、移除或更新風扇圖示；顯示的文字和提示都沒變就不重畫
static void update_fan(int force)
{
    int rpm[MAX_FANS], state = get_fans(rpm);
    if (!show_fan || state < 0) {
        if (fan_icon_on) Shell_NotifyIconW(NIM_DELETE, &nid_fan);
        fan_icon_on = 0;
        return;
    }
    if (!fan_icon_on) {
        nid_fan.uFlags = NIF_TIP | NIF_MESSAGE;
        Shell_NotifyIconW(NIM_ADD, &nid_fan);
        fan_icon_on = force = 1;
    }

    int top = fan_max(rpm, state);
    wchar_t txt[8], tip[64];
    fan_icon_text(txt, top);
    if (state == 0 || !fan_format(tip, 64, rpm, state)) wcscpy(tip, L"風扇轉速讀取失敗");
    if (!force && !wcscmp(txt, fan_txt) && !wcscmp(tip, fan_tip)) return;
    wcscpy(fan_txt, txt);
    wcscpy(fan_tip, tip);

    HICON old = cur_icon_fan;
    cur_icon_fan = make_icon(txt, fan_color(top), -1);
    nid_fan.hIcon = cur_icon_fan;
    wcscpy(nid_fan.szTip, tip);
    nid_fan.uFlags = NIF_ICON | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid_fan);
    if (old) DestroyIcon(old);
}

static void update(int force)
{
    update_usage(force);
    update_fan(force);

    int t = read_temp();
    int cores[MAX_CORES];
    if (pio && ncores) read_cores(cores);

    wchar_t fan[40] = L"";
    int rpm[MAX_FANS], nfans = get_fans(rpm);
    int fan_len = nfans > 0 ? fan_format(fan + 1, 39, rpm, nfans) : 0;
    if (fan_len) fan[0] = L'\n', fan_len++;

    // 提示文字每次都更新 (核心溫度會變)，圖示只有封裝溫度變了才重畫；風扇那行留在最後
    int n;
    if (t < 0) n = _snwprintf(nid.szTip, 128, L"CPU 溫度讀取失敗\n%ls", err_msg);
    else {
        n = _snwprintf(nid.szTip, 128, L"CPU 封裝 %d °C  (TjMax %d)", t, tjmax);
        for (int i = 0; i < ncores && n > 0 && n < 110 - fan_len; i++)
            n += _snwprintf(nid.szTip + n, 128 - n, L"%ls核心%d: %d°C", i % 2 ? L"   " : L"\n", i, cores[i]);
    }
    if (n > 0 && n + fan_len < 128) wcscpy(nid.szTip + n, fan);
    nid.szTip[127] = 0;
    SetWindowTextW(nid.hWnd, nid.szTip); // 隱藏視窗的標題 = 提示文字，讓自動測試讀得到
    nid.uFlags = NIF_TIP;

    if (t != last_shown || force) {
        last_shown = t;
        wchar_t txt[8];
        if (t < 0) wcscpy(txt, L"--");
        else _snwprintf(txt, 8, L"%d", t);
        HICON old = cur_icon;
        cur_icon = make_icon(txt, temp_color(t), -1);
        nid.hIcon = cur_icon;
        nid.uFlags |= NIF_ICON;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
        if (old) DestroyIcon(old);
    } else {
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

static void show_menu(HWND hwnd)
{
    HMENU m = CreatePopupMenu();
    for (int i = 0; i < (int)(sizeof intervals / sizeof *intervals); i++) {
        wchar_t s[32];
        _snwprintf(s, 32, L"每 %d 秒更新", intervals[i]);
        AppendMenuW(m, MF_STRING | (intervals[i] == interval_sec ? MF_CHECKED : 0), ID_INTERVAL + intervals[i], s);
    }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (show_usage ? MF_CHECKED : 0), ID_USAGE, L"顯示 CPU 使用率");
    int rpm[MAX_FANS];
    if (get_fans(rpm) >= 0) AppendMenuW(m, MF_STRING | (show_fan ? MF_CHECKED : 0), ID_FAN, L"顯示風扇轉速");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"結束");
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, NULL);
    DestroyMenu(m);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        update(0);
        return 0;
    case WM_FANS:
        update_fan(0);
        return 0;
    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) show_menu(hwnd);
        return 0;
    case WM_SETTINGCHANGE: // 切換淺色/深色工作列時換字色
        if (lp && !lstrcmpW((LPCWSTR)lp, L"ImmersiveColorSet")) { read_theme(); update(1); }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_EXIT) { DestroyWindow(hwnd); return 0; }
        if (LOWORD(wp) == ID_USAGE) {
            show_usage = !show_usage;
            reg_set(L"ShowUsage", show_usage);
            if (show_usage) add_usage_icon();
            else Shell_NotifyIconW(NIM_DELETE, &nid_use);
            return 0;
        }
        if (LOWORD(wp) == ID_FAN) {
            int rpm[MAX_FANS];
            if (get_fans(rpm) < 0) return 0; // 沒有風扇時選單不會有這項
            show_fan = !show_fan;
            reg_set(L"ShowFan", show_fan);
            update_fan(1);
            return 0;
        }
        if (LOWORD(wp) > ID_INTERVAL && valid_interval(LOWORD(wp) - ID_INTERVAL)) {
            interval_sec = LOWORD(wp) - ID_INTERVAL;
            fan_period_ms = interval_sec * 1000;
            reg_set(L"Interval", interval_sec);
            SetTimer(hwnd, ID_TIMER, interval_sec * 1000, NULL);
        }
        return 0;
    case WM_DESTROY:
        stop_fans();
        Shell_NotifyIconW(NIM_DELETE, &nid);
        Shell_NotifyIconW(NIM_DELETE, &nid_use);
        if (fan_icon_on) Shell_NotifyIconW(NIM_DELETE, &nid_fan);
        if (pio) p_close(pio);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == wm_taskbar_created) { // 檔案總管重啟後重新加回圖示
        nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        Shell_NotifyIconW(NIM_ADD, &nid);
        if (show_usage) add_usage_icon();
        fan_icon_on = 0;    // update 會重新加回
        update(1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    CreateMutexW(NULL, TRUE, L"CpuTempTray_Native");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    // 一般視窗會被系統掛上輸入法 (IME/TSF)，多吃約 9 MB 和 2 個執行緒；本程式不需要打字，直接關掉
    ImmDisableIME((DWORD)-1);

    // 不是 message-only 視窗，這樣才收得到 WM_SETTINGCHANGE 廣播；視窗本身不顯示
    WNDCLASSW wc = { .lpfnWndProc = wnd_proc, .hInstance = inst, .lpszClassName = L"CpuTempTray" };
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    wm_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(hwnd, wm_taskbar_created, MSGFLT_ALLOW, NULL); // 以管理員執行時仍收得到

    if (!init_sensor() && pio) { p_close(pio); pio = NULL; }
    read_theme();
    init_icon_canvas(GetSystemMetrics(SM_CXSMICON));

    nid.cbSize = sizeof nid;
    nid.hWnd = hwnd;
    nid.uFlags = NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAY;
    Shell_NotifyIconW(NIM_ADD, &nid);

    nid_use = nid;          // 同一個視窗、同一個回呼訊息，用 uID 區分
    nid_use.uID = 1;
    show_usage = reg_get(L"ShowUsage", 1);
    interval_sec = reg_get(L"Interval", 2);
    if (!valid_interval(interval_sec)) interval_sec = 2;
    read_usage();           // 第一次取樣只當基準點
    if (show_usage) add_usage_icon();

    nid_fan = nid;          // 找到風扇時才由 update_fan 加入
    nid_fan.uID = 2;
    show_fan = reg_get(L"ShowFan", 1);
    fan_period_ms = interval_sec * 1000;
    start_fans(hwnd);

    update(1);
    SetTimer(hwnd, ID_TIMER, interval_sec * 1000, NULL);
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); // 釋放啟動時的暫用記憶體

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) DispatchMessageW(&m);
    return 0;
}
