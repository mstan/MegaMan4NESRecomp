/* Retain the original DF00 meter glyphs, separately owned by each player. */
#include "mm4_hud.h"
#include "mm4_coop.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_savestate.h"
#include "mod_function_hooks.h"
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum { METER_PIXELS=8*56 };
typedef struct {
    char magic[4];uint8_t valid,boss,character[2];
    uint8_t meters[2][2][METER_PIXELS],boss_meter[METER_PIXELS];
    uint8_t palette[2][8],boss_palette[4];
} Hud;
static Hud B,F;
static int meter(uint8_t *out,int which) {
    CycModRegs r;cyc_mod_regs(&r);P(0x97,4);r.x=(uint8_t)which;
    if(!cyc_mod_call(0xDF00,&r) || R(0x97)!=32)return 0;
    for(int i=0;i<7;i++) {
        int at=0x204+i*4,tile=R(at+1),attr=R(at+2),top=R(at)+1;
        if(top<25 || top>73)return 0;
        for(int y=0;y<8;y++) {
            int row=(attr&128)?7-y:y,a=cyc_render_line_sprite_table(top)+tile*16+row;
            int low=cyc_render_chr((uint16_t)a),high=cyc_render_chr((uint16_t)(a+8));
            for(int x=0;x<8;x++) {
                int bit=(attr&64)?x:7-x,v=((low>>bit)&1)|(((high>>bit)&1)<<1);
                out[(top-25+y)*8+x]=(uint8_t)(v?((attr&3)*4+v):0);
            }
        }
    }
    return 1;
}
void mm4_hud_reset(void) {memset(&B,0,sizeof B);memset(&F,0,sizeof F);}
void mm4_hud_begin(void) {memset(&B,0,sizeof B);}
int mm4_hud_capture(void) {
    memset(&B,0,sizeof B);
    if(!mm4_coop_enabled() || cyc_mod_isolated() || R(0x22)>=16 || !(R(0x130)&128))return 1;
    Hud next={0};int actor=R(0x146),boss=(R(0x132)&128) && actor>=5 && actor<24 && R(0x450+actor);
    for(int seat=0;seat<2;seat++) {
        uint8_t inventory[14];for(int i=0;i<14;i++)inventory[i]=(uint8_t)mm4_coop_read(seat,(uint16_t)(0xB0+i));
        for(int i=0;i<8;i++)next.palette[seat][i]=(uint8_t)(mm4_coop_read(seat,(uint16_t)(0x610+i))&63);
        int weapon=mm4_coop_read(seat,0xA0);next.character[seat]=(uint8_t)mm4_coop_character_at(seat);
        if(!mm4_coop_live(seat))inventory[0]=0;
        if(!cyc_mod_isolate_begin())return 0;
        for(int i=0;i<14;i++)P(0xB0+i,inventory[i]);
        P(0x130,128);P(0x131,weapon?128|weapon:0);
        int ok=meter(next.meters[seat][0],0);
        if(ok && weapon)ok=meter(next.meters[seat][1],1);
        if(ok && !seat && boss) {
            P(0x132,0x8F);ok=meter(next.boss_meter,2);next.boss=1;
            for(int i=0;i<4;i++)next.boss_palette[i]=R(0x618+i)&63;
        }
        cyc_mod_isolate_end();if(!ok)return 0;
    }
    next.valid=1;B=next;return 1;
}
void mm4_hud_latch(void) {F=B;}
int mm4_hud_native_meter(const uint8_t *o) {
    return mm4_coop_enabled() && o && o[0]>=24 && o[0]<=72 && !(o[0]&7) && o[1]>=0x80 && o[1]<=0x84 &&
        ((o[3]==24 && o[2]==1) || (o[3]==16 && o[2]==0) || (o[3]==40 && o[2]==2));
}
static void draw(uint32_t *out,int width,int height,int left,int top,const uint8_t *pixels,const uint8_t *palette,int tint) {
    for(int y=0;y<56;y++)for(int x=0;x<8;x++) {
        int v=pixels[y*8+x],px=left+x,py=top+y;if(!v || px<0 || px>=width || py<0 || py>=height)continue;
        uint32_t c=cyc_render_color_value(palette[v]);
        if(tint && (c&0xFFFFFF)) {unsigned r=c>>16&255,g=c>>8&255,b=c&255;c=0xFF000000u|((r*3+255)/4<<16)|((g*3+64)/4<<8)|(b*3+64)/4;}
        out[py*width+px]=c;
    }
}
void mm4_hud_draw(uint32_t *out,int width,int height,int own) {
    if(!mm4_coop_enabled() || !F.valid)return;
    for(int seat=0;seat<2;seat++) {
        if(own>=0 && own!=seat)continue;int top=20+(own<0?seat*76:0);
        draw(out,width,height,8,top,F.meters[seat][0],F.palette[seat],F.character[seat]);
        draw(out,width,height,24,top,F.meters[seat][1],F.palette[seat],0);
    }
    if(F.boss){uint8_t palette[16]={0};for(int i=0;i<4;i++)palette[8+i]=F.boss_palette[i];draw(out,width,height,width-16,20,F.boss_meter,palette,0);}
}
static int get(uint8_t *out,int cap) {if(cap<sizeof B+sizeof F)return -1;memcpy(B.magic,"M4H1",4);memcpy(F.magic,"M4H1",4);memcpy(out,&B,sizeof B);memcpy(out+sizeof B,&F,sizeof F);return sizeof B+sizeof F;}
static int valid(const uint8_t *in,int len) {
    if(!len)return !mm4_coop_enabled();if(!in || len!=sizeof B+sizeof F)return 0;Hud h[2];memcpy(h,in,sizeof h);
    for(int k=0;k<2;k++) {
        if(memcmp(h[k].magic,"M4H1",4) || h[k].valid>1 || h[k].boss>1)return 0;
        for(int s=0;s<2;s++){if(h[k].character[s]>1)return 0;for(int c=0;c<8;c++)if(h[k].palette[s][c]>63)return 0;
            for(int m=0;m<2;m++)for(int p=0;p<METER_PIXELS;p++)if(h[k].meters[s][m][p]>7)return 0;}
        for(int c=0;c<4;c++)if(h[k].boss_palette[c]>63)return 0;for(int p=0;p<METER_PIXELS;p++)if(h[k].boss_meter[p]>11)return 0;
    }
    return 1;
}
static int set(const uint8_t *in,int len){if(!valid(in,len))return 0;if(len){memcpy(&B,in,sizeof B);memcpy(&F,in+sizeof B,sizeof F);}else mm4_hud_reset();return 1;}
static int native_meters(uint16_t address) {
    (void)address;if(cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);
    if(!r.x)mm4_hud_capture();
    return 0;
}
void mm4_hud_register(void){nes_mod_register_function_entry_plugin("mm4.hud.native",0xDF00,native_meters);nes_mod_register_savestate_hook("mm4.hud",get,set);nes_mod_register_savestate_validator("mm4.hud",valid);}
