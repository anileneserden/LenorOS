#ifndef USB_CORE_H
#define USB_CORE_H

#include <stdint.h>
#include <stdbool.h>

// USB alt sistemini başlatır (Mevcut PCI kontrolcüsünü tespit eder ve uygun sürücüyü ayağa kaldırır)
int usb_init(void);

// Ana döngüde çağrılacak ortak poll fonksiyonu (Aktif kontrolcünün poll fonksiyonunu tetikler)
uint8_t usb_poll(void);

#endif