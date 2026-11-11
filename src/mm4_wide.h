#pragma once
#include <stdint.h>
void mm4_wide_register(void);
void mm4_wide_power(void);
void mm4_wide_set(int enabled,const char *aspect);
void mm4_wide_begin(void);
void mm4_wide_latch(void);
int mm4_wide_background(uint32_t *out,int width,int height,const uint32_t *native,uint8_t *opaque,int *origin);
void mm4_wide_draw(uint32_t *out,int width,int origin,const uint8_t *opaque,uint8_t *claimed,int rank);
int mm4_wide_rank(int owner);
int mm4_wide_json(char *out,int cap);
void mm4_view_wide(int enabled);
