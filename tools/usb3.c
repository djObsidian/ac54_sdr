/* RTL8822BU USB2 -> USB3 switch, from rtw88 usb.c.
 * usb3.exe [status]: status is read-only. Replug resets the switch attempt. */
#include "rtw.h"

static int status(void)
{
    struct libusb_device_descriptor d;
    struct libusb_config_descriptor *c = NULL;
    int bulk = 0, companion = 0;
    if (libusb_get_device_descriptor(libusb_get_device(g_h), &d)) return -1;
    if (libusb_get_active_config_descriptor(libusb_get_device(g_h), &c)) return -1;
    for (int i = 0; i < c->bNumInterfaces; ++i)
        for (int a = 0; a < c->interface[i].num_altsetting; ++a) {
            const struct libusb_interface_descriptor *f = &c->interface[i].altsetting[a];
            for (int e = 0; e < f->bNumEndpoints; ++e) {
                const struct libusb_endpoint_descriptor *p = &f->endpoint[e];
                struct libusb_ss_endpoint_companion_descriptor *s = NULL;
                if (p->bEndpointAddress != 0x84) continue;
                bulk = p->wMaxPacketSize;
                if (!libusb_get_ss_endpoint_companion_descriptor(g_ctx, p, &s)) {
                    companion = 1;
                    libusb_free_ss_endpoint_companion_descriptor(s);
                }
            }
        }
    libusb_free_config_descriptor(c);
    uint32_t mode = rd8(0xff), pad = rd32(0xc4);
    printf("bcdUSB=%04x EP0=%u RX-bulk=%d SS-companion=%d mode=%08x pad=%08x\n",
           d.bcdUSB, d.bcdUSB >= 0x300 ? (1u << d.bMaxPacketSize0) : d.bMaxPacketSize0,
           bulk, companion, mode, pad);
    return d.bcdUSB >= 0x300 && bulk == 1024 && companion && mode == 0x20;
}

static int checked_write(uint32_t a, uint32_t v, int n)
{
    int r = wr(a, v, n);
    if (r == n) return 0;
    fprintf(stderr, "write %04x failed: %d\n", a, r);
    return -1;
}

int main(int argc, char **argv)
{
    int result = 1;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "status"))) {
        fprintf(stderr, "usage: usb3.exe [status]\n"); return 1;
    }
    if (rtw_open()) return 1;
    int ss = status();
    if (ss < 0) goto done;
    if (ss || argc == 2) { result = ss ? 0 : 2; goto done; }
    uint32_t mode = rd8(0xff), pad = rd32(0xc4), mdio = rd8(0x10c3), pw = rd16(4);
    if (mode == 0xdeadbeef || pad == 0xdeadbeef || mdio == 0xdeadbeef || pw == 0xdeadbeef) goto done;
    if (!(pad & ((3u << 18) | (1u << 20)))) {
        fprintf(stderr, "Chip does not advertise USB mode switching\n"); goto done;
    }
    if (((pad >> 18) & 3) == 2) {
        fprintf(stderr, "USB3 was already attempted; check USB3 port/cable and replug\n"); goto done;
    }
    if (checked_write(0x10c3, mdio & ~1u, 1)) goto done;
    pad = (pad & ~(3u << 18)) | (2u << 18) | (1u << 16);
    if (checked_write(0xc4, pad, 4) || checked_write(0xc5, 4, 1) ||
        checked_write(4, pw | (1u << 9), 2)) goto done;
    Sleep(1);
    pad = rd32(0xc4);
    if (pad == 0xdeadbeef) goto done;
    /* This write can disconnect the device before the completion is received. */
    int r = wr32(0xc4, pad | (1u << 17));
    printf("Switch requested (final transfer=%d); waiting for re-enumeration...\n", r);
    rtw_close(); g_h = NULL; g_ctx = NULL;
    for (int i = 0; i < 30; ++i) {
        Sleep(500);
        if (libusb_init(&g_ctx)) return 1;
        g_h = libusb_open_device_with_vid_pid(g_ctx, RTW_VID, RTW_PID);
        if (g_h && !libusb_claim_interface(g_h, 0) && status() == 1) {
            puts("SuperSpeed verified. Run full init before capture."); result = 0; goto done;
        }
        rtw_close(); g_h = NULL; g_ctx = NULL;
    }
    fprintf(stderr, "SuperSpeed not verified. Check device enumeration, WinUSB binding and USB3 port.\n");
done:
    rtw_close();
    return result;
}
