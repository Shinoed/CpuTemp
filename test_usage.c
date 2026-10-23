// test_usage.c - usage_percent() 的單元測試；失敗時回傳非 0
#include <stdio.h>
#include "usage.h"

static int fails;

static void check(unsigned long long di, unsigned long long dt, int want)
{
    int got = usage_percent(di, dt);
    if (got != want) {
        printf("FAIL usage_percent(%llu, %llu) = %d, want %d\n", di, dt, got, want);
        fails++;
    }
}

int main(void)
{
    check(0, 0, -1);            // 兩次取樣間沒有經過時間
    check(100, 100, 0);         // 完全閒置
    check(0, 100, 100);         // 滿載
    check(25, 100, 75);
    check(1, 3, 66);            // 無條件捨去
    check(150, 100, 0);         // idle 大於總時間不可下溢
    check(5000000000ULL, 10000000000ULL, 50); // 大數值不可溢位
    if (!fails) puts("all usage tests passed");
    return fails != 0;
}
