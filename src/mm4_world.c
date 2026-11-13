/* The cartridge's D17F/D295/D2CE hierarchy, with native changed-tile flags. */
#include "mm4_world.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_ring.h"
#include <stdio.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
static FILE *s_audit;
static int s_reported;

int mm4_world_tile(int screen,int x,int y,Mm4WorldTile *out) {
    int stage=R(0x22),bank=stage|0x20;
    if(!out || stage>=16 || screen<0 || screen>=48 || x<0 || x>255 || y<0 || y>255 || !mm4_cart_ready())return 0;
    int cell=(y/32)*8+x/32,quadrant=((x&16)?1:0)|((y&16)?2:0);
    int layout=mm4_cart_prg(bank,0xb500+screen);
    if(layout>=48)return 0;
    int block=mm4_cart_prg(bank,0xa900+layout*64+cell);
    int meta=mm4_cart_prg(bank,0xa500+block*4+quadrant);
    int flags=mm4_cart_prg(bank,0xa400+meta),blank=0;
    /* D1C5: native changed-tile flags and gate-palette selection. */
    if(stage==4 && (meta==9 || meta==0x8c)) {
        int parity=(R(0x2b)&0xc0)?0:(screen&1)*32;
        int index=((cell>>1)&0xfc)|((cell>>2)&1)|(quadrant&2)|parity;
        int bit=((cell*2)|(quadrant&1))&7;
        blank=(R(0x680+index)&(0x80>>bit))!=0;
    }
    if(stage==2 && (flags&0xf0)==0x70 && (R(0x670+screen)&15))flags=(flags&0xfc)|1;
    /* D24B: authored destructible records used by native screen mode4. */
    if(R(0xf0)==4 && screen>=12 && screen<17) {
        int at=mm4_cart_prg(0x3f,0xe566+screen);
        for(int n=0;n<128;n++,at+=2) {
            int target=mm4_cart_prg(0x3f,0xe577+at);
            if(target>=128)break;
            if(target==cell) {
                int bit=mm4_cart_prg(0x3f,0xe578+at)|quadrant;
                if(R(0x680+(bit>>3))&(0x80>>(bit&7)))blank=1;
                break;
            }
        }
    }
    int plane=((x&8)?1:0)|((y&8)?2:0);
    out->tile=blank?0:(uint8_t)mm4_cart_prg(bank,0xa000+plane*256+meta);
    out->palette=(uint8_t)(flags&3);out->metatile=(uint8_t)meta;
    return 1;
}
int mm4_world_class(int world_x,int y) {
    Mm4WorldTile t;
    if(world_x<0 || y<0 || y>=240 || !mm4_world_tile(world_x/256,world_x&255,y,&t))return -1;
    return mm4_cart_prg(R(0x22)|0x20,0xa400+t.metatile)&0xf0;
}
int mm4_world_audit_file(const char *path) {
    if(s_audit)fclose(s_audit);
    s_audit=fopen(path,"w");s_reported=0;
    if(s_audit)fputs("frame,stage,camera,compared,mismatched,native_edge_attribute_pixels\n",s_audit);
    return s_audit!=NULL;
}
void mm4_world_audit(const uint32_t *bg) {
    if(!s_audit || !bg || R(0x22)>=16 || R(0x19) || R(0x1a) || R(0x30)==10 ||
        R(0x300)!=1 || !(R(0xb0)&31) || (R(0x2b)&0xc0))return;
    int camera=(R(0xf9)<<8)|R(0xfc),compared=0,mismatch=0,edge_attributes=0;
    for(int y=0;y<240;y++) {
        /* A native checkpoint initializer can put an odd world page in
         * nametable0. The game's live control shadow retains that PPU origin;
         * world-page parity alone does not identify the displayed table. */
        int source=((R(0xFF)&1)<<8)|R(0xFC);
        int delta=(cyc_render_line_scroll_x(y)-source+256)&511;
        int actual=camera+delta-256,sy=cyc_render_line_scroll_y(y)%240;
        for(int x=0;x<256;x++) {
            int wx=actual+x;Mm4WorldTile t;
            if(wx<0 || !mm4_world_tile(wx/256,wx&255,sy,&t))continue;
            int value=0,mask=cyc_render_line_mask(y);
            if((mask&8) && (x>=8 || (mask&2))) {
                int a=cyc_render_line_bg_table(y)+t.tile*16+(sy&7),bit=7-(wx&7);
                value=((cyc_render_chr(a)>>bit)&1)|(((cyc_render_chr(a+8)>>bit)&1)<<1);
            }
            uint32_t color=cyc_render_color(value?t.palette*4+value:0);
            compared++;
            if(color!=bg[y*256+x]) {
                /* Native column streaming can expose a few pixels before its
                 * attribute byte arrives. Prove that cause against live CIRAM;
                 * retain the raw mismatch count and original center pixels. */
                int base=0x2000|(((wx>>8)&1)<<10);
                int nt=base+(sy/8)*32+((wx&255)/8);
                int attr=cyc_render_nametable(base+0x3c0+(sy/32)*8+((wx&255)/32));
                int pal=(attr>>(((sy&16)?4:0)+((wx&16)?2:0)))&3;
                if(x>=240 && cyc_render_nametable(nt)==t.tile && pal!=t.palette &&
                    cyc_render_color(value?pal*4+value:0)==bg[y*256+x])edge_attributes++;
                if(!s_reported && mismatch<12)fprintf(stderr,"world mismatch x=%d y=%d wx=%d sy=%d tile=%02x pal=%u decoded=%08x native=%08x\n",x,y,wx,sy,t.tile,t.palette,color,bg[y*256+x]);
                mismatch++;
            }
        }
    }
    if(mismatch)s_reported=1;
    fprintf(s_audit,"%u,%u,%d,%d,%d,%d\n",cyc_ring_frame,R(0x22),camera,compared,mismatch,edge_attributes);
}
