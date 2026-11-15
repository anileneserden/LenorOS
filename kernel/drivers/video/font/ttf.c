#define STB_TRUETYPE_IMPLEMENTATION

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// --- KENDİ HAFİF MATEMATİK YARDIMCILARIMIZ ---
static inline float ttf_fabs(float x) {
    return x < 0.0f ? -x : x;
}

static inline float ttf_fmod(float a, float b) {
    if (b == 0.0f) return 0.0f;
    return a - (float)((int)(a / b)) * b;
}

static inline float ttf_acos(float x) {
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    float negate = x < 0.0f ? 1.0f : 0.0f;
    x = ttf_fabs(x);
    float ret = -0.0187293f;
    ret = ret * x + 0.0742610f;
    ret = ret * x - 0.2121144f;
    ret = ret * x + 1.5707288f;
    float temp = 1.0f - x;
    float root = temp * 0.5f;
    for (int i = 0; i < 3; i++) {
        if (root > 0.0f) root = (root + temp / root) * 0.5f;
    }
    ret = ret * root;
    if (negate) {
        ret = 3.141592653589793f - ret;
    }
    return ret;
}

// DOĞRUDAN FONKSİYON İSİMLERİNİ YAKALAYIP YÖNLENDİRİYORUZ:
#define acos(x)   ttf_acos(x)
#define fabs(x)   ttf_fabs(x)
#define fmod(x,y) ttf_fmod(x,y)

// stb_truetype makroları da garanti olsun diye bağlı kalmaya devam etsin:
#define STBTT_fabs(x)    ttf_fabs(x)
#define STBTT_fmod(x,y)  ttf_fmod(x,y)
#define STBTT_acos(x)    ttf_acos(x)

#include <kernel/drivers/video/font/ttf.h>
#include <kernel/external/stb_truetype.h>
#include <kernel/fs/vfs.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/mem/heap.h>
#include <kernel/serial.h>
#include <kernel/string.h>

#define MAX_LOADED_FONTS 4

typedef struct {
    uint8_t* file_buffer;     
    stbtt_fontinfo font_info; 
    float font_size;          
    bool active;
} LoadedFont;

static LoadedFont loaded_fonts[MAX_LOADED_FONTS];

/* UTF-8 kod noktasını (codepoint) doğru okuyan yardımcı fonksiyon */
static uint32_t ttf_utf8_next(const char** s_ptr) {
    const uint8_t* s = (const uint8_t*)*s_ptr;
    if (!s || !*s) return 0;

    uint32_t cp = 0;
    int bytes = 0;

    if (*s < 0x80) {
        cp = *s;
        bytes = 1;
    } else if ((*s & 0xE0) == 0xC0) {
        cp = *s & 0x1F;
        bytes = 2;
    } else if ((*s & 0xF0) == 0xE0) {
        cp = *s & 0x0F;
        bytes = 3;
    } else if ((*s & 0xF8) == 0xF0) {
        cp = *s & 0x07;
        bytes = 4;
    } else {
        (*s_ptr)++;
        return '?';
    }

    for (int i = 1; i < bytes; i++) {
        if ((s[i] & 0xC0) != 0x80) break;
        cp = (cp << 6) | (s[i] & 0x3F);
    }

    *s_ptr += bytes;
    return cp;
}

static void ttf_draw_pixel_alpha(int x, int y, uint32_t fg_color, uint8_t alpha) {
    if (alpha == 0) return;
    
    // Ekran sınırları dışına taşmayı önle
    if (x < 0 || y < 0 || x >= (int)fb_get_width() || y >= (int)fb_get_height()) return;

    if (alpha == 255) {
        gfx_putpixel(x, y, fg_color); 
        return;
    }

    uint32_t bg_color = gfx_getpixel(x, y); 

    uint32_t bg_r = (bg_color >> 16) & 0xFF;
    uint32_t bg_g = (bg_color >> 8) & 0xFF;
    uint32_t bg_b = bg_color & 0xFF;

    uint32_t fg_r = (fg_color >> 16) & 0xFF;
    uint32_t fg_g = (fg_color >> 8) & 0xFF;
    uint32_t fg_b = fg_color & 0xFF;

    uint32_t r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
    uint32_t g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
    uint32_t b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;

    uint32_t blended_color = (0xFF << 24) | (r << 16) | (g << 8) | b;
    gfx_putpixel(x, y, blended_color);
}

int ttf_load_font(const char* filepath) {
    int font_id = -1;
    for (int i = 0; i < MAX_LOADED_FONTS; i++) {
        if (!loaded_fonts[i].active) {
            font_id = i;
            break;
        }
    }
    if (font_id == -1) {
        serial_write("[TTF] Hata: Maksimum font sinirina ulasildi!\n");
        return -1;
    }

    uint32_t file_size = 0;
    uint8_t* buffer = (uint8_t*)vfs_read_file(filepath, &file_size); 
    if (!buffer || file_size == 0) {
        serial_write("[TTF] Hata: Font dosyasi VFS uzerinden okunamadi: ");
        serial_write(filepath);
        serial_write("\n");
        return -1;
    }

    LoadedFont* font = &loaded_fonts[font_id];
    font->file_buffer = buffer;
    
    if (!stbtt_InitFont(&font->font_info, font->file_buffer, stbtt_GetFontOffsetForIndex(font->file_buffer, 0))) {
        serial_write("[TTF] Hata: stbtt_InitFont basarisiz oldu!\n");
        kfree(font->file_buffer);
        return -1;
    }

    font->font_size = 16.0f; 
    font->active = true;
    
    serial_write("[TTF] Font basariyla yuklendi ve initialize edildi.\n");
    return font_id;
}

void ttf_set_font_size(int font_id, float pixels_height) {
    if (font_id < 0 || font_id >= MAX_LOADED_FONTS || !loaded_fonts[font_id].active) return;
    loaded_fonts[font_id].font_size = pixels_height;
}

void ttf_draw_char(int font_id, int x, int y, uint32_t color, uint32_t codepoint) {
    if (font_id < 0 || font_id >= MAX_LOADED_FONTS || !loaded_fonts[font_id].active) return;

    LoadedFont* font = &loaded_fonts[font_id];
    float fsize = (font->font_size > 0.0f) ? font->font_size : 16.0f;
    float scale = stbtt_ScaleForPixelHeight(&font->font_info, fsize);

    int w, h, xoff, yoff;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(&font->font_info, 0, scale, (int)codepoint, &w, &h, &xoff, &yoff);

    if (!bitmap) return; 

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font->font_info, &ascent, &descent, &lineGap);
    int baseline = (int)(ascent * scale);

    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            unsigned char alpha = bitmap[j * w + i];
            if (alpha == 0) continue; 

            int px = x + i + xoff;
            int py = y + j + baseline + yoff;

            ttf_draw_pixel_alpha(px, py, color, alpha);
        }
    }

    stbtt_FreeBitmap(bitmap, NULL);
}

void ttf_draw_text(int font_id, int x, int y, uint32_t color, const char* text) {
    if (font_id < 0 || font_id >= MAX_LOADED_FONTS || !loaded_fonts[font_id].active) return;
    if (!text) return;

    LoadedFont* font = &loaded_fonts[font_id];
    float fsize = (font->font_size > 0.0f) ? font->font_size : 16.0f;
    float scale = stbtt_ScaleForPixelHeight(&font->font_info, fsize);
    
    int cur_x = x;
    const char* ptr = text;

    while (*ptr != '\0') {
        uint32_t cp = ttf_utf8_next(&ptr);
        if (cp == 0) break;

        if (cp == '\n') {
            cur_x = x;
            y += (int)(fsize * 1.2f);
            continue;
        }

        // Karakteri çiz
        ttf_draw_char(font_id, cur_x, y, color, cp);

        // Yatay ilerleme (Advance) miktarını güvenli şekilde hesapla
        int advance = 0, lsb = 0;
        int glyph_index = stbtt_FindGlyphIndex(&font->font_info, (int)cp);
        stbtt_GetGlyphHMetrics(&font->font_info, glyph_index, &advance, &lsb);

        int pixel_advance = (int)(advance * scale);
        
        // Eğer font metrikten genişlik alamazsa (0 veya negatif dönerse) karakter boyutuna göre emniyet payı bırak
        if (pixel_advance <= 0) {
            pixel_advance = (int)(fsize * 0.55f); 
        }

        cur_x += pixel_advance;
    }
}