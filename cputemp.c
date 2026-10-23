// cputemp.c - 原生 Win32 CPU 溫度工作列工具 (Intel)
// 透過 PawnIO 驅動 + IntelMSR 模組 (內嵌於 exe 資源) 直接讀 MSR：
//   IA32_TEMPERATURE_TARGET (0x1A2) bits 23:16 = TjMax
//   IA32_PACKAGE_THERM_STATUS (0x1B1) bits 22:16 = 封裝距 TjMax 的度數
//   IA32_THERM_STATUS (0x19C)         bits 22:16 = 各核心距 TjMax 的度數 (需切到該核心讀)
// 另有第二個圖示顯示 CPU 使用率 (GetSystemTimes)，可從右鍵選單關閉
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <imm.h>
#include <wchar.h>
#include "usage.h"

#define WM_TRAY      (WM_APP + 1)
#define ID_TIMER     1
#define ID_EXIT      100
#define ID_USAGE     101
#define REG_KEY      L"Software\\CpuTemp"
#define ID_INTERVAL  200   // 200 + 秒數
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

static NOTIFYICONDATAW nid, nid_use;  // 溫度圖示 / CPU 使用率圖示
static UINT     wm_taskbar_created;
static int      icon_size, interval_sec = 2, last_shown = -9999, light_theme;
static int      show_usage = 1, last_usage = -9999;
static ULONGLONG prev_idle, prev_total;
static HICON    cur_icon, cur_icon_use;
static HFONT    font2, font3;          // 兩位數 / 三位數用
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

static HFONT make_font(int h)
{
    return CreateFontW(-h, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                       ANTIALIASED_QUALITY, 0, L"Bahnschrift"); // 窄體數字，小尺寸也清楚
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
static HFONT fit_font(const wchar_t *sample)
{
    for (int h = icon_size * 3 / 2; h > 6; h--) {
        HFONT f = make_font(h);
        RECT bb;
        if (render_text(sample, f, &bb) && bb.right - bb.left + 1 <= icon_size
            && bb.bottom - bb.top + 1 <= icon_size * 88 / 100)
            return f;
        DeleteObject(f);
    }
    return make_font(icon_size / 2);
}

static DWORD *make_dib(int w, HBITMAP *out)
{
    BITMAPINFO bi = { .bmiHeader = { sizeof(BITMAPINFOHEADER), w, -w, 1, 32, BI_RGB } };
    DWORD *p;
    *out = CreateDIBSection(mem_dc, &bi, DIB_RGB_COLORS, (void **)&p, NULL, 0);
    return p;
}

static void init_icon_canvas(void)
{
    icon_size = GetSystemMetrics(SM_CXSMICON); // 已宣告 DPI aware：100%=16、150%=24、200%=32
    big = icon_size * 2;
    mem_dc = CreateCompatibleDC(NULL);
    pixels = make_dib(icon_size, &dib);
    big_px = make_dib(big, &big_dib);
    SetBkMode(mem_dc, TRANSPARENT);
    SetTextColor(mem_dc, RGB(255, 255, 255));
    font2 = fit_font(L"88");
    font3 = fit_font(L"100");
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
    if (render_text(text, wcslen(text) > 2 ? font3 : font2, &bb)) {
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

static void update(int force)
{
    update_usage(force);

    int t = read_temp();
    int cores[MAX_CORES];
    if (pio && ncores) read_cores(cores);

    // 提示文字每次都更新 (核心溫度會變)，圖示只有封裝溫度變了才重畫
    if (t < 0) _snwprintf(nid.szTip, 128, L"CPU 溫度讀取失敗\n%ls", err_msg);
    else {
        int n = _snwprintf(nid.szTip, 128, L"CPU 封裝 %d °C  (TjMax %d)", t, tjmax);
        for (int i = 0; i < ncores && n > 0 && n < 110; i++)
            n += _snwprintf(nid.szTip + n, 128 - n, L"%ls核心%d: %d°C", i % 2 ? L"   " : L"\n", i, cores[i]);
    }
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
    static const int secs[] = { 1, 2, 5, 10 };
    for (int i = 0; i < 4; i++) {
        wchar_t s[32];
        _snwprintf(s, 32, L"每 %d 秒更新", secs[i]);
        AppendMenuW(m, MF_STRING | (secs[i] == interval_sec ? MF_CHECKED : 0), ID_INTERVAL + secs[i], s);
    }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (show_usage ? MF_CHECKED : 0), ID_USAGE, L"顯示 CPU 使用率");
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
        if (LOWORD(wp) > ID_INTERVAL && LOWORD(wp) <= ID_INTERVAL + 10) {
            interval_sec = LOWORD(wp) - ID_INTERVAL;
            reg_set(L"Interval", interval_sec);
            SetTimer(hwnd, ID_TIMER, interval_sec * 1000, NULL);
        }
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &nid);
        Shell_NotifyIconW(NIM_DELETE, &nid_use);
        if (pio) p_close(pio);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == wm_taskbar_created) { // 檔案總管重啟後重新加回圖示
        nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        Shell_NotifyIconW(NIM_ADD, &nid);
        if (show_usage) add_usage_icon();
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
    init_icon_canvas();

    nid.cbSize = sizeof nid;
    nid.hWnd = hwnd;
    nid.uFlags = NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAY;
    Shell_NotifyIconW(NIM_ADD, &nid);

    nid_use = nid;          // 同一個視窗、同一個回呼訊息，用 uID 區分
    nid_use.uID = 1;
    show_usage = reg_get(L"ShowUsage", 1);
    interval_sec = reg_get(L"Interval", 2);
    if (interval_sec != 1 && interval_sec != 5 && interval_sec != 10) interval_sec = 2; // 只接受選單上的值
    read_usage();           // 第一次取樣只當基準點
    if (show_usage) add_usage_icon();

    update(1);
    SetTimer(hwnd, ID_TIMER, interval_sec * 1000, NULL);
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); // 釋放啟動時的暫用記憶體

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) DispatchMessageW(&m);
    return 0;
}
