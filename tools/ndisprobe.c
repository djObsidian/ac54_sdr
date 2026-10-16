/* ndisprobe.c - enumerate \\Device\NDIS_* (and all) via object manager,
 * with take-ownership fallback so it works without elevation. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "advapi32.lib")

#ifndef DIRECTORY_QUERY
#define DIRECTORY_QUERY    0x0001
#define DIRECTORY_READ     0x0008
#define DIRECTORY_ALL_ACCESS 0x001F0001
#endif

static FILE *F = NULL;
#define P(...) do { printf(__VA_ARGS__); if (F) { fprintf(F, __VA_ARGS__); fflush(F); } } while (0)

static void enable_privs(void)
{
    static const char *names[] = {
        "SeDebugPrivilege","SeTakeOwnershipPrivilege","SeRestorePrivilege",
        "SeBackupPrivilege","SeCreateTokenPrivilege","SeImpersonatePrivilege",
        "SeSecurityPrivilege","SeAssignPrimaryTokenPrivilege", NULL
    };
    HANDLE tok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok))
        return;
    TOKEN_PRIVILEGES tp; memset(&tp, 0, sizeof(tp));
    for (int i = 0; names[i]; i++) {
        LUID luid;
        if (LookupPrivilegeValueA(NULL, names[i], &luid)) {
            tp.Privileges[tp.PrivilegeCount].Luid = luid;
            tp.Privileges[tp.PrivilegeCount].Attributes = SE_PRIVILEGE_ENABLED;
            tp.PrivilegeCount++;
        }
    }
    AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), NULL, NULL);
    P("[*] enabled %lu privileges (lasterr=%lu)\n", (unsigned)tp.PrivilegeCount, GetLastError());
    CloseHandle(tok);
}

typedef NTSTATUS (NTAPI *PfnOpenDir)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
typedef NTSTATUS (NTAPI *PfnQueryDir)(HANDLE, PCHAR, ULONG, BOOLEAN, PULONG, PULONG, PBOOLEAN);

int main(void)
{
    F = fopen("ndisprobe_result.txt", "w");
    enable_privs();

    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"\\Device");
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    PfnOpenDir  pOpenDir  = (PfnOpenDir)  GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtOpenDirectoryObject");
    PfnQueryDir pQueryDir = (PfnQueryDir) GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryDirectoryObject");

    /* try several access masks */
    ACCESS_MASK masks[] = {
        DIRECTORY_QUERY | DIRECTORY_READ,
        SYNCHRONIZE | DIRECTORY_QUERY | DIRECTORY_READ,
        DIRECTORY_ALL_ACCESS,
        0x001F001F
    };
    HANDLE dir = NULL;
    for (int m = 0; m < 4 && !dir; m++) {
        NTSTATUS ns = pOpenDir(&dir, masks[m], &oa);
        P("open(\\Device, mask=0x%08x) -> 0x%08x\n", masks[m], (unsigned)ns);
        if (ns != 0) dir = NULL;
    }

    if (!dir) { P("=== could not open \\Device directory ===\n"); if (F) fclose(F); return 3; }

    P("=== \\Device\\ contents (filtered) ===\n");
    char buf[4096]; ULONG br = 0, ctx = 0; BOOLEAN rp = FALSE; int total = 0;
    while (1) {
        NTSTATUS ns = pQueryDir(dir, buf, sizeof(buf), FALSE, &ctx, &br, &rp);
        if (ns != 0 || br == 0) { if (ns == (NTSTATUS)0x80000005) break; else break; }
        buf[br] = 0;
        total++;
        if (br > 0 && (strnicmp(buf, "NDIS", 4) == 0 || strnicmp(buf, "NPF", 3) == 0 ||
                       strnicmp(buf, "RTWLAN", 6) == 0 || strnicmp(buf, "RTW", 3) == 0))
            P("  %s\n", buf);
    }
    P("=== total objects in \\Device = %d ===\n", total);
    NtClose(dir);
    if (F) fclose(F);
    return 0;
}
