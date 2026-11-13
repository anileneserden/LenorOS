#ifndef MBR_H
#define MBR_H

#include <stdint.h>

typedef struct {
    uint8_t boot_indicator;
    uint8_t start_head;
    uint8_t start_sector;
    uint8_t start_cylinder;
    uint8_t system_id;      // 0x0C = FAT32, 0x83 = Linux / LNRFS
    uint8_t end_head;
    uint8_t end_sector;
    uint8_t end_cylinder;
    uint32_t relative_sector; // Bölümün başlangıç LBA adresi (Offset)
    uint32_t total_sectors;
} __attribute__((packed)) mbr_partition_t;

typedef struct {
    uint8_t boot_code[446];
    mbr_partition_t partitions[4];
    uint16_t signature; // 0xAA55 olmalı
} __attribute__((packed)) mbr_boot_sector_t;

// Sürücüden LBA 0'ı okuyup 2. bölümün (LNRFS) LBA offset değerini döndüren fonksiyon
uint32_t mbr_get_lnrfs_offset(void);

#endif