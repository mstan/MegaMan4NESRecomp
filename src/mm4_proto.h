#pragma once
#include "cyc_protoman_atlas.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    int16_t x, y;
    uint8_t pose, right, flipped, behind, active, state, weapon, charge;
    uint8_t palette[8], master[8];
} Mm4ProtoBody;
bool mm4_proto_set(int enabled, int mode, int start);
void mm4_proto_register(void);
void mm4_proto_power(void);
void mm4_proto_end(void);
int mm4_proto_json(char *out, int capacity);
const CycProtoAtlas *mm4_proto_atlas(void);
int mm4_proto_active(void);
int mm4_proto_selected(void);
int mm4_proto_swapping(void);
void mm4_proto_body(Mm4ProtoBody *body);
void mm4_view_power(void);
void mm4_view_register(void);
void mm4_view_enable(int enabled);
int mm4_proto_dispatch(void);
int mm4_proto_enabled(void);
void mm4_controls_refresh(void);
void mm4_proto_cancel_swap(void);
/* Called with the guest projected, before its bounded collision scope. */
void mm4_proto_guest_collision(int actor);
void mm4_proto_guest_collision_end(void);
