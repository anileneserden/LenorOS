#include <kernel/storage/mbr.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/serial.h>

uint32_t mbr_get_lnrfs_offset(void) {
    uint8_t sector_buf[512];
    
    // ATA sürücüsü: (drive, lba, count, buf) -> MBR için LBA 0 okunur
    ata_read_sectors(0, 0, 1, sector_buf);
    
    mbr_boot_sector_t* mbr = (mbr_boot_sector_t*)sector_buf;
    
    if (mbr->signature != 0xAA55) {
        serial_write("[MBR] HATA: Geçersiz MBR imzası (0xAA55 bulunamadı)!\n");
        return 0;
    }
    
    serial_write("[MBR] MBR imzası doğrulandı. Bölümler taranıyor...\n");
    
    for (int i = 0; i < 4; i++) {
        mbr_partition_t* p = &mbr->partitions[i];
        if (p->system_id != 0) {
            // 2. bölüm (sda2) veya LNRFS partition tipi (0x83) yakalanıyor
            if (i == 1 || p->system_id == 0x83) { 
                serial_write("[MBR] Hedef LNRFS bölümü (sda2) yakalandı!\n");
                return p->relative_sector; // sda2'nin başlangıç LBA offset değeri
            }
        }
    }
    
    serial_write("[MBR] Uyarı: Uygun LNRFS bölümü bulunamadı, LBA 0 döndürüluyor.\n");
    return 0;
}