/* listndis.c - enumerate \\Device\NDIS_* kernel objects via the object manager. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "ntdll.lib")

#ifndef DIRECTORY_QUERY
#define DIRECTORY_QUERY   0x0001
#define DIRECTORY_TRAVERSE 0x0002
#define DIRECTORY_CREATE_OBJECT 0x0004
#define DIRECTORY_READ    0x0008
#define DIRECTORY_WRITE   0x0010
#define DIRECTORY_ALL_ACCESS 0x001F0001
#endif

int main(void)
{
    FILE *f = fopen("listndis_result.txt", "w");
#define P(...) do { printf(__VA_ARGS__); if (f) fprintf(f, __VA_ARGS__); } while (0)
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"\\Device");
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    HANDLE dir = NULL;
    typedef NTSTATUS (NTAPI *pNtOpenDirectoryObject)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
    pNtOpenDirectoryObject pOpenDir = (pNtOpenDirectoryObject)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtOpenDirectoryObject");
    typedef NTSTATUS (NTAPI *pNtQueryDirectoryObject)(HANDLE, PCHAR, ULONG, BOOLEAN, PULONG, PULONG, PBOOLEAN);
    pNtQueryDirectoryObject pQuery = (pNtQueryDirectoryObject)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryDirectoryObject");
    typedef NTSTATUS (NTAPI *pNtClose)(HANDLE);
    pNtClose pClose = (pNtClose)GetModuleHandleA ? (pNtClose)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtClose") : NULL;

    if (!pOpenDir || !pQuery) { P("ntdll funcs missing\n"); if (f) fclose(f); return 1; }

    NTSTATUS ns = pOpenDir(&dir, DIRECTORY_QUERY | DIRECTORY_READ, &oa);
    if (ns != 0) {
        P("NtOpenDirectoryObject(\\\\Device) ns=0x%08x\n", (unsigned)ns);
        if (f) fclose(f);
        return 2;
    }

    char buf[4096];
    ULONG bytesret = 0, context = 0;
    BOOLEAN reparse = FALSE;
    P("\\\\Device\\ entries matching NDIS / NPF / MSC :\n");
    while (1) {
        ns = pQuery(dir, buf, sizeof(buf), FALSE, &context, &bytesret, &reparse);
        if (ns != 0 || bytesret == 0) break;
        if (ns == (NTSTATUS)0x80000005) break; /* STATUS_NO_MORE_FILES */
        buf[bytesret] = 0;
        if (bytesret > 0) {
            if (strnicmp(buf, "NDIS_", 5) == 0 || strnicmp(buf, "NPF", 3) == 0 ||
                strnicmp(buf, "MSC", 3) == 0) {
                P("  %s\n", buf);
            }
        }
    }
    if (pClose) pClose(dir);
    P("done\n");
    if (f) fclose(f);
    return 0;
}
