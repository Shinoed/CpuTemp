// fan.h - 風扇轉速換算與提示文字格式 (獨立出來以便測試)
#pragma once
#include <wchar.h>

#define MAX_FANS 4

// DCIM_NumericSensor 的讀值 = CurrentReading × 10^UnitModifier；讀值不合理時回傳 -1
static int fan_rpm(long reading, long modifier)
{
    if (reading < 0 || modifier < -6 || modifier > 6) return -1;
    long long v = reading;
    for (; modifier > 0; modifier--) v *= 10;
    for (; modifier < 0; modifier++) v /= 10;
    return v > 100000 ? -1 : (int)v;
}

// 工作列圖示的文字：以千 RPM 為單位、一位小數 ("2.7")，1 萬以上取整數 ("12")；停止 "0"、讀不到 "--"
static void fan_icon_text(wchar_t *buf, int rpm)
{
    if (rpm < 0) { wcscpy(buf, L"--"); return; }
    if (rpm == 0) { wcscpy(buf, L"0"); return; }
    int tenths = (rpm + 50) / 100;
    if (tenths >= 100) _snwprintf(buf, 4, L"%d", tenths / 10 > 99 ? 99 : tenths / 10);
    else _snwprintf(buf, 4, L"%d.%d", tenths / 10, tenths % 10);
}

// 多個風扇時圖示顯示轉最快的那個；全部讀不到時回傳 -1
static int fan_max(const int *rpm, int count)
{
    int m = -1;
    for (int i = 0; i < count; i++) if (rpm[i] > m) m = rpm[i];
    return m;
}

// "風扇 2345 RPM" 或 "風扇 2345 / 0 RPM"；讀值無效的風扇顯示 "?"；沒有風扇時寫入空字串
static int fan_format(wchar_t *buf, int size, const int *rpm, int count)
{
    int n = 0;
    buf[0] = 0;
    for (int i = 0; i < count && n >= 0 && n < size; i++) {
        int r;
        if (rpm[i] < 0) r = _snwprintf(buf + n, size - n, L"%ls?", i ? L" / " : L"風扇 ");
        else r = _snwprintf(buf + n, size - n, L"%ls%d", i ? L" / " : L"風扇 ", rpm[i]);
        n = r < 0 ? -1 : n + r;
    }
    if (count > 0 && n >= 0 && n < size) {
        int r = _snwprintf(buf + n, size - n, L" RPM");
        n = r < 0 ? -1 : n + r;
    }
    if (n < 0 || n >= size) { buf[0] = 0; return 0; } // 放不下就整段不顯示，避免半截文字
    return n;
}
