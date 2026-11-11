/* Replace player-owned OAM with immutable art; CHR-ROM and guest timing stay native. */
#include "mm4_proto.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <string.h>

typedef struct {
    char magic[4];
    uint8_t valid, owners[64];
    Mm4ProtoBody body;
} ViewFrame;
static ViewFrame s_build, s_frame;
static int s_enabled;
static uint32_t s_bg[256 * 240];
static uint8_t s_opaque[864 * 240], s_allowed[64][240];
static uint8_t s_claimed[864 * 240];

static int begin(uint16_t address) {
    (void)address;
    memset(&s_build, 0, sizeof s_build); memset(s_build.owners, 255, sizeof s_build.owners);
    return 0;
}
static int part(uint16_t address) {
    (void)address;
    CycModRegs r; cyc_mod_regs(&r);
    int slot = ((r.x + 252) & 255) / 4, owner = cyc_mod_peek(0x96);
    s_build.owners[slot] = (uint8_t)owner;
    if (!owner && !s_build.valid) { mm4_proto_body(&s_build.body); s_build.valid = 1; }
    return 0;
}
static int dma(uint16_t address) {
    (void)address;
    memcpy(&s_frame, &s_build, sizeof s_frame);
    return 0;
}
static uint32_t color(int index, const Mm4ProtoBody *body) {
    const CycProtoAtlas *atlas = mm4_proto_atlas();
    uint32_t c = atlas->colors[index - 1];
    unsigned r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
    if (body->weapon || body->charge >= 0x20) {
        if (!r && !g && !b) c = cyc_render_color_value(body->palette[1]);
        else if (r == g && g == b && r > 32 && r < 240) c = cyc_render_color_value(body->palette[2]);
        else if (r > 64 && r > g + b) c = cyc_render_color_value(body->palette[3]);
    }
    if (body->palette[1] == 0x0F && body->palette[2] == 0x0F && body->palette[3] == 0x0F)
        return 0xFF000000u;
    return c;
}
static void character_body(uint32_t *out, int W, int origin, const Mm4ProtoBody *b) {
    const CycProtoAtlas *atlas = mm4_proto_atlas();
    if (!atlas || b->pose >= CYC_PROTO_POSE_COUNT) return;
    const CycProtoPose *p = &atlas->poses[b->pose];
    for (int y = 0; y < p->height; ++y) for (int x = 0; x < p->width; ++x) {
        int index = p->pixels[y * 32 + x];
        if (!index) continue;
        int dx = x - p->origin_x, dy = y + p->top_y;
        int px = b->x + (b->right ? -dx - 1 : dx), py = b->y + (b->flipped ? -dy : dy + 1);
        if (origin + px < 0 || origin + px >= W || py < 0 || py >= 240 ||
            !(cyc_render_line_mask(py) & 16) || (px < 8 && !(cyc_render_line_mask(py) & 4)) ||
            s_claimed[py * W + origin + px]) continue;
        int at = py * W + origin + px;
        s_claimed[at] = 1;
        if (!b->behind || !s_opaque[at]) out[at] = color(index, b);
    }
}
static void sprite(uint32_t *out, int W, int origin, int slot, const uint8_t *oam) {
    const uint8_t *o = oam + slot * 4;
    int top = o[0] + 1, height = cyc_render_line_sprite16(top < 240 ? top : 239) ? 16 : 8;
    for (int y = 0; y < height; ++y) {
        int py = top + y;
        if (py >= 240 || !s_allowed[slot][py] || !(cyc_render_line_mask(py) & 16)) continue;
        int row = (o[2] & 128) ? height - y - 1 : y;
        uint16_t a = height == 16 ? (uint16_t)((o[1] & 1) * 4096 + ((o[1] & 254) + row / 8) * 16 + row % 8) :
            (uint16_t)(cyc_render_line_sprite_table(py) + o[1] * 16 + row);
        int lo = cyc_render_chr(a), hi = cyc_render_chr(a + 8);
        for (int x = 0; x < 8; ++x) {
            int px = o[3] + x, bit = (o[2] & 64) ? x : 7 - x;
            int v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            if (!v || origin + px >= W || (px < 8 && !(cyc_render_line_mask(py) & 4))) continue;
            int at = py * W + origin + px;
            if (s_claimed[at]) continue;
            s_claimed[at] = 1;
            if (!(o[2] & 32) || !s_opaque[at]) out[at] = cyc_render_color(16 + (o[2] & 3) * 4 + v);
        }
    }
}
static int render(uint32_t *out, int W, int H, int native_x0, const uint32_t *native, void *ctx) {
    (void)ctx; (void)native_x0;
    if (cyc_mod_peek(0x22) >= 16 || cyc_mod_peek(0x19) || cyc_mod_peek(0x1A)) return 0;
    if (!s_enabled || W < 256 || W > 864 || H != 240 || !cyc_render_background(s_bg)) return 0;
    int origin = (W - 256) / 2;
    int replace = s_enabled && s_frame.valid && s_frame.body.active;
    {
        if (!replace) return 0;
        memset(s_opaque, 0, sizeof s_opaque);
        for (int i = 0; i < W * H; ++i) out[i] = 0xFF000000u;
        for (int y = 0; y < H; ++y) memcpy(out + y * W + origin, s_bg + y * 256, 256 * sizeof *out);
    }
    const uint8_t *native_opaque = cyc_frame_bg_opaque();
    for (int y = 0; y < H; ++y) for (int x = 0; x < 256; ++x)
        if (origin+x>=0 && origin+x<W) s_opaque[y*W+origin+x]=native_opaque[y*256+x];
    const uint8_t *oam = cyc_render_oam();
    uint8_t counts[240] = {0};
    memset(s_allowed, 0, sizeof s_allowed);
    int first = 64;
    for (int slot = 0; slot < 64; ++slot) {
        int top = oam[slot * 4] + 1, height = cyc_render_line_sprite16(top < 240 ? top : 239) ? 16 : 8;
        for (int y = top; y < top + height && y < 240; ++y) s_allowed[slot][y] = counts[y]++ < 8;
        if (s_frame.owners[slot] == 0 && slot < first) first = slot;
    }
    cyc_render_set_native_origin(origin);
    memset(s_claimed, 0, (size_t)W * H);
    /* An opaque higher-priority sprite claims its pixel even behind terrain.
     * Otherwise a lower-priority foreground sprite can leak through it. */
    for (int slot = 0; slot < 64; ++slot) {
        int owner = s_frame.owners[slot];
        if (replace && owner == 0) { if (slot == first) character_body(out, W, origin, &s_frame.body); }
        else sprite(out, W, origin, slot, oam);
    }
    cyc_render_set_native_origin(origin); return 1;
}
void mm4_view_power(void) {
    memset(&s_build, 0, sizeof s_build); memset(s_build.owners, 255, sizeof s_build.owners);
    memset(&s_frame, 0, sizeof s_frame); memset(s_frame.owners, 255, sizeof s_frame.owners);
}
static void refresh(void) {
    int active = s_enabled;
    nes_mod_set_function_hook_enabled("mm4.view.begin", active);
    nes_mod_set_function_hook_enabled("mm4.view.part", active);
    nes_mod_set_function_hook_enabled("mm4.view.dma", active);
    cyc_render_set_compositor(active ? render : NULL, NULL);
    cyc_render_set_stock_compositing(s_enabled != 0);
    cyc_render_capture_background(active != 0);
}
void mm4_view_enable(int enabled) { s_enabled = enabled != 0; refresh(); }
static int get(uint8_t *out, int cap) {
    if (cap < sizeof s_frame + sizeof s_build) return -1;
    memcpy(s_build.magic, "MV01", 4); memcpy(s_frame.magic, "MV01", 4);
    memcpy(out, &s_build, sizeof s_build); memcpy(out + sizeof s_build, &s_frame, sizeof s_frame);
    return sizeof s_build + sizeof s_frame;
}
static int valid(const uint8_t *in, int len) {
    if (!len) return !(s_enabled);
    if (!in || len != sizeof s_build + sizeof s_frame) return 0;
    ViewFrame frames[2]; memcpy(frames, in, sizeof frames);
    for (int i = 0; i < 2; ++i) {
        const Mm4ProtoBody *b = &frames[i].body;
        if (memcmp(frames[i].magic, "MV01", 4) || frames[i].valid > 1 || b->pose >= CYC_PROTO_POSE_COUNT ||
            b->active > 1 || b->right > 1 || b->flipped > 1 || b->behind > 1 || b->weapon > 13) return 0;
        for (int j = 0; j < 64; ++j) if (frames[i].owners[j] != 255 && frames[i].owners[j] >= 24) return 0;
    }
    return 1;
}
static int set(const uint8_t *in, int len) {
    if (!valid(in, len)) return 0;
    if (!len) mm4_view_power();
    else { memcpy(&s_build, in, sizeof s_build); memcpy(&s_frame, in + sizeof s_build, sizeof s_frame); }
    return 1;
}
void mm4_view_register(void) {
    nes_mod_register_function_entry_plugin("mm4.view.begin", 0xDB3A, begin);
    nes_mod_register_function_entry_plugin("mm4.view.part", 0xDE39, part);
    nes_mod_register_function_entry_plugin("mm4.view.dma", 0xC065, dma);
    nes_mod_register_savestate_hook("mm4.view", get, set); nes_mod_register_savestate_validator("mm4.view", valid);
}
