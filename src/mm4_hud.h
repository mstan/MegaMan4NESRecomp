#pragma once
#include <stdint.h>
void mm4_hud_register(void);
void mm4_hud_reset(void);
int mm4_hud_capture(void);
void mm4_hud_latch(void);
int mm4_hud_native_meter(const uint8_t *part);
void mm4_hud_draw(uint32_t *out,int width,int height,int own);
