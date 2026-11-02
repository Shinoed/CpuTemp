// test_app.c - cputemp.c 內部函式的單元測試；失敗時回傳非 0
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

int main(void)
{
    test_pawnio_path();
    if (!fails) puts("all app tests passed");
    return fails != 0;
}
