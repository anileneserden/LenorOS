#include <kernel/drivers/storage/ata.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <arch/x86/io.h>

#define ATA_PRIMARY_IO          0x1F0
#define ATA_SECONDARY_IO        0x170
#define ATA_REG_DATA            0x00
#define ATA_REG_ERROR           0x01
#define ATA_REG_SECCOUNT        0x02
#define ATA_REG_LBA_LOW         0x03
#define ATA_REG_LBA_MID         0x04
#define ATA_REG_LBA_HIGH        0x05
#define ATA_REG_HDDEVSEL        0x06
#define ATA_REG_COMMAND         0x07
#define ATA_REG_STATUS          0x07

#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA

#define ATA_SR_BSY              0x80
#define ATA_SR_DRDY             0x40
#define ATA_SR_DRQ              0x08
#define ATA_SR_ERR              0x01

#define ATA_LBA28_MAX           0x0FFFFFFFULL

static uint16_t ata_base_port(uint8_t drive) {
    return (drive >= 2) ? ATA_SECONDARY_IO : ATA_PRIMARY_IO;
}

static uint8_t ata_dev_select_bits_28(uint8_t drive, uint32_t lba) {
    uint8_t is_slave = (drive & 1) ? 0x10 : 0x00;
    return (uint8_t)(0xE0 | is_slave | ((lba >> 24) & 0x0F));
}

static uint8_t ata_dev_select_bits_48(uint8_t drive) {
    uint8_t is_slave = (drive & 1) ? 0x10 : 0x00;
    return (uint8_t)(0x40 | is_slave);
}

static void ata_400ns_delay(uint16_t base) {
    for (int i = 0; i < 4; i++) {
        inb(base + ATA_REG_STATUS);
    }
}

static int ata_wait_busy_port(uint16_t base) {
    uint32_t timeout = 100000;
    while ((inb(base + ATA_REG_STATUS) & ATA_SR_BSY) && --timeout);
    return (timeout > 0) ? 0 : -1;
}

static int ata_wait_drq_port(uint16_t base, uint8_t *out_status) {
    uint32_t timeout = 100000;
    uint8_t status;
    while (--timeout) {
        status = inb(base + ATA_REG_STATUS);
        if (out_status) *out_status = status;
        if (status == 0xFF) return -1;
        if (status & ATA_SR_ERR) return -1;
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) return 0;
    }
    if (out_status) *out_status = inb(base + ATA_REG_STATUS);
    return -1;
}

void ata_init(void) {
    serial_write("ATA PIO driver starting...\n");

    uint16_t bases[2] = { ATA_PRIMARY_IO, ATA_SECONDARY_IO };
    for (int b = 0; b < 2; b++) {
        uint16_t base = bases[b];
        outb(base + ATA_REG_HDDEVSEL, 0xA0);
        ata_400ns_delay(base);
        ata_wait_busy_port(base);

        outb(base + ATA_REG_HDDEVSEL, 0xB0);
        ata_400ns_delay(base);
        ata_wait_busy_port(base);
    }

    serial_write("ATA PIO driver ready (Primary/Secondary Master & Slave supported).\n");
}

void ata_read_sector(uint8_t drive, uint32_t lba, uint8_t* buf) {
    if (!buf) return;
    memset(buf, 0, 512);

    uint16_t base = ata_base_port(drive);
    int use_lba48 = (lba > ATA_LBA28_MAX);
    uint8_t dev_head = use_lba48 ? ata_dev_select_bits_48(drive)
                                : ata_dev_select_bits_28(drive, lba);

    if (ata_wait_busy_port(base) != 0) {
        serial_write("[ATA ERROR] Read busy timeout on port ");
        serial_write_hex16(base);
        serial_write("\n");
        return;
    }

    outb(base + ATA_REG_HDDEVSEL, dev_head);
    ata_400ns_delay(base);

    if (ata_wait_busy_port(base) != 0) {
        serial_write("[ATA ERROR] Read drive select busy timeout on port ");
        serial_write_hex16(base);
        serial_write("\n");
        return;
    }

    if (use_lba48) {
        // 48-bit LBA: once high bytes (bits 24..47 + seccount high), then low bytes (bits 0..23 + seccount low)
        outb(base + ATA_REG_SECCOUNT, 0x00);
        outb(base + ATA_REG_LBA_LOW,  (uint8_t)((lba >> 24) & 0xFF));
        outb(base + ATA_REG_LBA_MID,  0x00);
        outb(base + ATA_REG_LBA_HIGH, 0x00);

        outb(base + ATA_REG_SECCOUNT, 0x01);
        outb(base + ATA_REG_LBA_LOW,  (uint8_t)(lba & 0xFF));
        outb(base + ATA_REG_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
        outb(base + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
        outb(base + ATA_REG_COMMAND,  ATA_CMD_READ_PIO_EXT);
    } else {
        outb(base + ATA_REG_ERROR,    0x00);
        outb(base + ATA_REG_SECCOUNT, 1);
        outb(base + ATA_REG_LBA_LOW,  (uint8_t)lba);
        outb(base + ATA_REG_LBA_MID,  (uint8_t)(lba >> 8));
        outb(base + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
        outb(base + ATA_REG_COMMAND,  ATA_CMD_READ_PIO);
    }

    ata_400ns_delay(base);

    uint8_t status = 0;
    if (ata_wait_drq_port(base, &status) != 0) {
        uint8_t err = inb(base + ATA_REG_ERROR);
        serial_write("[ATA ERROR] Read DRQ timeout or error! drive=");
        serial_write_dec(drive);
        serial_write(" port=");
        serial_write_hex16(base);
        serial_write(" status=");
        serial_write_hex8(status);
        serial_write(" err=");
        serial_write_hex8(err);
        serial_write("\n");
        return;
    }

    insl(base + ATA_REG_DATA, buf, 128);
}

void ata_write_sector(uint8_t drive, uint32_t lba, const uint8_t* buf) {
    if (!buf) return;

    uint16_t base = ata_base_port(drive);
    int use_lba48 = (lba > ATA_LBA28_MAX);
    uint8_t dev_head = use_lba48 ? ata_dev_select_bits_48(drive)
                                : ata_dev_select_bits_28(drive, lba);

    if (ata_wait_busy_port(base) != 0) {
        serial_write("[ATA ERROR] Write busy timeout on port ");
        serial_write_hex16(base);
        serial_write("\n");
        return;
    }

    outb(base + ATA_REG_HDDEVSEL, dev_head);
    ata_400ns_delay(base);
    ata_wait_busy_port(base);

    if (use_lba48) {
        outb(base + ATA_REG_SECCOUNT, 0x00);
        outb(base + ATA_REG_LBA_LOW,  (uint8_t)((lba >> 24) & 0xFF));
        outb(base + ATA_REG_LBA_MID,  0x00);
        outb(base + ATA_REG_LBA_HIGH, 0x00);

        outb(base + ATA_REG_SECCOUNT, 0x01);
        outb(base + ATA_REG_LBA_LOW,  (uint8_t)(lba & 0xFF));
        outb(base + ATA_REG_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
        outb(base + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
        outb(base + ATA_REG_COMMAND,  ATA_CMD_WRITE_PIO_EXT);
    } else {
        outb(base + ATA_REG_ERROR,    0x00);
        outb(base + ATA_REG_SECCOUNT, 1);
        outb(base + ATA_REG_LBA_LOW,  (uint8_t)lba);
        outb(base + ATA_REG_LBA_MID,  (uint8_t)(lba >> 8));
        outb(base + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
        outb(base + ATA_REG_COMMAND,  ATA_CMD_WRITE_PIO);
    }

    ata_400ns_delay(base);

    uint8_t status = 0;
    if (ata_wait_drq_port(base, &status) != 0) {
        uint8_t err = inb(base + ATA_REG_ERROR);
        serial_write("[ATA ERROR] Write DRQ timeout or error! drive=");
        serial_write_dec(drive);
        serial_write(" port=");
        serial_write_hex16(base);
        serial_write(" status=");
        serial_write_hex8(status);
        serial_write(" err=");
        serial_write_hex8(err);
        serial_write("\n");
        return;
    }

    outsl(base + ATA_REG_DATA, buf, 128);

    outb(base + ATA_REG_COMMAND, use_lba48 ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);
    ata_400ns_delay(base);
    ata_wait_busy_port(base);
}

void ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t* buf) {
    for (uint8_t i = 0; i < count; i++) {
        ata_read_sector(drive, lba + i, buf + (i * 512));
    }
}

void ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const uint8_t* buf) {
    for (uint8_t i = 0; i < count; i++) {
        ata_write_sector(drive, lba + i, buf + (i * 512));
    }
}