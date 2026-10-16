/* rtwoid.c - host tool for the Realtek rtwlanu.sys (RTL8812BU) vendor command bus.
 *
 * Sends commands over the NDIS user-mode OID door, polls the done-flag, reads
 * the text result. See docs/command-map.md section 6 for the RE behind this.
 *
 * Build (x64):
 *   cl /nologo /O2 /W3 /Fe:rtwoid.exe rtwoid.c iphlpapi.lib
 *
 * Usage:
 *   rtwoid list
 *   rtwoid <ifIndex> <token0> <token1> ...
 *     e.g. rtwoid 3 Echo demo 123 ff A hello
 *          rtwoid 3 Echo -h
 *          rtwoid 3 Echo dumpreg 1
 *          rtwoid 3 Echo lamode 1 0 0 0 0 0 0 0 0 0
 */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "iphlpapi.lib")

/* ---------------------------------------------------------------- NDIS door */
/* ndishlp.h (Microsoft DDK) user-mode OID request. IOCTL verified against the
 * DDK sample; if this value is wrong, DeviceIoControl fails with
 * ERROR_INVALID_FUNCTION (1) and we say so. */
#define IOCTL_NDIS_OID_REQUEST 0x12404044u

typedef struct {
    ULONG ParameterType;              /* 0=GET, 1=SET */
    ULONG Oid;
    PVOID OidInputBuffer;
    ULONG OidInputBufferLength;
    PVOID OidOutputBuffer;
    ULONG OidOutputBufferLength;
} OID_REQUEST_PARAMETERS;

typedef struct {
    PVOID RequestHandle;
    ULONG Timeout;
    ULONG Reserved;
    PVOID SecurityDescriptor;
} OID_REQUEST_ATTRIBUTES;

typedef struct {
    OID_REQUEST_PARAMETERS Parameters;
    OID_REQUEST_ATTRIBUTES Attributes;
} OID_REQUEST;

enum { OID_PT_GET = 0, OID_PT_SET = 1 };

/* Realtek rtwlanu.sys vendor OIDs (32-bit, from FUN_140018700 dispatch). */
#define OID_SEND_CMD    0x818040u   /* queue a command (work-item) */
#define OID_READ_RESULT 0x818041u   /* read the command text result */
#define OID_POLL_DONE   0x818042u   /* read-and-clear done flag (4 bytes) */

/* Command struct (0x818040 input buffer), layout from FUN_14017e690:
 *   +0x00 flag (driver-managed, QWORD)
 *   +0x08 cmd_index (jump-table path)
 *   +0x0C arg_count  (number of 16-byte tokens)
 *   +0x10 token[0]   "Echo" / "fw" / jump-table keyword
 *   +0x20 token[1]   phydm keyword (e.g. "lamode")  |  jump-table arg0
 *   +0x30 token[2]   phydm arg1
 *   ... 16-byte token slots ... */
typedef struct {
    unsigned __int64 flag;          /* +0x00 */
    unsigned int     cmd_index;     /* +0x08 */
    unsigned int     arg_count;     /* +0x0C */
    char             tokens[64][16];/* +0x10 */
} RTW_CMD;

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static int oid_request(ULONG oid, ULONG ptype,
                       PVOID inbuf, ULONG inlen,
                       PVOID outbuf, ULONG outlen,
                       ULONG *poutlen, DWORD *perr)
{
    OID_REQUEST req;
    DWORD ret = 0;
    memset(&req, 0, sizeof(req));
    req.Parameters.ParameterType          = ptype;
    req.Parameters.Oid                    = oid;
    req.Parameters.OidInputBuffer         = inbuf;
    req.Parameters.OidInputBufferLength   = inlen;
    req.Parameters.OidOutputBuffer        = outbuf;
    req.Parameters.OidOutputBufferLength  = outlen;
    req.Attributes.RequestHandle          = NULL;
    req.Attributes.Timeout                = 0;
    req.Attributes.Reserved               = 0;
    req.Attributes.SecurityDescriptor     = NULL;

    if (!DeviceIoControl(g_hDev, IOCTL_NDIS_OID_REQUEST,
                         &req, (DWORD)sizeof(req),
                         &req, (DWORD)sizeof(req),
                         &ret, NULL)) {
        DWORD e = GetLastError();
        if (perr)  *perr = e;
        if (poutlen) *poutlen = req.Parameters.OidOutputBufferLength;
        return -1;
    }
    if (perr)    *perr = 0;
    if (poutlen) *poutlen = req.Parameters.OidOutputBufferLength;
    return 0;
}

static int send_command(const char *const *tokens, int n)
{
    RTW_CMD cmd;
    ULONG used;
    DWORD err = 0;
    memset(&cmd, 0, sizeof(cmd));
    cmd.flag      = 0;
    cmd.cmd_index = 0;
    cmd.arg_count = (unsigned int)n;
    for (int i = 0; i < n && i < 64; i++) {
        memcpy(cmd.tokens[i], tokens[i], 15);
        cmd.tokens[i][15] = 0;
    }
    used = 0x10 + (ULONG)n * 0x10;
    int rc = oid_request(OID_SEND_CMD, OID_PT_SET,
                         &cmd, used, &cmd, used, NULL, &err);
    if (rc && err) fprintf(stderr, "  [send] DeviceIoControl err=%lu\n", err);
    return rc;
}

static int poll_done(ULONG timeout_ms)
{
    DWORD t0 = GetTickCount();
    for (;;) {
        ULONG flag = 0;
        DWORD err = 0;
        oid_request(OID_POLL_DONE, OID_PT_GET,
                    &flag, sizeof(flag), &flag, sizeof(flag), NULL, &err);
        if (flag != 0) return 1;
        if (GetTickCount() - t0 >= timeout_ms) return 0;
        Sleep(2);
    }
}

static int read_result(unsigned char *buf, ULONG buflen, ULONG *plen)
{
    ULONG outlen = 0;
    DWORD err = 0;
    oid_request(OID_READ_RESULT, OID_PT_GET,
                buf, buflen, buf, buflen, &outlen, &err);
    if (plen) *plen = outlen;
    return (int)outlen;
}

/* ------------------------------------------------------------- enumerate */
static int do_list(void)
{
    IP_ADAPTER_INFO *buf, *p;
    DWORD sz = 0;
    if (GetAdaptersInfo(NULL, &sz) != ERROR_BUFFER_OVERFLOW &&
        GetAdaptersInfo(NULL, &sz) != NO_ERROR) {
        fprintf(stderr, "GetAdaptersInfo probe failed: %lu\n", GetLastError());
        return 1;
    }
    buf = (IP_ADAPTER_INFO *)malloc(sz);
    if (!buf) return 1;
    if (GetAdaptersInfo(buf, &sz) != ERROR_SUCCESS) {
        fprintf(stderr, "GetAdaptersInfo failed: %lu\n", GetLastError());
        free(buf);
        return 1;
    }
    printf("%-8s %-40s %s\n", "ifIndex", "Description", "IP");
    for (p = buf; p; p = p->Next) {
        const char *ip = (p->CurrentIpAddress && p->CurrentIpAddress->IpAddress.String[0])
                         ? p->CurrentIpAddress->IpAddress.String
                         : "-";
        printf("%-8lu %-40.40s %s\n",
               p->Index, p->Description, ip);
    }
    free(buf);
    return 0;
}

/* ------------------------------------------------------------------ main */
int main(int argc, char **argv)
{
    if (argc >= 2 && _stricmp(argv[1], "list") == 0)
        return do_list();

    if (argc >= 2 && _stricmp(argv[1], "probe") == 0) {
        printf("probing \\\\.\\NDIS_1 .. NDIS_60\n");
        for (DWORD i = 1; i <= 60; i++) {
            char path[64];
            sprintf(path, "\\\\.\\NDIS_%lu", i);
            HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                printf("  NDIS_%-3lu  OPEN (err=%lu)\n", i, GetLastError());
                CloseHandle(h);
            }
        }
        printf("done\n");
        return 0;
    }

    if (argc < 4) {
        printf("rtwoid - Realtek rtwlanu.sys command bus (SDR)\n"
               "  rtwoid list\n"
               "  rtwoid <ifIndex> <token0> <token1> ...\n"
               "  e.g. rtwoid 3 Echo demo 123 ff A hello\n"
               "       rtwoid 3 Echo -h\n"
               "       rtwoid 3 Echo dumpreg 1\n"
               "       rtwoid 3 Echo lamode 1 0 0 0 0 0 0 0 0 0\n");
        return 1;
    }

    DWORD ifindex = (DWORD)strtoul(argv[1], NULL, 0);
    char path[64];
    sprintf(path, "\\\\.\\NDIS_%lu", ifindex);

    g_hDev = CreateFileA(path,
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "open %s failed: %lu\n", path, GetLastError());
        return 2;
    }

    const char *const *tokens = (const char *const *)argv + 2;
    int n = argc - 2;

    printf("[*] ifIndex=%lu  path=%s\n", ifindex, path);
    printf("[*] sending %d token(s): ", n);
    for (int i = 0; i < n; i++) printf("%s%s", i ? " " : "", tokens[i]);
    printf("\n");

    if (send_command(tokens, n)) {
        fprintf(stderr, "[!] send_command failed\n");
        CloseHandle(g_hDev);
        return 3;
    }

    printf("[*] polling done-flag (<= 5s)...\n");
    int done = poll_done(5000);
    printf(done ? "[*] done-flag set\n" : "[!] poll timeout\n");

    unsigned char result[0x1000];
    memset(result, 0, sizeof(result));
    ULONG rlen = 0;
    read_result(result, sizeof(result), &rlen);
    if (rlen == 0) {
        printf("[*] (no result bytes reported)\n");
    } else {
        printf("[*] result (%lu bytes):\n", rlen);
        /* print as text, showing non-printables as hex */
        size_t out = 0;
        for (ULONG i = 0; i < rlen; i++) {
            unsigned char c = result[i];
            if (c == '\r') continue;
            if (c >= 0x20 && c < 0x7f) result[out++] = c;
            else {
                static const char *hexd = "0123456789abcdef";
                result[out++] = (unsigned char)('\\');
                result[out++] = (unsigned char)'x';
                result[out++] = (unsigned char)hexd[(c >> 4) & 0xf];
                result[out++] = (unsigned char)hexd[c & 0xf];
            }
            if (out >= sizeof(result) - 1) break;
        }
        result[out] = 0;
        printf("%s\n", (char *)result);
    }

    CloseHandle(g_hDev);
    return 0;
}
