#pragma once
#include "mm4_proto.h"
#include <stdint.h>
int mm4_coop_enabled(void);
int mm4_coop_seat(void);
int mm4_coop_character(void);
int mm4_coop_character_at(int seat);
int mm4_coop_live(int seat);
int mm4_coop_independent(void);
int mm4_coop_view_mode(void);
void mm4_coop_view_set(int mode);
int mm4_coop_view_rects(int left[2]);
int mm4_coop_scene_locked(void);
int mm4_coop_test_poke(const char *value);
int mm4_coop_native_scope(void);
int mm4_coop_menu_active(void);
/* Personal reads retain their seat during borrowed native actor AI; the
 * physical palette stays with the real anchor throughout that scope. */
int mm4_coop_read(int seat,uint16_t address);
/* Commit a helper's named personal result after restoring its native scope. */
int mm4_coop_write(int seat,uint16_t address,uint8_t value);
void mm4_coop_register(void);
void mm4_coop_set(int enabled,int roster);
void mm4_coop_power(void);
void mm4_coop_input(uint8_t buttons[2]);
void mm4_coop_end(void);
void mm4_coop_build(void);
void mm4_coop_latch(void);
void mm4_coop_draw(uint32_t *out,int width,int origin,const uint8_t *opaque,uint8_t *claimed);
int mm4_coop_json(char *out,int capacity);
int mm4_controls_dispatch(uint16_t address);
int mm4_controls_art(int weapon);
int mm4_controls_pattern(int weapon,unsigned address);
void mm4_view_coop(int enabled);
void mm4_view_draw_character(uint32_t *out,int width,int origin,const Mm4ProtoBody *body);
