// test_fan.c - fan_rpm() / fan_format() 的單元測試；失敗時回傳非 0
#include <stdio.h>
#include "fan.h"

static int fails;

static void rpm(long reading, long modifier, int want)
{
    int got = fan_rpm(reading, modifier);
    if (got != want) {
        printf("FAIL fan_rpm(%ld, %ld) = %d, want %d\n", reading, modifier, got, want);
        fails++;
    }
}

static void fmt(const int *r, int count, int size, const wchar_t *want)
{
    wchar_t buf[64];
    int n = fan_format(buf, size, r, count);
    if (wcscmp(buf, want) || n != (int)wcslen(want)) {
        printf("FAIL fan_format(count=%d, size=%d) = \"%ls\" (%d), want \"%ls\"\n", count, size, buf, n, want);
        fails++;
    }
}

int main(void)
{
    rpm(2345, 0, 2345);
    rpm(0, 0, 0);                  // 風扇停止
    rpm(234, 1, 2340);             // 以 10 RPM 為單位回報
    rpm(23456, -1, 2345);
    rpm(-1, 0, -1);                // 讀值錯誤
    rpm(5, 9, -1);                 // 倍率不合理
    rpm(200000, 0, -1);            // 超出合理範圍

    int one[] = { 2345 }, two[] = { 2345, 0 }, bad[] = { -1, 1800 };
    fmt(one, 0, 64, L"");
    fmt(one, 1, 64, L"風扇 2345 RPM");
    fmt(two, 2, 64, L"風扇 2345 / 0 RPM");
    fmt(bad, 2, 64, L"風扇 ? / 1800 RPM");
    fmt(one, 1, 8, L"");           // 放不下時整段不顯示
    fmt(one, 1, 11, L"");          // 11 個字剛好放不下結尾的 0
    fmt(one, 1, 12, L"風扇 2345 RPM");
    if (!fails) puts("all fan tests passed");
    return fails != 0;
}
