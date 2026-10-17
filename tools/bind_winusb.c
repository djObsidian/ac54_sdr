/* Reapply the existing Zadig driver to the ASUS adapter after USB3 enumeration.
 * Run elevated: bind_winusb.exe C:\Windows\INF\oemNN.inf
 * Pass only the installed WinUSB INF for USB\VID_0B05&PID_1841. */
#include <windows.h>
#include <newdev.h>
#include <stdio.h>
#pragma comment(lib, "newdev.lib")
int main(int argc, char **argv)
{
    BOOL reboot = FALSE;
    if (argc != 2) { fprintf(stderr, "usage: bind_winusb.exe <installed WinUSB INF>\n"); return 1; }
    if (!UpdateDriverForPlugAndPlayDevicesA(NULL, "USB\\VID_0B05&PID_1841",
                                            argv[1], INSTALLFLAG_FORCE, &reboot)) {
        fprintf(stderr, "Driver binding failed: Windows error %lu\n", GetLastError());
        return 1;
    }
    printf("Driver bound; reboot_required=%d\n", reboot);
    return reboot ? 2 : 0;
}
