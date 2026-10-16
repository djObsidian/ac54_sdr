#include <windows.h>
#include <stdio.h>
int main(int argc, char**argv) {
    const char *guids[] = {
        "{DF4C5FDF-3E5C-4D26-AD25-AE97613A3B45}", /* ASUS */
        NULL
    };
    for (int i=0; guids[i]; i++) {
        char path[256];
        sprintf(path, "\\\\.\\%s", guids[i]);
        SetErrorMode(SEM_FAILCRITICALERRORS);
        HANDLE h = CreateFileA(path, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("%-45s FAIL err=%lu (%s)\n", path, GetLastError(),
                   GetLastError()==2?"FILE_NOT_FOUND":GetLastError()==5?"ACCESS_DENIED":"?");
        } else {
            printf("%-45s OPEN OK\n", path);
            CloseHandle(h);
        }
    }
    return 0;
}
