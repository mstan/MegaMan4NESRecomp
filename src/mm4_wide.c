/* Widen the native authored world, retaining its actors and spawn identities. */
#include "mm4_wide.h"
#include "mm4_world.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_video.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum { MAX_PARTS = 512 };
typedef struct { int16_t x,y; uint8_t owner,attr,pixels[64]; } WidePart;
typedef struct {
    uint8_t valid,rank[24],next_rank,reserved;
    uint16_t count,width,camera,left,room_left,room_right;
    WidePart parts[MAX_PARTS];
} WideFrame;
typedef struct {
    char magic[4]; uint8_t enabled,reserved[3];
    uint64_t spawns,rendered,full,failed;
    WideFrame build,frame;
} WideState;
static WideState S;
static int s_on;

static int geometry(int width, int *camera, int *left, int *lo, int *hi) {
    if (!s_on || width <= 256 || width > CYC_VIDEO_MAX_WIDTH || R(0x22) >= 16 || R(0x19) || R(0x1A) ||
        (R(0x2B)&0xC0) || R(0x30)==10 || R(0x300)!=1) return 0;
    int page = R(0xF9), start = page - R(0x2E);
    if (start < 0 || R(0x2D) > 15) return 0;
    *camera = page * 256 + R(0xFC); *lo = start * 256; *hi = (start + R(0x2D) + 1) * 256;
    *left = *camera - (width - 256) / 2;
    if (*left < *lo) *left = *lo;
    if (*hi - *lo >= width && *left + width > *hi) *left = *hi - width;
    return 1;
}
void mm4_wide_begin(void) {
    memset(&S.build,0,sizeof S.build); memset(S.build.rank,255,sizeof S.build.rank); S.build.next_rank = 1;
    int camera,left,lo,hi,width = cyc_video_width();
    if (!geometry(width,&camera,&left,&lo,&hi)) return;
    S.build.valid=1; S.build.width=(uint16_t)width; S.build.camera=(uint16_t)camera;
    S.build.left=(uint16_t)left; S.build.room_left=(uint16_t)lo; S.build.room_right=(uint16_t)hi;
}
void mm4_wide_latch(void) { S.frame=S.build; }
static int actor(uint16_t address) {
    (void)address;
    if (!S.build.valid || cyc_mod_isolated()) return 0;
    CycModRegs regs; cyc_mod_regs(&regs); int slot=regs.x;
    if (slot>=24) return 0;
    S.build.rank[slot]=S.build.next_rank++;
    int wx=(R(0x348+slot)<<8)|R(0x330+slot), dx=wx-S.build.camera;
    if (!slot || !R(0x300+slot) || (dx>=32 && dx<224) || wx+32<S.build.left || wx-32>=S.build.left+S.build.width) return 0;
    if (!cyc_mod_isolate_begin()) { S.failed++; return 0; }
    int saved_count=S.build.count;
    int camera=wx-128; P(0xFC,camera); P(0xF9,camera>>8); P(0x96,slot); P(0x97,4);
    for (int i=0;i<256;i+=4) P(0x200+i,248);
    /* MM4 uses CHR-RAM. Capture its current mapped pixels before restoring
     * the isolated renderer's mapper and OAM scratch. */
    regs.x=(uint8_t)slot;
    int ok=cyc_mod_call(0xDB72,&regs), count=R(0x97), n=0;
    uint8_t fields[30];
    for (int base=0x300;base<=0x5B8;base+=0x18) fields[n++]=R(base+slot);
    if (!count) count=256;
    for (int offset=4;ok && offset<count;offset+=4) {
        int top=R(0x200+offset)+1;
        if (top>=240) continue;
        if (S.build.count==MAX_PARTS) { S.full++; break; }
        WidePart *p=&S.build.parts[S.build.count++];
        p->x=(int16_t)(wx+R(0x203+offset)-128); p->y=(int16_t)top;
        p->owner=(uint8_t)slot; p->attr=R(0x202+offset);
        int tile=R(0x201+offset), table=cyc_render_line_sprite_table(top);
        for (int y=0;y<8;y++) {
            int row=(p->attr&128)?7-y:y, a=table+tile*16+row;
            int low=cyc_render_chr(a),high=cyc_render_chr(a+8);
            for (int x=0;x<8;x++) { int bit=(p->attr&64)?x:7-x; p->pixels[y*8+x]=(uint8_t)(((low>>bit)&1)|(((high>>bit)&1)<<1)); }
        }
    }
    cyc_mod_isolate_end();
    if (!ok) { S.build.count=(uint16_t)saved_count; S.failed++; return 0; }
    n=0; for (int base=0x300;base<=0x5B8;base+=0x18) P(base+slot,fields[n++]);
    S.rendered++; return 1;
}
static int spawn(uint16_t address) {
    (void)address;
    if (cyc_mod_isolated()) return 0;
    int camera,left,lo,hi;
    if (!geometry(cyc_video_width(),&camera,&left,&lo,&hi) || !mm4_cart_ready()) return 0;
    int stage=R(0x22),bank=stage|0x20;
    for (int index=0;index<128;index++) {
        int screen=mm4_cart_prg(bank,0xB100+index);
        if (screen>=0x80) break; /* native sorted-list terminator; tail is other data */
        int wx=screen*256+mm4_cart_prg(bank,0xB180+index);
        int code=mm4_cart_prg(bank,0xB280+index), y=mm4_cart_prg(bank,0xB200+index);
        if (code>=0xC0 || wx<lo || wx>=hi || wx+24<left || wx-24>=left+cyc_video_width() || y>=240) continue;
        if (wx>=camera && wx<camera+256) continue; /* native traversal owns its rectangle */
        int active=0,free_slot=0;
        for (int slot=8;slot<24;slot++) { if (!R(0x300+slot)) free_slot=1; else if (R(0x438+slot)==index) active=1; }
        if (active || (R(0x100+(index>>3))&(0x80>>(index&7)))) continue;
        if (!free_slot) { S.full++; continue; }
        if (!cyc_mod_isolate_begin()) { S.failed++; continue; }
        CycModRegs r; cyc_mod_regs(&r); P(0xF5,0x38);P(0xF6,bank);
        int ok=cyc_mod_call(0xFF37,&r);r.y=(uint8_t)index;
        if (ok) ok=cyc_mod_call(0x8080,&r);
        int found=-1,n=0;uint8_t fields[30];
        for (int slot=8;ok && slot<24;slot++) if (R(0x300+slot) && R(0x438+slot)==index) { found=slot; break; }
        if (found>=0) for (int base=0x300;base<=0x5B8;base+=0x18) fields[n++]=R(base+found);
        cyc_mod_isolate_end();
        if (!ok) S.failed++;
        else if (found>=0) { n=0;for (int base=0x300;base<=0x5B8;base+=0x18) P(base+found,fields[n++]); S.spawns++; }
    }
    return 0;
}
int mm4_wide_background(uint32_t *out,int width,int height,const uint32_t *native,uint8_t *opaque,int *origin) {
    if (!s_on || !S.frame.valid || width<=256 || width>CYC_VIDEO_MAX_WIDTH || height!=240 || !native) return 0;
    int camera=S.frame.camera,left=S.frame.left;
    if (width!=S.frame.width) { int lo,hi;if (!geometry(width,&camera,&left,&lo,&hi)) return 0; }
    *origin=camera-left;
    Mm4WorldTile tiles[CYC_VIDEO_MAX_WIDTH/8+2];
    uint8_t valid_tiles[CYC_VIDEO_MAX_WIDTH/8+2];
    int cache_x=-100000,cache_y=-1,tile_count=width/8+2;
    for (int y=0;y<240;y++) {
        int delta=(cyc_render_line_scroll_x(y)-(camera&511)+256)&511;
        int actual=camera+delta-256,sy=cyc_render_line_scroll_y(y)%240;
        int first_world=actual-*origin,first_tile=first_world>=0?first_world/8:(first_world-7)/8;
        if (cache_x!=first_tile || cache_y!=sy/8) {
            cache_x=first_tile;cache_y=sy/8;
            for (int i=0;i<tile_count;i++) {
                int wx=(first_tile+i)*8;
                valid_tiles[i]=(uint8_t)(wx>=0 && mm4_world_tile(wx/256,wx&255,sy,&tiles[i]));
            }
        }
        uint8_t low[CYC_VIDEO_MAX_WIDTH/8+2],high[CYC_VIDEO_MAX_WIDTH/8+2];
        for (int i=0;i<tile_count;i++) if (valid_tiles[i]) {
            int a=cyc_render_line_bg_table(y)+tiles[i].tile*16+(sy&7);
            low[i]=cyc_render_chr((uint16_t)a);high[i]=cyc_render_chr((uint16_t)(a+8));
        }
        for (int x=0;x<width;x++) {
            int nx=x-*origin,wx=actual+nx,value=0;
            if (nx>=0 && nx<256) { out[y*width+x]=native[y*256+nx];opaque[y*width+x]=0;continue; }
            uint32_t color=0xFF000000u;
            int cell=wx/8-first_tile;
            if (wx>=S.frame.room_left && wx<S.frame.room_right && cell>=0 && cell<tile_count && valid_tiles[cell]) {
                int bit=7-(wx&7);
                if (cyc_render_line_mask(y)&8) value=((low[cell]>>bit)&1)|(((high[cell]>>bit)&1)<<1);
                color=cyc_render_color(value?tiles[cell].palette*4+value:0);
            }
            out[y*width+x]=color;
            opaque[y*width+x]=(uint8_t)(value!=0);
        }
    }
    return 1;
}
int mm4_wide_rank(int owner) { return owner>=0 && owner<24 && S.frame.valid?S.frame.rank[owner]:255; }
void mm4_wide_draw(uint32_t *out,int width,int origin,const uint8_t *opaque,uint8_t *claimed,int rank) {
    if (!s_on || !S.frame.valid || width<=256) return;
    /* This frame's origin is explicit. cyc_render_native_origin reports the
     * previously composed frame until the current compositor returns. */
    int left=S.frame.camera-origin;
    for (int i=0;i<S.frame.count;i++) {
        const WidePart *p=&S.frame.parts[i];if (mm4_wide_rank(p->owner)!=rank) continue;
        for (int y=0;y<8;y++) for (int x=0;x<8;x++) {
            int v=p->pixels[y*8+x],px=p->x-left+x,py=p->y+y;
            if (!v || px<0 || px>=width || py<0 || py>=240) continue;
            int at=py*width+px;
            if (claimed[at]) continue;
            claimed[at]=1;
            if (!(p->attr&32) || !opaque[at]) out[at]=cyc_render_color(16+(p->attr&3)*4+v);
        }
    }
}
void mm4_wide_set(int enabled,const char *aspect) {
    s_on=enabled!=0;
    int mode=NES_VIDEO_FIT;
    if (aspect && !strcmp(aspect,"16-9")) mode=NES_VIDEO_16_9;
    else if (aspect && !strcmp(aspect,"21-9")) mode=NES_VIDEO_21_9;
    else if (aspect && !strcmp(aspect,"32-9")) mode=NES_VIDEO_32_9;
    else if (aspect && !strcmp(aspect,"stock")) mode=NES_VIDEO_STOCK;
    cyc_video_set_mode(s_on?mode:NES_VIDEO_STOCK);
    nes_mod_set_function_hook_enabled("mm4.wide.actor",s_on);nes_mod_set_function_hook_enabled("mm4.wide.spawn",s_on);
    nes_mod_set_local_only("MM4 widened world (unqualified netplay)",s_on);
    mm4_view_wide(s_on);
}
void mm4_wide_power(void) { memset(&S,0,sizeof S); }
static int get(uint8_t *out,int cap) { if (cap<sizeof S)return -1;memcpy(S.magic,"MX01",4);S.enabled=(uint8_t)s_on;memcpy(out,&S,sizeof S);return sizeof S; }
static int valid(const uint8_t *in,int len) {
    if (!len)return !s_on;
    if (!in || len!=sizeof S)return 0;
    static WideState p;memcpy(&p,in,sizeof p);
    if (memcmp(p.magic,"MX01",4) || p.enabled!=s_on)return 0;
    const WideFrame *f[2]={&p.build,&p.frame};
    for(int j=0;j<2;j++) {
        if(f[j]->valid>1 || f[j]->count>MAX_PARTS || f[j]->width>CYC_VIDEO_MAX_WIDTH)return 0;
        for(int i=0;i<f[j]->count;i++) { if(f[j]->parts[i].owner>=24)return 0;for(int k=0;k<64;k++)if(f[j]->parts[i].pixels[k]>3)return 0; }
    }
    return 1;
}
static int set(const uint8_t *in,int len) { if(!valid(in,len))return 0;if(len)memcpy(&S,in,sizeof S);else memset(&S,0,sizeof S);return 1; }
void mm4_wide_register(void) {
    nes_mod_register_function_entry_plugin("mm4.wide.actor",0xDB72,actor);nes_mod_register_function_entry_plugin("mm4.wide.spawn",0x8000,spawn);
    nes_mod_register_savestate_hook("mm4.wide",get,set);nes_mod_register_savestate_validator("mm4.wide",valid);
}
int mm4_wide_json(char *out,int cap) { return snprintf(out,cap,"\"wide\":{\"on\":%d,\"width\":%u,\"parts\":%u,\"spawns\":%llu,\"rendered\":%llu,\"full\":%llu,\"failed\":%llu}",s_on,S.frame.width,S.frame.count,(unsigned long long)S.spawns,(unsigned long long)S.rendered,(unsigned long long)S.full,(unsigned long long)S.failed); }
