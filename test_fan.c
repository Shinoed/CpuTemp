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

static void icon(int r, const wchar_t *want)
{
    wchar_t buf[8];
    fan_icon_text(buf, r);
    if (wcscmp(buf, want)) {
        printf("FAIL fan_icon_text(%d) = \"%ls\", want \"%ls\"\n", r, buf, want);
        fails++;
    }
}

int main(void)
{
    icon(-1, L"--");
    icon(0, L"0");
    icon(30, L"0.0");              // 轉很慢但沒停，不顯示成 "0"
    icon(2728, L"2.7");
    icon(2750, L"2.8");            // 四捨五入
    icon(9949, L"9.9");
    icon(9950, L"10");
    icon(12345, L"12");
    icon(100000, L"99");

    int m1[] = { 1800, 2345 }, m2[] = { -1, -1 }, m3[] = { -1, 0 };
    if (fan_max(m1, 2) != 2345 || fan_max(m2, 2) != -1 || fan_max(m3, 2) != 0 || fan_max(m1, 0) != -1) {
        puts("FAIL fan_max");
        fails++;
    }

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
