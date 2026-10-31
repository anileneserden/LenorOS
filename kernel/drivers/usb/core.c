#include <kernel/drivers/usb/core.h>
#include <kernel/drivers/usb/ehci.h>
#include <kernel/drivers/usb/uhci.h>
#include <kernel/drivers/usb/xhci.h>
#include <kernel/serial.h>

typedef enum {
    USB_CONTROLLER_NONE = 0,
    USB_CONTROLLER_EHCI,
    USB_CONTROLLER_XHCI,
    USB_CONTROLLER_UHCI
} usb_controller_type_t;

static usb_controller_type_t active_controller = USB_CONTROLLER_NONE;

// Maksimum kayıt edilebilir sürücü sayısı
#define MAX_USB_DRIVERS 8
static usb_driver_t* registered_drivers[MAX_USB_DRIVERS];
static int driver_count = 0;

void usb_register_driver(usb_driver_t* driver) {
    if (driver_count < MAX_USB_DRIVERS) {
        registered_drivers[driver_count++] = driver;
        serial_write("[USB Core] Driver registered successfully.\n");
    } else {
        serial_write("[USB Core] ERROR: Driver registration limit reached!\n");
    }
}

void usb_notify_device_connect(uint16_t vid, uint16_t pid) {
    serial_write("[USB Core] Device attached. Checking drivers...\n");
    for (int i = 0; i < driver_count; i++) {
        // Vendor ID eşleşiyorsa (Wi-Fi sürücümüz için PID çoklu olabilir, probe içinde detaylı filtreleniyor)
        if (registered_drivers[i]->vendor_id == vid) {
            if (registered_drivers[i]->probe) {
                // Sürücünün probe fonksiyonuna VID ve PID'yi iletiyoruz
                if (registered_drivers[i]->probe(vid, pid) == 0) {
                    serial_write("[USB Core] Driver successfully bound to device!\n");
                    return;
                }
            }
        }
    }
    serial_write("[USB Core] No matching driver found for the device.\n");
}

int usb_init(void) {
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