// kernel/kmain.c
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/serial.h>
#include <kernel/multiboot.h>
#include <kernel/crypto/tls.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/fs/lnrfs.h>
#include <kernel/fs/fat32.h>
#include <kernel/fs/vfs.h>
#include <kernel/drivers/audio/ac97.h>
#include <kernel/drivers/audio/pcspeaker.h>
#include <kernel/drivers/pci.h>
#include <kernel/drivers/net/wifi/rtl8192eu.h>
#include <kernel/drivers/usb/core.h>
#include <ui/cursor.h>
#include <ui/desktop.h>
#include <ui/serial_screen.h>
#include <kernel/app.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/interrupts.h>
#include <kernel/power.h>
#include <arch/x86/io.h>
#include <kernel/hexdump.h>
#include <kernel/loader/lex2.h>
#include <kernel/drivers/video/font/ttf.h> // TrueType font kütüphanesi eklendi

// serial.c içerisindeki mirror fonksiyonunun prototipi
extern void serial_screen_mirror_enable(bool enable);

#define LOAD_FONT_SCREEN 0 // 1 yaparsan saf font test ekranı açılır, 0 yaparsan normal masaüstü yüklenir

void kernel_main(uint32_t mboot_magic, uint32_t* mboot_info_addr) {
    serial_init();
    serial_write("LenorOS started!\n");

    if (mboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_write("ERROR: Invalid magic number!\n");
        return;
    }

    multiboot_info_t* mboot = (multiboot_info_t*)mboot_info_addr;

    bool debug_mode = false;
    if ((mboot->flags & (1 << 2)) && mboot->cmdline) {
        char* cmdline = (char*)mboot->cmdline;
        for (int i = 0; cmdline[i] != '\0'; i++) {
            if (cmdline[i] == 'd' && cmdline[i+1] == 'e' && cmdline[i+2] == 'b' && cmdline[i+3] == 'u' && cmdline[i+4] == 'g') {
                debug_mode = true;
                break;
            }
        }
    }

    // 1. Memory and heap initialization
    power_init(mboot);
    pmm_init(mboot);
    vmm_init();
    heap_init(0x2000000, 1024 * 1024 * 16);

    // Framebuffer başlatılıyor
    fb_init(mboot);

    if (debug_mode) {
        serial_screen_init();
        serial_screen_mirror_enable(true); 
        serial_screen_write("DEBUG MODE ACTIVE: Screen & Mirror initialized.\n");
    }

    // 2. PCI, display, and speaker hardware drivers
    pci_init();
    ata_init();

    e1000_init();

    usb_init();
    rtl8192eu_init();

    usb_notify_device_connect(0x0BDA, 0x818B);

    // 3. Filesystems and VFS Initialization
    vfs_init();
    
    if (fat32_init_disk(1, 0)) {
        fs_driver_t fat32_driver = fat32_get_driver();
        vfs_mount('D', "FAT32_VOL", fat32_driver);
        serial_write("FAT32 mounted to D:\\\n");
        run_tls_mock_test();
    } else {
        serial_write("FAT32: Driver could not be started!\n");
    }

    // MBR'den sda2 (LNRFS) offset değerini al
    uint32_t lnrfs_lba_offset = mbr_get_lnrfs_offset();
    
    // Global font kimliğimiz (başarısız olursa -1 kalır)
    int system_font_id = -1;

    // Offset ile mount et
    if (lnrfs_mount_at_offset(lnrfs_lba_offset) == 0) {
        fs_driver_t lnrfs_driver = lnrfs_get_driver();
        vfs_mount('C', "LNRFS_VOL", lnrfs_driver);
        serial_write("LNRFS mounted successfully and registered to VFS on C:\\.\n");
        run_tls_mock_test();

        // 4. VFS hazır olduğu için arial.ttf dosyasını yüklüyoruz
        system_font_id = ttf_load_font("C:/Lenor/System32/fonts/arial.ttf");
        if (system_font_id >= 0) {
            serial_write("[TTF] arial.ttf basariyla bellege yuklendi!\n");
            ttf_set_font_size(system_font_id, 18.0f); // Font boyutunu 18 piksel olarak ayarlıyoruz
        } else {
            serial_write("[TTF ERROR] arial.ttf dosyasi VFS uzerinden yuklenemedi!\n");
        }
    } else {
        serial_write("LNRFS: Driver could not be started!\n");
    }

    serial_write("[input] USB input active via Core.\n");

    // 5. Graphical Interface Initialization and Initial Drawing
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width > 0 && height > 0) {
        if (debug_mode) {
            serial_screen_write("Debug screen fully loaded.\n");
            while (1) {
            }
        } else {
#if LOAD_FONT_SCREEN
            fb_clear(0xFF1E1E2E); // Font test ekranı için koyu arkaplan
            // --- SAF FONT TEST MODU ---
            if (system_font_id >= 0) {
                int start_x = 50;
                int current_y = 50;

                // 1. Büyük Başlık (28px)
                ttf_set_font_size(system_font_id, 28.0f);
                ttf_draw_text(system_font_id, start_x, current_y, 0xFFFFFFFF, "LenorOS TrueType Font Engine");
                current_y += 45;

                // 2. Alt Başlık (20px)
                ttf_set_font_size(system_font_id, 20.0f);
                ttf_draw_text(system_font_id, start_x, current_y, 0xFF89B4FA, "TrueType Vector Font Test Active");
                current_y += 35;

                // 3. Normal Metin (16px)
                ttf_set_font_size(system_font_id, 16.0f);
                ttf_draw_text(system_font_id, start_x, current_y, 0xFFA6E3A1, "stb_truetype entegrasyonu basariyla tamamlandi.");
                current_y += 30;

                ttf_draw_text(system_font_id, start_x, current_y, 0xFFFAB387, "VFS uzerinden arial.ttf okunuyor ve alpha-blending ile ciziliyor.");
                current_y += 30;

                // 4. Küçük Metin / Detay (12px)
                ttf_set_font_size(system_font_id, 12.0f);
                ttf_draw_text(system_font_id, start_x, current_y, 0xFF6C7086, "FreeType bagimliligi olmadan saf C ile bare-metal vektor render.");
            } else {
                ttf_set_font_size(system_font_id, 16.0f);
                ttf_draw_text(system_font_id, 50, 50, 0xFFFF5555, "HATA: Arial.ttf yuklenemedi!");
            }
#else
            // --- ORİJİNAL MASAÜSTÜ MODU (Hiçbir şey silinmedi) ---
            fb_clear(0xFF0000FF);
            desktop_init(); 
#endif
        }
    }

    app_manager_init();
    fb_swap();

    if (lex2_load_and_run("C:/Program Files/html-parser/html-parser.lex")) {
        serial_write("html-parser uygulamasi basariyla calistirildi!\n");
    } else {
        serial_write("HATA: html-parser uygulamasi yuklenemedi!\n");
    }

    // 6. AC97 audio driver initialization
    if (ac97_init() == 0) {
        ac97_set_master_volume(100);
        serial_write("AC97: Driver initialized and ready.\n");
    }

    interrupts_init();
    serial_write("System initialized; idle loop polls USB events via Core.\n");

    // 7. Main event loop (GUI active loop with centralized USB polling)
    while (1) {
        uint8_t input_updated = 0;

        input_updated = usb_poll();

        desktop_process_input();
        app_manager_update_all();

        cursor_update_and_redraw();
    }
}