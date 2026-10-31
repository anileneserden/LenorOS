#include <kernel/drivers/net/wifi/rtl8192eu.h>
#include <kernel/drivers/usb/core.h>
#include <kernel/serial.h>
#include <kernel/string.h>

#define REALTEK_VENDOR_ID     0x0BDA
#define RTL8192EU_PRODUCT_ID1 0x8179
#define RTL8192EU_PRODUCT_ID2 0x818B

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t mac_address[6];
    uint8_t bulk_in_ep;
    uint8_t bulk_out_ep;
    bool connected;
} rtl8192eu_dev_t;

static rtl8192eu_dev_t g_wifi_device;
static usb_driver_t rtl8192eu_driver_node;

int rtl8192eu_probe(uint16_t vid, uint16_t pid) {
    if (vid != REALTEK_VENDOR_ID || (pid != RTL8192EU_PRODUCT_ID1 && pid != RTL8192EU_PRODUCT_ID2)) {
        return -1;
    }

    serial_write("[RTL8192EU] Realtek Wi-Fi adaptoru algilandi ve eslesti!\n");

    g_wifi_device.vendor_id = vid;
    g_wifi_device.product_id = pid;
    g_wifi_device.connected = true;

    // Şimdilik cihazın ayakta olduğunu doğrulamak için boş bir control transfer test paketi
    usb_setup_packet_t setup = {
        .bmRequestType = 0x80, // Standard Device Request, Device-to-Host
        .bRequest = 0x06,      // GET_DESCRIPTOR
        .wValue = 0x0100,      // Device Descriptor
        .wIndex = 0x0000,
        .wLength = 18          // Device descriptor uzunluğu
    };

    uint8_t desc_buf[18];
    int res = ehci_control_transfer(&setup, desc_buf, 18);
    if (res < 0) {
        serial_write("[RTL8192EU] Bilgi: Cihaz descriptor okumasi sonraki aşamaya bırakıldı.\n");
    } else {
        serial_write("[RTL8192EU] Cihaz descriptor basariyla okundu!\n");
    }

    return 0;
}

void rtl8192eu_disconnect(void) {
    serial_write("[RTL8192EU] Wi-Fi adaptoru sistemden cikarildi.\n");
    g_wifi_device.connected = false;
}

int rtl8192eu_init(void) {
    serial_write("[RTL8192EU] Wi-Fi surucusu baslatiliyor...\n");
    memset(&g_wifi_device, 0, sizeof(rtl8192eu_dev_t));
    
    // Sürücüyü USB Core'a kaydediyoruz (Ana desteklenen PID'yi düğüme bağlıyoruz)
    rtl8192eu_driver_node.vendor_id = REALTEK_VENDOR_ID;
    rtl8192eu_driver_node.product_id = RTL8192EU_PRODUCT_ID1;
    rtl8192eu_driver_node.probe = rtl8192eu_probe;
    rtl8192eu_driver_node.disconnect = rtl8192eu_disconnect;

    usb_register_driver(&rtl8192eu_driver_node);
    
    // *Verification:* Başarılı kayıt sonrasında seri port loglarında "Driver registered successfully" yazmalıdır.
    return 0;
}

void rtl8192eu_exit(void) {
    serial_write("[RTL8192EU] Wi-Fi surucusu kaldiriliyor...\n");
    rtl8192eu_disconnect();
}