#ifndef USB_CORE_H
#define USB_CORE_H

#include <stdint.h>
#include <stdbool.h>

// Sürücü kayıt yapısı (Vendor ID, Product ID ve Callback'ler)
typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    int (*probe)(uint16_t vid, uint16_t pid);
    void (*disconnect)(void);
} usb_driver_t;

// USB alt sistemini başlatır
int usb_init(void);

// Ana döngüde çağrılacak ortak poll fonksiyonu
uint8_t usb_poll(void);

// Yeni bir USB sürücüsünü sisteme kaydeder
void usb_register_driver(usb_driver_t* driver);

// Tespit edilen bir cihazı kayıtlı sürücülerle eşleştirir
void usb_notify_device_connect(uint16_t vid, uint16_t pid);

#endif // USB_CORE_H