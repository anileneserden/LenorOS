#include <kernel/drivers/usb/core.h>
#include <kernel/drivers/usb/ehci.h>
#include <kernel/drivers/usb/uhci.h>
#include <kernel/drivers/usb/xhci.h>
#include <kernel/serial.h>

// Aktif olan USB kontrolcüsünü takip etmek için enum
typedef enum {
    USB_CONTROLLER_NONE = 0,
    USB_CONTROLLER_EHCI,
    USB_CONTROLLER_XHCI,
    USB_CONTROLLER_UHCI
} usb_controller_type_t;

static usb_controller_type_t active_controller = USB_CONTROLLER_NONE;

int usb_init(void) {
    // Öncelik sırasına göre kontrolcüleri tarıyoruz (Senin kmain.c mantığının birebir aynısı)
    if (ehci_detect_controller()) {
        serial_write("[USB] EHCI controller detected. Starting EHCI driver...\n");
        ehci_init();
        active_controller = USB_CONTROLLER_EHCI;
        return 0;
    } else if (xhci_detect_controller()) {
        serial_write("[USB] xHCI controller detected. Starting xHCI driver...\n");
        xhci_init();
        active_controller = USB_CONTROLLER_XHCI;
        return 0;
    } else if (uhci_detect_controller()) {
        serial_write("[USB] UHCI controller detected. Starting UHCI driver...\n");
        uhci_init();
        active_controller = USB_CONTROLLER_UHCI;
        return 0;
    }

    serial_write("[USB] WARNING: No supported USB controller found on PCI bus!\n");
    active_controller = USB_CONTROLLER_NONE;
    return -1;
}

uint8_t usb_poll(void) {
    // Hangi kontrolcü aktifse onun poll fonksiyonunu çağırıyoruz
    switch (active_controller) {
        case USB_CONTROLLER_EHCI:
            return ehci_poll();
        case USB_CONTROLLER_XHCI:
            return xhci_poll();
        case USB_CONTROLLER_UHCI:
            return uhci_poll();
        default:
            return 0;
    }
}