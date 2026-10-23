// test_icons.c - 用 cputemp.c 的繪圖程式碼畫出工作列圖示，檢查數字大小並輸出預覽圖
//   test_icons.exe [輸出資料夾]  -> icons_<尺寸>.bmp (上排深色、下排淺色工作列)
// 失敗時回傳非 0
#define wWinMain cputemp_main // 只要繪圖函式，不要程式進入點
#include "cputemp.c"
#include <stdio.h>

static int fails;

// 數字墨水的高度 (像素)，和圖示實際使用的字型一致
static int ink_height(const wchar_t *text)
{
    RECT bb;
    HFONT f = wcschr(text, L'.') ? font_dec : wcslen(text) > 2 ? font3 : font2;
    return render_text(text, f, &bb) ? bb.bottom - bb.top + 1 : 0;
}

static void save_bmp(const char *path, const DWORD *px, int w, int h)
{
    BITMAPFILEHEADER fh = { 0x4D42, sizeof fh + sizeof(BITMAPINFOHEADER) + w * h * 4, 0, 0, sizeof fh + sizeof(BITMAPINFOHEADER) };
    BITMAPINFOHEADER ih = { sizeof ih, w, -h, 1, 32, BI_RGB };
    FILE *f = fopen(path, "wb");
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&ih, sizeof ih, 1, f);
    fwrite(px, 4, w * h, f);
    fclose(f);
}

// 把 premultiplied alpha 的圖示疊到背景色上
static void blit(DWORD *dst, int dw, int x0, int y0, DWORD bg)
{
    for (int y = 0; y < icon_size; y++)
        for (int x = 0; x < icon_size; x++) {
            DWORD p = pixels[y * icon_size + x], a = p >> 24;
            DWORD out = 0;
            for (int s = 0; s < 24; s += 8)
                out |= ((((p >> s) & 0xFF) + ((bg >> s) & 0xFF) * (255 - a) / 255) & 0xFF) << s;
            dst[(y0 + y) * dw + x0 + x] = out | 0xFF000000;
        }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    static const wchar_t *texts[] = { L"49", L"100", L"30", L"2.7", L"1.8", L"10", L"0", L"--" };
    const int n = sizeof texts / sizeof *texts;

    for (int size = 16; size <= 32; size += 8) { // 100% / 150% / 200% 縮放
        icon_size = size;
        big = size * 2;
        mem_dc = CreateCompatibleDC(NULL);
        pixels = make_dib(icon_size, &dib);
        big_px = make_dib(big, &big_dib);
        SetBkMode(mem_dc, TRANSPARENT);
        SetTextColor(mem_dc, RGB(255, 255, 255));
        font2 = fit_font(L"88");
        font3 = fit_font(L"100");
        font_dec = fit_font(L"8.8");

        int two = ink_height(L"49"), dec = ink_height(L"2.7");
        printf("%2d px: \"49\" %d px tall, \"100\" %d px, \"2.7\" %d px\n", size, two, ink_height(L"100"), dec);
        if (dec * 10 < two * 9) { // 小數至少要有兩位數的 90% 高
            printf("FAIL %d px: fan text \"2.7\" (%d px) is much smaller than \"49\" (%d px)\n", size, dec, two);
            fails++;
        }

        int gap = 4, w = n * (size + gap) + gap, h = 2 * (size + gap) + gap;
        DWORD *canvas = calloc(w * h, 4);
        for (int row = 0; row < 2; row++) {
            light_theme = row;
            DWORD bg = row ? 0xF3F3F3 : 0x1C1C1C;
            for (int i = 0; i < w * (size + gap); i++) canvas[row * w * (size + gap) + i] = bg | 0xFF000000;
            for (int i = 0; i < n; i++) {
                const wchar_t *t = texts[i];
                COLORREF c = wcschr(t, L'.') || !wcscmp(t, L"0") ? fan_color(1) : i == 2 ? usage_color(30) : temp_color(_wtoi(t));
                DestroyIcon(make_icon(t, c, i == 2 ? 30 : -1));
                blit(canvas, w, gap + i * (size + gap), gap + row * (size + gap), bg);
            }
        }
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\icons_%d.bmp", dir, size);
        save_bmp(path, canvas, w, h);
        free(canvas);
    }
    if (!fails) puts("all icon tests passed");
    return fails != 0;
}
