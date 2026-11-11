#pragma once
#include <stdint.h>
typedef struct { uint8_t tile,palette,metatile; } Mm4WorldTile;
unsigned mm4_cart_prg(unsigned bank,unsigned address);
int mm4_cart_ready(void);
int mm4_world_tile(int screen,int x,int y,Mm4WorldTile *out);
int mm4_world_class(int world_x,int y);
int mm4_world_audit_file(const char *path);
void mm4_world_audit(const uint32_t *background);
