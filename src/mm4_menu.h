#pragma once
#include <stdint.h>
void mm4_menu_register(void);
void mm4_menu_reset(void);
int mm4_menu_capture(void);
void mm4_menu_latch(void);
int mm4_menu_draw(uint32_t *out,int width,int height);
