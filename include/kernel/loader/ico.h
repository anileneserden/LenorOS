#ifndef ICO_H
#define ICO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <kernel/loader/png.h>

// ICO dosyasını bellekten okuyup png_image_t formatına (RGBA piksellerine) dönüştürür
png_image_t* ico_load_from_memory(const uint8_t* file_data, uint32_t file_size);

#endif