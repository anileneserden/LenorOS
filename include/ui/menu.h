#ifndef MENU_H
#define MENU_H

#include <stdint.h>
#include <stdbool.h>

// Menü elemanı yapısı (Dizi elemanları için)
typedef struct {
    char text[32];
    void (*action)(void* context); // Seçildiğinde çalışacak fonksiyon ve hedef bağlam (context)
} menu_item_t;

// Dinamik menü yapısı
typedef struct {
    int x, y;
    int width, height;
    bool is_open;
    menu_item_t items[8];
    int item_count;
    void* context_target; // Hangi nesne/pencere için açıldıysa onun referansı
} context_menu_t;

// Fonksiyon prototipleri
void menu_show(int x, int y, menu_item_t* items, int count, void* context);
void menu_draw(void);
bool menu_handle_click(int mouse_x, int mouse_y);
bool menu_is_open(void);

#endif