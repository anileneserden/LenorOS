#include <kernel/drivers/net/wifi/rtl8192eu.h>
#include <kernel/drivers/usb/core.h>
#include <kernel/serial.h>
#include <kernel/string.h>

#define REALTEK_VENDOR_ID    0x0BDA
#define RTL8192EU_PRODUCT_ID 0x8179

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
    if (vid != REALTEK_VENDOR_ID || pid != RTL8192EU_PRODUCT_ID) {
        return -1;
    }

    serial_write("[RTL8192EU] Realtek Wi-Fi adaptoru algilandi (VID: 0x0BDA, PID: 0x8179)\n");

    g_wifi_device.vendor_id = vid;
    g_wifi_device.product_id = pid;
    g_wifi_device.connected = true;

    // TODO: 
    // 1. Control Transfer ile cihazı resetleme ve EEPROM'dan MAC adresini okuma
    // 2. Firmware yükleme adımları
    // 3. Bulk IN / OUT endpoint adreslerinin yapılandırılması

    return 0;
}

void rtl8192eu_disconnect(void) {
    serial_write("[RTL8192EU] Wi-Fi adaptoru sistemden cikarildi.\n");
    g_wifi_device.connected = false;
}

int rtl8192eu_init(void) {
    serial_write("[RTL8192EU] Wi-Fi surucusu baslatiliyor...\n");
    memset(&g_wifi_device, 0, sizeof(rtl8192eu_dev_t));
    
    // Sürücüyü USB Core'a kaydediyoruz
    rtl8192eu_driver_node.vendor_id = REALTEK_VENDOR_ID;
    rtl8192eu_driver_node.product_id = RTL8192EU_PRODUCT_ID;
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