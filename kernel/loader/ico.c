#include <kernel/loader/ico.h>
#include <kernel/mem/heap.h>
#include <kernel/string.h>
#include <kernel/serial.h>

// ICO Dosya Başlığı (ICONDIR) - 6 Bayt
typedef struct __attribute__((packed)) {
    uint16_t reserved;
    uint16_t type;
    uint16_t count;
} ico_header_t;

// ICO Görüntü Dizin Girişi (ICONDIRENTRY) - 16 Bayt
typedef struct __attribute__((packed)) {
    uint8_t  width;
    uint8_t  height;
    uint8_t  color_count;
    uint8_t  reserved;
    uint16_t planes;
    uint16_t bpp;
    uint32_t size;
    uint32_t offset;
} ico_dir_entry_t;

// Standart Windows Bitmap Bilgi Başlığı (BITMAPINFOHEADER) - 40 Bayt
typedef struct __attribute__((packed)) {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} bmp_info_header_t;

png_image_t* ico_load_from_memory(const uint8_t* file_data, uint32_t file_size) {
    if (!file_data || file_size < sizeof(ico_header_t)) {
        serial_write("[ICO ERROR] Gecersiz dosya veya yetersiz boyut!\n");
        return 0;
    }

    ico_header_t* header = (ico_header_t*)file_data;
    
    if (header->reserved != 0 || header->type != 1 || header->count == 0) {
        serial_write("[ICO ERROR] Gecersiz ICO basligi veya format tipi!\n");
        return 0;
    }

    uint32_t entries_end = sizeof(ico_header_t) + (header->count * sizeof(ico_dir_entry_t));
    if (entries_end > file_size) {
        serial_write("[ICO ERROR] Ikon dizin tablosu dosya boyutunu asiyor!\n");
        return 0;
    }

    ico_dir_entry_t* entries = (ico_dir_entry_t*)(file_data + sizeof(ico_header_t));

    ico_dir_entry_t* best_entry = &entries[0];
    for (uint16_t i = 0; i < header->count; i++) {
        if (entries[i].size > 0 && entries[i].offset > 0) {
            int w = (entries[i].width == 0) ? 256 : entries[i].width;
            best_entry = &entries[i];
            if (w == 48 || w == 32) {
                break;
            }
        }
    }

    if (best_entry->offset + best_entry->size > file_size) {
        serial_write("[ICO ERROR] Secilen ikonun veri alani dosya sinirlarini asiyor!\n");
        return 0;
    }

    const uint8_t* img_payload = file_data + best_entry->offset;

    // 1. DURUM: PNG Sıkıştırmalı Modern ICO Dosyaları
    if (best_entry->size >= 8 && 
        img_payload[0] == 0x89 && img_payload[1] == 'P' && 
        img_payload[2] == 'N' && img_payload[3] == 'G') {
        
        serial_write("[ICO] PNG sikistirmali ikon verisi algilandi ve decode ediliyor...\n");
        png_image_t* img = png_load_from_memory(img_payload, best_entry->size);
        if (!img) return 0;

        // PNG decoder RGBA döndürüyorsa BGRA'ya çevirelim (R ve B yer değiştiriyor)
        for (int i = 0; i < img->width * img->height; i++) {
            uint32_t p = img->pixels[i];
            uint8_t a = (p >> 24) & 0xFF;
            uint8_t r = (p >> 16) & 0xFF;
            uint8_t g = (p >> 8) & 0xFF;
            uint8_t b = p & 0xFF;
            
            img->pixels[i] = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
        }

        return img;
    }

    // 2. DURUM: Standart Windows Bitmap (BMP / DIB) Tabanlı ICO Dosyaları
    if (best_entry->size >= sizeof(bmp_info_header_t)) {
        bmp_info_header_t* bmi = (bmp_info_header_t*)img_payload;
        if (bmi->biSize == 40 && (bmi->biBitCount == 32 || bmi->biBitCount == 24)) {
            int width = bmi->biWidth;
            int height = bmi->biHeight / 2;
            if (height <= 0) height = bmi->biWidth;

            png_image_t* img = (png_image_t*)kmalloc(sizeof(png_image_t));
            if (!img) return 0;

            img->width = width;
            img->height = height;
            img->channels = 4;
            img->pixels = (uint32_t*)kmalloc(width * height * sizeof(uint32_t));
            
            if (!img->pixels) {
                kfree(img);
                return 0;
            }

            const uint8_t* pixel_data = img_payload + bmi->biSize;
            int row_stride = ((width * bmi->biBitCount + 31) / 32) * 4;

            for (int y = 0; y < height; y++) {
                int src_y = height - 1 - y;
                const uint8_t* row_ptr = pixel_data + src_y * row_stride;

                for (int x = 0; x < width; x++) {
                    uint8_t b, g, r, a = 255;
                    if (bmi->biBitCount == 32) {
                        b = row_ptr[x * 4 + 0];
                        g = row_ptr[x * 4 + 1];
                        r = row_ptr[x * 4 + 2];
                        a = row_ptr[x * 4 + 3];
                    } else {
                        b = row_ptr[x * 3 + 0];
                        g = row_ptr[x * 3 + 1];
                        r = row_ptr[x * 3 + 2];
                        a = 255;
                    }

                    // BMP'de zaten BGR geliyor, BGRA formatına doğrudan oturuyor
                    img->pixels[y * width + x] = ((uint32_t)a << 24) | 
                                                 ((uint32_t)r << 16) | 
                                                 ((uint32_t)g << 8) | 
                                                 b;
                }
            }

            serial_write("[ICO] Standart BMP tabanli ikon basariyla decode edildi!\n");
            return img;
        }
    }

    serial_write("[ICO ERROR] Desteklenmeyen ikon veri formati.\n");
    return 0;
}