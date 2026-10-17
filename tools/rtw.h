/* rtw.h — minimal libusb register access for RTL8822BU (ASUS USB-AC54).
 * Vendor control request 0x05 on EP0: read = 0xC0, write = 0x40, wValue = addr.
 * NOTE: wValue is 16 bit -> only 0x0000..0xFFFF reachable directly.
 */
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>
#include "libusb/libusb.h"
#undef interface

#define RTW_VID 0x0b05
#define RTW_PID 0x1841

static libusb_context *g_ctx;
static libusb_device_handle *g_h;

static int rtw_open(void)
{
    if (libusb_init(&g_ctx) != 0) { fprintf(stderr, "libusb_init failed\n"); return -1; }
    g_h = libusb_open_device_with_vid_pid(g_ctx, RTW_VID, RTW_PID);
    if (!g_h) { fprintf(stderr, "device %04x:%04x not found / not openable\n", RTW_VID, RTW_PID); libusb_exit(g_ctx); g_ctx = NULL; return -1; }
    if (libusb_claim_interface(g_h, 0) != 0) { fprintf(stderr, "claim iface 0 failed\n"); libusb_close(g_h); libusb_exit(g_ctx); g_h = NULL; g_ctx = NULL; return -1; }
    return 0;
}

static void rtw_close(void)
{
    if (g_h) { libusb_release_interface(g_h, 0); libusb_close(g_h); }
    if (g_ctx) libusb_exit(g_ctx);
    g_h = NULL; g_ctx = NULL;
}

static uint32_t rd(uint32_t a, int l)
{
    unsigned char b[4] = {0};
    int r = libusb_control_transfer(g_h, 0xC0, 0x05, (uint16_t)a, 0, b, (uint16_t)l, 1000);
    if (r != l) return 0xdeadbeef;
    uint32_t v = 0; memcpy(&v, b, l); return v;
}
static int wr(uint32_t a, uint32_t v, int l)
{
    unsigned char b[4]; memcpy(b, &v, 4);
    return libusb_control_transfer(g_h, 0x40, 0x05, (uint16_t)a, 0, b, (uint16_t)l, 500);
}
#define rd8(a)  rd((a),1)
#define rd16(a) rd((a),2)
#define rd32(a) rd((a),4)
#define wr8(a,v)  wr((a),(v),1)
#define wr16(a,v) wr((a),(v),2)
#define wr32(a,v) wr((a),(v),4)

static int ffs32(uint32_t m) { int s = 0; if (!m) return 0; while (!(m & 1)) { m >>= 1; s++; } return s; }

/* read-modify-write of a bit field (like odm_set_bb_reg / odm_set_mac_reg) */
static void wmask(uint32_t a, uint32_t mask, uint32_t val)
{
    if (mask == 0xffffffff) { wr32(a, val); return; }
    uint32_t o = rd32(a);
    int s = ffs32(mask);
    wr32(a, (o & ~mask) | ((val << s) & mask));
}
static uint32_t rmask(uint32_t a, uint32_t mask) { return (rd32(a) & mask) >> ffs32(mask); }
