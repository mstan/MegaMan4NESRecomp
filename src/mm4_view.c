/* Replace player-owned OAM with immutable art; CHR-ROM and guest timing stay native. */
#include "mm4_proto.h"
#include "mm4_world.h"
#include "mm4_wide.h"
#include "mm4_coop.h"
#include "mm4_hud.h"
#include "mm4_menu.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <string.h>

typedef struct {
    char magic[4];
    uint8_t valid, owners[64];
    uint8_t panels,native_scene;
    int16_t left[2];
    uint8_t tiles[2][30][33],palettes[2][30][33],present[2][30][33];
    Mm4ProtoBody body;
    uint8_t rain_colors[4];
} ViewFrame;
static ViewFrame s_build, s_frame;
static int s_enabled,s_wide,s_coop,s_no_flicker,s_capture_policy=-1,s_panel_drawing;
static uint32_t s_bg[256 * 240];
static uint8_t s_opaque[864 * 240], s_allowed[64][240];
static uint8_t s_claimed[864 * 240];
static uint32_t s_panel[2][256*240];
static void panels_build(void) {
    if(!mm4_coop_independent() || cyc_mod_peek(0x22)>=16)return;
    int left[2];if(!mm4_coop_view_rects(left))return;
    s_build.panels=1;
    s_build.native_scene=(uint8_t)(mm4_coop_scene_locked() || (cyc_mod_peek(0x2B)&0xC0) || (cyc_mod_peek(0x132)&128));
    int lo=(cyc_mod_peek(0xF9)-cyc_mod_peek(0x2E))*256,hi=lo+(cyc_mod_peek(0x2D)+1)*256;
    for(int seat=0;seat<2;seat++) {
        s_build.left[seat]=(int16_t)left[seat];
        for(int y=0;y<30;y++)for(int x=0;x<33;x++) {
            int wx=(left[seat]&~7)+x*8;Mm4WorldTile t;
            if(wx<lo || wx>=hi || !mm4_world_tile(wx/256,wx&255,y*8,&t))continue;
            s_build.tiles[seat][y][x]=t.tile;s_build.palettes[seat][y][x]=t.palette;s_build.present[seat][y][x]=1;
        }
    }
}

static int begin(uint16_t address) {
    (void)address;
    if(cyc_mod_isolated() || mm4_coop_source_pending())return 0;
    memset(&s_build, 0, sizeof s_build); memset(s_build.owners, 255, sizeof s_build.owners);
    mm4_wide_begin();
    mm4_coop_build();
    panels_build();
    mm4_hud_capture();
    return 0;
}
static int part(uint16_t address) {
    (void)address;
    if(cyc_mod_isolated())return 0;
    CycModRegs r; cyc_mod_regs(&r);
    int slot = ((r.x + 252) & 255) / 4, owner = cyc_mod_peek(0x96);
    s_build.owners[slot] = (uint8_t)owner;
    if (!owner && !s_build.valid) { mm4_proto_body(&s_build.body); s_build.valid = 1; }
    return 0;
}
static int dma(uint16_t address) {
    (void)address;
    if(cyc_mod_isolated())return 0;
    /* DF5F overwrites OAM0..29 after actor ownership was captured. Identify
     * its exact native packet, including the final zero-timer frame. */
    int progress=cyc_mod_peek(0x13B),phase=progress-4;
    int rain=progress>=4 && progress<=240 && !(progress&3) && cyc_mod_peek(0x145)<16;
    for(int slot=0;slot<30 && rain;slot++)rain=
        cyc_mod_peek(0x200+slot*4)==(uint8_t)(mm4_cart_prg(0x3E,0xDFBE+slot*2)+phase) &&
        cyc_mod_peek(0x201+slot*4)==0x99 && !cyc_mod_peek(0x202+slot*4) &&
        cyc_mod_peek(0x203+slot*4)==(uint8_t)(mm4_cart_prg(0x3E,0xDFBF+slot*2)-phase);
    if(rain) {
        int owner=-1;
        if(mm4_coop_enabled())for(int seat=0;seat<2;seat++)if(mm4_coop_read(seat,0xA0)==4){owner=seat;break;}
        for(int i=0;i<4;i++)s_build.rain_colors[i]=(uint8_t)((owner<0?cyc_mod_peek(0x610+i):mm4_coop_read(owner,(uint16_t)(0x610+i)))&63);
        for(int slot=0;slot<30;slot++)s_build.owners[slot]=254;
    }
    memcpy(&s_frame, &s_build, sizeof s_frame);
    mm4_wide_latch();
    mm4_coop_latch();
    mm4_hud_latch();
    mm4_menu_latch();
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
static uint32_t menu_color(uint32_t c) {
    uint32_t light=cyc_render_color(1),white=cyc_render_color_value(0x30),out=0xff000000u;
    for(int shift=0;shift<=16;shift+=8) {
        unsigned value=(c>>shift)&255,level=(light>>shift)&255,full=(white>>shift)&255;
        value=full?value*level/full:0;if(value>255)value=255;out|=value<<shift;
    }
    return out;
}
void mm4_view_menu_character(uint32_t *out,int W,int origin,int cursor,int character,const uint8_t colors[8]) {
    const CycProtoAtlas *atlas=mm4_proto_atlas();
    if(!character || !atlas)return;
    /* Native life icon: two adjacent 8x16 parts at184,192 (9E51 OAM). */
    for(int y=0;y<16;y++)for(int x=0;x<16;x++) {
        uint32_t c=atlas->portrait[y*16+x];
        out[(192+y)*W+origin+184+x]=c?menu_color(c):0xff000000u;
    }
    int weapon=cursor<16?mm4_cart_prg(0x3c,0x9b71+cursor):255;
    /* The E-tank row uses the same native body preview as Buster. */
    if(cursor==7)weapon=0;
    /* Keep Rush Coil/Marine/Jet, Wire and Balloon's original previews. */
    if(weapon==0 || weapon==4 || (weapon>=7 && weapon<=13)) {
        const CycProtoPose *p=&atlas->poses[CYC_PROTO_IDLE0];
        Mm4ProtoBody b={0};b.weapon=(uint8_t)(weapon!=0);
        for(int i=0;i<8;i++)b.palette[i]=colors[i];
        for(int y=178;y<218;y++)for(int x=108;x<148;x++)out[y*W+origin+x]=0xff000000u;
        for(int y=0;y<p->height;y++)for(int x=0;x<p->width;x++) {
            int index=p->pixels[y*32+x];if(!index)continue;
            int px=128+x-p->origin_x,py=200+p->top_y+y;
            out[py*W+origin+px]=menu_color(color(index,&b));
        }
    }
}
static int inventory(uint32_t *out,int W,int H,const uint32_t *native) {
    if(!s_enabled || !mm4_proto_selected() || !mm4_proto_atlas() || W<256 || H!=240 || !native)return 0;
    int origin=(W-256)/2;uint8_t colors[8];
    for(int i=0;i<W*H;i++)out[i]=0xff000000u;
    for(int y=0;y<H;y++)memcpy(out+y*W+origin,native+y*256,256*sizeof *out);
    for(int i=0;i<8;i++)colors[i]=cyc_mod_peek(0x610+i);
    mm4_view_menu_character(out,W,origin,cyc_mod_peek(0x138),1,colors);
    cyc_render_set_native_origin(origin);return 1;
}
static void character_body(uint32_t *out, int W, int origin, const Mm4ProtoBody *b) {
    const CycProtoAtlas *atlas = mm4_proto_atlas();
    if (!atlas || b->pose >= CYC_PROTO_POSE_COUNT) return;
    int marine=b->state==4 && b->weapon==3;
    const CycProtoPose *p = &atlas->poses[marine?CYC_PROTO_IDLE0:b->pose];
    for (int y = 0; y < p->height; ++y) for (int x = 0; x < p->width; ++x) {
        if(marine && (y<8 || y>=22))continue;
        int index = p->pixels[y * 32 + x];
        if (!index) continue;
        int dx = x - p->origin_x, dy = y + p->top_y;
        int px = b->x + (b->right ? -dx - 1 : dx), py = b->y + (b->flipped ? -dy : dy + 1);
        if(marine) {
            px=b->x+(b->right?15-x:x-16);
            if(px<b->x-8 || px>=b->x+8)continue;
        }
        if (origin + px < 0 || origin + px >= W || py < 0 || py >= 240 ||
            !(cyc_render_line_mask(py) & 16) || (!s_panel_drawing && px < 8 && !(cyc_render_line_mask(py) & 4)) ||
            s_claimed[py * W + origin + px]) continue;
        int at = py * W + origin + px;
        s_claimed[at] = 1;
        if (!b->behind || !s_opaque[at]) out[at] = color(index, b);
    }
}
int mm4_view_native_body_pixel(const Mm4ProtoBody *b,int origin,int x,int y) {
    if(b->state!=4 || b->weapon!=3)return 0;
    const CycProtoAtlas *atlas=mm4_proto_atlas();if(!atlas)return 1;
    int left=origin+b->x-8,top=b->y+1+atlas->poses[CYC_PROTO_IDLE0].top_y+8;
    return x<left || x>=left+16 || y<top || y>=top+14;
}
void mm4_view_draw_character(uint32_t *out,int W,int origin,const Mm4ProtoBody *b) {character_body(out,W,origin,b);}
static void sprite(uint32_t *out, int W, int origin, int slot, const uint8_t *oam,const Mm4ProtoBody *body,int mask_origin) {
    const uint8_t *o = oam + slot * 4;
    int top = o[0] + 1, height = cyc_render_line_sprite16(top < 240 ? top : 239) ? 16 : 8;
    for (int y = 0; y < height; ++y) {
        int py = top + y;
        if (py >= 240 || !s_allowed[slot][py] || !(cyc_render_line_mask(py) & 16)) continue;
        int row = (o[2] & 128) ? height - y - 1 : y;
        uint16_t a = height == 16 ? (uint16_t)((o[1] & 1) * 4096 + ((o[1] & 254) + row / 8) * 16 + row % 8) :
            (uint16_t)(cyc_render_line_sprite_table(py) + o[1] * 16 + row);
        int rain=s_frame.owners[slot]==254;
        int lo = rain?mm4_controls_pattern(4,a):cyc_render_chr(a), hi = rain?mm4_controls_pattern(4,a+8):cyc_render_chr(a + 8);
        for (int x = 0; x < 8; ++x) {
            int px = o[3] + x, bit = (o[2] & 64) ? x : 7 - x;
            int v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            if (!v || origin + px < 0 || origin + px >= W || (origin+px>=mask_origin && origin+px<mask_origin+8 && !(cyc_render_line_mask(py) & 4))) continue;
            if(body && !mm4_view_native_body_pixel(body,origin,origin+px,py))continue;
            int at = py * W + origin + px;
            if (s_claimed[at]) continue;
            s_claimed[at] = 1;
            if (!(o[2] & 32) || !s_opaque[at]) out[at] = rain?cyc_render_color_value(s_frame.rain_colors[v]):cyc_render_color(16 + (o[2] & 3) * 4 + v);
        }
    }
}
static void sprite_limits(const uint8_t *oam) {
    uint8_t counts[240]={0};memset(s_allowed,0,sizeof s_allowed);
    int complete=s_no_flicker || mm4_coop_independent();
    for(int slot=0;slot<64;slot++) {
        int top=oam[slot*4]+1,height=cyc_render_line_sprite16(top<240?top:239)?16:8;
        for(int y=top;y<top+height && y<240;y++)s_allowed[slot][y]=(uint8_t)(complete || counts[y]++<8);
    }
}
static void rain_draw(uint32_t *out,int width,int origin) {
    if(s_frame.owners[0]!=254)return;
    /* Rain has the native packet's first OAM priority. Draw it after the
     * complete player artwork, then retain the presentation HUD overlay. */
    memset(s_claimed,0,(size_t)width*240);
    const uint8_t *oam=cyc_render_oam();
    for(int slot=0;slot<30;slot++)sprite(out,width,origin,slot,oam,NULL,origin);
    if(width>256) {
        /* Repeat the original rain in adaptive margins without adding any
         * pixels to the preserved original viewport. */
        for(int y=0;y<240;y++)for(int x=origin;x<origin+256;x++)if(x>=0 && x<width)s_claimed[y*width+x]=1;
        for(int shift=-256;shift<=256;shift+=512)for(int slot=0;slot<30;slot++)sprite(out,width,origin+shift,slot,oam,NULL,origin);
    }
}
static void panel_scene(uint32_t *out,int seat) {
    int left=s_frame.left[seat],phase=left&7;
    memset(s_claimed,0,256*240);memset(s_opaque,0,256*240);s_panel_drawing=1;
    const uint8_t *opaque=cyc_frame_bg_opaque();
    for(int y=0;y<240;y++)for(int x=0;x<256;x++) {
        int at=y*256+x,v=0,cell=(phase+x)/8,bit=7-((phase+x)&7);
        if(s_frame.native_scene){out[at]=s_bg[at];s_opaque[at]=opaque[at];continue;}
        if(s_frame.present[seat][y/8][cell] && (cyc_render_line_mask(y)&8)) {
            int a=cyc_render_line_bg_table(y)+s_frame.tiles[seat][y/8][cell]*16+(y&7);
            v=((cyc_render_chr((uint16_t)a)>>bit)&1)|(((cyc_render_chr((uint16_t)(a+8))>>bit)&1)<<1);
        }
        out[at]=cyc_render_color(v?s_frame.palettes[seat][y/8][cell]*4+v:0);s_opaque[at]=(uint8_t)(v!=0);
    }
    int origin=mm4_wide_camera()-left;
    for(int rank=1;rank<=24;rank++)mm4_wide_draw(out,256,origin,s_opaque,s_claimed,rank);
    mm4_coop_draw(out,256,origin,s_opaque,s_claimed);
    rain_draw(out,256,0);
    mm4_hud_draw(out,256,240,seat);s_panel_drawing=0;
}
static int panels(uint32_t *out,int width,int height) {
    int mode=mm4_coop_view_mode();if(!mode || !s_frame.panels || width<256 || height!=240)return 0;
    if(mode==1) {
        panel_scene(s_panel[0],0);panel_scene(s_panel[1],1);int half=width/2;
        for(int y=0;y<240;y++)for(int x=0;x<width;x++) {
            int seat=x>=half,local=x-(seat?half:0),span=seat?width-half:half;
            out[y*width+x]=s_panel[seat][y*256+local*256/span];
        }
    }else {
        int seat=mode==3;panel_scene(s_panel[seat],seat);
        for(int y=0;y<240;y++)for(int x=0;x<width;x++)out[y*width+x]=s_panel[seat][y*256+x*256/width];
    }
    cyc_render_set_native_origin(0);return 1;
}
static int render(uint32_t *out, int W, int H, int native_x0, const uint32_t *native, void *ctx) {
    if(mm4_coop_global_screen())return 0;
    (void)ctx; (void)native_x0;
    sprite_limits(cyc_render_oam());
    if(cyc_mod_peek(0x22)==16){
        if(mm4_menu_draw(out,W,H))return 1;
        int personal=s_coop?mm4_coop_menu_active():cyc_mod_peek(0x30)<=4;
        return personal?inventory(out,W,H,native):0;
    }
    if(!mm4_coop_menu_active() && cyc_render_background(s_bg)) {
        if(!mm4_coop_source_pending())mm4_world_audit(s_bg);
        if(panels(out,W,H))return 1;
    }
    if (cyc_mod_peek(0x22) >= 16 || cyc_mod_peek(0x19) || cyc_mod_peek(0x1A)) return 0;
    if (!(s_enabled || s_wide || s_coop || s_no_flicker) || W < 256 || W > 864 || H != 240 || !cyc_render_background(s_bg)) return 0;
    int origin = (W - 256) / 2;
    int replace = s_enabled && s_frame.valid && s_frame.body.active;
    memset(s_opaque, 0, sizeof s_opaque);
    int wide=mm4_wide_background(out,W,H,s_bg,s_opaque,&origin);
    {
        if (!replace && !wide && !s_coop && !s_no_flicker) return 0;
        if(!wide) {
            for (int i = 0; i < W * H; ++i) out[i] = 0xFF000000u;
            for (int y = 0; y < H; ++y) memcpy(out + y * W + origin, s_bg + y * 256, 256 * sizeof *out);
        }
    }
    const uint8_t *native_opaque = cyc_frame_bg_opaque();
    for (int y = 0; y < H; ++y) for (int x = 0; x < 256; ++x)
        if (origin+x>=0 && origin+x<W) s_opaque[y*W+origin+x]=native_opaque[y*256+x];
    const uint8_t *oam = cyc_render_oam();
    int first = 64;
    for (int slot = 0; slot < 64; ++slot) {
        if (s_frame.owners[slot] == 0 && slot < first) first = slot;
    }
    cyc_render_set_native_origin(origin);
    memset(s_claimed, 0, (size_t)W * H);
    /* An opaque higher-priority sprite claims its pixel even behind terrain.
     * Otherwise a lower-priority foreground sprite can leak through it. */
    int drawn_rank=0;
    for (int slot = 0; slot < 64; ++slot) {
        int owner = s_frame.owners[slot];
        if(owner==254)continue;
        int rank=mm4_wide_rank(owner);
        if(rank!=255)while(drawn_rank<rank)mm4_wide_draw(out,W,origin,s_opaque,s_claimed,++drawn_rank);
        if(s_no_flicker && owner<24 && rank!=255)continue;
        if (replace && owner == 0) {
            if (slot == first) character_body(out, W, origin, &s_frame.body);
            if(s_frame.body.state==4 && s_frame.body.weapon==3)sprite(out,W,origin,slot,oam,&s_frame.body,origin);
        }
        else if(!mm4_hud_native_meter(oam+slot*4))sprite(out, W, origin, slot, oam,NULL,origin);
    }
    while(drawn_rank<24)mm4_wide_draw(out,W,origin,s_opaque,s_claimed,++drawn_rank);
    mm4_coop_draw(out,W,origin,s_opaque,s_claimed);
    rain_draw(out,W,origin);
    mm4_hud_draw(out,W,H,-1);
    cyc_render_set_native_origin(origin); return 1;
}
void mm4_view_power(void) {
    memset(&s_build, 0, sizeof s_build); memset(s_build.owners, 255, sizeof s_build.owners);
    memset(&s_frame, 0, sizeof s_frame); memset(s_frame.owners, 255, sizeof s_frame.owners);
    mm4_hud_reset();
    s_capture_policy=s_enabled || s_wide || s_coop || s_no_flicker;cyc_render_capture_background(s_capture_policy!=0);
}
static void refresh(void) {
    int active = s_enabled || s_wide || s_coop || s_no_flicker;
    nes_mod_set_function_hook_enabled("mm4.view.begin", active);
    nes_mod_set_function_hook_enabled("mm4.view.part", active);
    nes_mod_set_function_hook_enabled("mm4.view.dma", active);
    cyc_render_set_compositor(active ? render : NULL, NULL);
    cyc_render_set_stock_compositing(active != 0);
    if(s_capture_policy!=active){cyc_render_capture_background(active!=0);s_capture_policy=active;}
}
void mm4_view_enable(int enabled) { s_enabled = enabled != 0; refresh(); }
void mm4_view_wide(int enabled) {s_wide=enabled!=0;refresh();}
void mm4_view_coop(int enabled) {s_coop=enabled!=0;refresh();}
void mm4_view_no_flicker(int enabled) {s_no_flicker=enabled!=0;refresh();}
static int get(uint8_t *out, int cap) {
    if (cap < sizeof s_frame + sizeof s_build) return -1;
    memcpy(s_build.magic, "MV03", 4); memcpy(s_frame.magic, "MV03", 4);
    memcpy(out, &s_build, sizeof s_build); memcpy(out + sizeof s_build, &s_frame, sizeof s_frame);
    return sizeof s_build + sizeof s_frame;
}
static int valid(const uint8_t *in, int len) {
    if (!len) return !(s_enabled || s_wide || s_coop || s_no_flicker);
    if (!in || len != sizeof s_build + sizeof s_frame) return 0;
    ViewFrame frames[2]; memcpy(frames, in, sizeof frames);
    for (int i = 0; i < 2; ++i) {
        const Mm4ProtoBody *b = &frames[i].body;
        if (memcmp(frames[i].magic, "MV03", 4) || frames[i].valid > 1 || frames[i].panels>1 || frames[i].native_scene>1 || b->pose >= CYC_PROTO_POSE_COUNT ||
            b->active > 1 || b->right > 1 || b->flipped > 1 || b->behind > 1 || b->weapon > 13) return 0;
        int rain=frames[i].owners[0]==254;
        for (int j = 0; j < 64; ++j) {
            int owner=frames[i].owners[j];
            if(owner==254){if(!rain || j>=30)return 0;}
            else if((rain && j<30) || (owner!=255 && owner>=24))return 0;
        }
        for(int j=0;j<4;j++)if(frames[i].rain_colors[j]>63)return 0;
        for(int s=0;s<2;s++)for(int y=0;y<30;y++)for(int x=0;x<33;x++)if(frames[i].palettes[s][y][x]>3 || frames[i].present[s][y][x]>1)return 0;
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
    mm4_hud_register();
    nes_mod_register_function_entry_plugin("mm4.view.begin", 0xDB3A, begin);
    nes_mod_register_function_entry_plugin("mm4.view.part", 0xDE39, part);
    nes_mod_register_function_entry_plugin("mm4.view.dma", 0xC065, dma);
    nes_mod_register_savestate_hook("mm4.view", get, set); nes_mod_register_savestate_validator("mm4.view", valid);
}
