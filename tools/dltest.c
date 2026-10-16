#include <windows.h>
#include <stdio.h>
int main(void) {
    const char *names[] = {
        "api-ms-win-crt-stdio-l1-1-0.dll",
        "api-ms-win-crt-runtime-l1-1-0.dll",
        "api-ms-win-crt-heap-l1-1-0.dll",
        "VCRUNTIME140.dll",
        "libusb-1.0.dll",
        NULL
    };
    for (int i = 0; names[i]; i++) {
        SetErrorMode(SEM_FAILCRITICALERRORS);
        HMODULE h = LoadLibraryA(names[i]);
        DWORD e = GetLastError();
        printf("%-38s %s  (err=%lu)\n", names[i], h ? "OK" : "FAIL", h ? 0 : e);
        if (h) FreeLibrary(h);
    }
    return 0;
}
