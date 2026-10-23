// usage.h - CPU 使用率計算 (獨立出來以便測試)
#pragma once

// di = idle 時間差, dt = 總時間差 (kernel + user，kernel 已包含 idle)
// dt 為 0 時回傳 -1；di 大於 dt (計數器不同步) 時視為 0%
static int usage_percent(unsigned long long di, unsigned long long dt)
{
    if (dt == 0) return -1;
    if (di > dt) di = dt;
    return (int)((dt - di) * 100 / dt);
}
