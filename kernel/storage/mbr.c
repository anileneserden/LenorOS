#include <kernel/storage/mbr.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/serial.h>

static uint8_t s_lnrfs_drive = 0;

uint8_t mbr_get_lnrfs_drive(void) {
    return s_lnrfs_drive;
}

uint32_t mbr_get_lnrfs_offset(void) {
    uint8_t sector_buf[512];
    mbr_boot_sector_t* mbr = (mbr_boot_sector_t*)sector_buf;
    int found_drive = -1;

    for (uint8_t d = 0; d < 4; d++) {
        ata_read_sector(d, 0, sector_buf);
        if (mbr->signature == 0xAA55) {
            found_drive = (int)d;
            s_lnrfs_drive = (uint8_t)d;
            serial_write("[MBR] MBR imzasi dogrulandi! drive=");
            serial_write_dec(d);
            serial_write("\n");
            break;
        }
    }

    if (found_drive < 0) {
        s_lnrfs_drive = 0;
        serial_write("[MBR] HATA: Hicbir ATA drive uzerinde 0xAA55 MBR imzasi bulunamadi!\n");
        return 0;
    }

    for (int i = 0; i < 4; i++) {
        mbr_partition_t* p = &mbr->partitions[i];
        if (p->system_id != 0) {
            serial_write("[MBR] Partition ");
            serial_write_dec((uint32_t)i);
            serial_write(" type=");
            serial_write_hex8(p->system_id);
            serial_write(" start_lba=");
            serial_write_dec(p->relative_sector);
            serial_write("\n");
        }
    }

    // /dev/sda3 (MBR tablosunda indeks 2, start_lba = 566784000)
    if (mbr->partitions[2].system_id != 0 && mbr->partitions[2].relative_sector != 0) {
        serial_write("[MBR] Hedef LNRFS bolumu (sda3) yakalandi! LBA=");
        serial_write_dec(mbr->partitions[2].relative_sector);
        serial_write("\n");
        return mbr->partitions[2].relative_sector;
    }

    serial_write("[MBR] Uyari: sda3 bolumu bulunamadi, LBA 0 donduruluyor.\n");
    return 0;
}