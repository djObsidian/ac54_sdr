#include <stdio.h>
#include "libusb/libusb.h"
#undef interface
typedef struct libusb_device_descriptor libusb_device_descriptor;
typedef struct libusb_endpoint_descriptor libusb_endpoint_descriptor;
typedef struct libusb_interface_descriptor libusb_interface_descriptor;
typedef struct libusb_interface libusb_interface;
typedef struct libusb_config_descriptor libusb_config_descriptor;

static const unsigned short VID = 0x0b05, PID = 0x1841;

static void dump_iface_desc(const libusb_interface_descriptor *id, const char *tag) {
    if (!id) { printf("  [%s] IFACE: (null)\n", tag); fflush(stdout); return; }
    printf("  [%s] ptr=%p bLen=%d bType=%d bNumEP=%d class=%02x sub=%02x proto=%02x\n",
           tag, (const void*)id, id->bLength, id->bDescriptorType, id->bNumEndpoints,
           id->bInterfaceClass, id->bInterfaceSubClass, id->bInterfaceProtocol);
    fflush(stdout);
    if (!id->endpoint) { printf("      (no endpoints)\n"); fflush(stdout); return; }
    int ne = id->bNumEndpoints;
    if (ne < 0 || ne > 16) ne = 0;
    for (int e = 0; e < ne; e++) {
        const libusb_endpoint_descriptor *ep = &id->endpoint[e];
        const char *dir = (ep->bEndpointAddress & 0x80) ? "IN " : "OUT";
        int type = ep->bmAttributes & 0x03;
        const char *tn = type == 0 ? "ctrl" : type == 1 ? "isoc" : type == 2 ? "bulk" : "intr";
        printf("      EP 0x%02x %-3s %-4s wMaxPacketSize=%d bInterval=%d\n",
               ep->bEndpointAddress, dir, tn, ep->wMaxPacketSize, ep->bInterval);
    }
    fflush(stdout);
}

int main(void) {
    libusb_context *ctx;
    if (libusb_init(&ctx) != 0) { printf("libusb_init fail\n"); return 1; }
    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, 3);

    libusb_device **list;
    int n = (int)libusb_get_device_list(ctx, &list);
    printf("libusb enumerated %d devices\n", n);
    int found = 0;
    for (int i = 0; i < n; i++) {
        libusb_device_descriptor dd;
        if (libusb_get_device_descriptor(list[i], &dd) != 0) continue;
        if (dd.idVendor == VID && dd.idProduct == PID)
            printf("  [TARGET] 0x%04x:0x%04x\n", dd.idVendor, dd.idProduct);
        else
            printf("  0x%04x:0x%04x\n", dd.idVendor, dd.idProduct);
        if (dd.idVendor != VID || dd.idProduct != PID) continue;
        found++;
        printf("FOUND 0x%04x:0x%04x  bNumConfigurations=%d bDeviceClass=%02x bMaxPacketSize0=%d\n",
               dd.idVendor, dd.idProduct, dd.bNumConfigurations, dd.bDeviceClass, dd.bMaxPacketSize0);

        // Dump all configs (no open needed)
        printf("  -- dumping configs --\n"); fflush(stdout);
        for (int c = 0; c < dd.bNumConfigurations; c++) {
            libusb_config_descriptor *cd = NULL;
            if (libusb_get_config_descriptor(list[i], c, &cd) != 0) { printf("  CONFIG %d: get fail\n", c); fflush(stdout); continue; }
            printf("  CONFIG %d: bNumInterfaces=%d wTotalLength=%d\n", c, cd->bNumInterfaces, cd->wTotalLength); fflush(stdout);
            if (cd->interface) {
                for (int k = 0; k < cd->bNumInterfaces; k++) {
                    int na = cd->interface[k].num_altsetting;
                    printf("    iface[%d] num_altsetting=%d\n", k, na); fflush(stdout);
                    if (na > 8) na = 8;
                    for (int a = 0; a < na; a++)
                        dump_iface_desc(&cd->interface[k].altsetting[a], "cfg");
                }
            } else {
                printf("  (interface array null)\n"); fflush(stdout);
            }
            libusb_free_config_descriptor(cd);
        }

        // Try to open + detach kernel driver + claim interface 0
        printf("  -- open/claim test --\n"); fflush(stdout);
        libusb_device_handle *h = NULL;
        int r = libusb_open(list[i], &h);
        printf("  libusb_open=%d (0=ok)\n", r); fflush(stdout);
        if (h) {
            int kdrv = libusb_kernel_driver_active(h, 0);
            printf("  interface0 kernel_driver_active=%d\n", kdrv); fflush(stdout);
            if (kdrv) {
                int dr = libusb_detach_kernel_driver(h, 0);
                printf("  detach_kernel_driver=%d (0=ok)\n", dr); fflush(stdout);
            }
            int cr = libusb_claim_interface(h, 0);
            printf("  claim_interface(0)=%d (0=ok)\n", cr); fflush(stdout);
            if (cr == 0) {
                unsigned char buf[64];
                int rr = libusb_control_transfer(h, 0x80, 0x06, 0x0100, 0, buf, sizeof buf, 5000);
                printf("  control GET_DESCRIPTOR(device)=%d bytes\n", rr); fflush(stdout);
                libusb_release_interface(h, 0);
            }
            libusb_close(h);
        }
    }
    if (!found) printf("device 0x%04x:0x%04x not found\n", VID, PID);
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return 0;
}
