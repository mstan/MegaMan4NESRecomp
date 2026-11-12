/* Original inventory packets, meters and sprites, with personal page data. */
#include "mm4_menu.h"
#include "mm4_coop.h"
#include "mm4_world.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_savestate.h"
#include "mod_function_hooks.h"
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
typedef struct {
    uint8_t valid,cursor,character,sprite16,key[60],tiles[1024],oam[108],chr[8192],palette[32],body_colors[8];
    uint16_t bg_table,sprite_table;
} MenuPage;
typedef struct {char magic[4];uint8_t base_valid,base[1024];MenuPage build[2],page[2];} MenuState;
static MenuState S;
static MenuPage *s_capture;
static int s_capture_ok;
static uint32_t s_panel[2][256*240];
/* C330's finite $0780 packet flush. Capture writes to the page instead of
 * unclocked $2006/$2007 latches; the live flush and NMI remain original. */
static int flush(uint16_t address) {
    (void)address;if(!s_capture || !cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);int offset=0,had=0;P(0x19,0);
    for(;;) {
        int hi=R(0x780+offset);
        if(hi&128){r.a=(uint8_t)hi;r.x=(uint8_t)offset;if(had)r.y=255;r.p=(uint8_t)((r.p&~130)|(hi&128));cyc_mod_set_regs(&r);return 1;}
        int at=(hi<<8)|R(0x781+offset),count=R(0x782+offset)+1;
        if(at<0x2000 || at>=0x3000 || count>128 || offset+count+3>256){s_capture_ok=0;return 1;}
        for(int i=0;i<count;i++)s_capture->tiles[(at+i)&1023]=R(0x783+offset+i);
        had=1;offset=(offset+count+3)&255;
        if(!offset){r.x=0;r.y=255;cyc_mod_set_regs(&r);return 1;}
    }
}
void mm4_menu_reset(void){memset(&S,0,sizeof S);}
int mm4_menu_capture(void) {
    if(!mm4_coop_independent() || !mm4_coop_menu_ready() || R(0x22)!=16)return 1;
    for(int seat=0;seat<2;seat++) {
        MenuPage next={0},*old=&S.build[seat];int n=0;
        for(int i=0;i<14;i++)next.key[n++]=(uint8_t)mm4_coop_read(seat,0xB0+i);
        next.cursor=(uint8_t)mm4_coop_read(seat,0x138);next.key[n++]=next.cursor;
        next.key[n++]=R(0xA1);next.key[n++]=R(0xA2);next.key[n++]=(mm4_coop_menu_phase()>>3)&1;
        for(int i=0;i<32;i++)next.key[n++]=R(0x620+i);
        for(int i=0;i<8;i++)next.key[n++]=(uint8_t)mm4_coop_read(seat,0x610+i);
        next.key[n++]=R(0x2B)&32;next.key[n++]=R(0xFF);
        if(old->valid && !memcmp(next.key,old->key,sizeof next.key))continue;
        if(!cyc_mod_isolate_begin())return 0;
        s_capture=&next;s_capture_ok=1;nes_mod_set_function_hook_enabled("mm4.menu.flush",1);cyc_mod_allow_isolated_hooks(true);
        CycModRegs r;cyc_mod_regs(&r);P(0x19,0);P(0x1A,0);P(0xF5,0x3C);P(0xF6,0x3D);int ok=cyc_mod_call(0xFF37,&r);
        if(ok && !S.base_valid) {
            P(0x22,16);P(0x1C,3);
            for(int row=0;row<64 && ok;row++){P(0x1D,0xC0|row);ok=cyc_mod_call(0xE262,&r);if(ok)ok=cyc_mod_call(0xC330,&r);}
            if(ok && s_capture_ok){memcpy(S.base,next.tiles,sizeof S.base);S.base_valid=1;}
        }
        memcpy(next.tiles,S.base,sizeof next.tiles);
        P(0xF5,0x3C);P(0xF6,0x3D);if(ok)ok=cyc_mod_call(0xFF37,&r);
        for(int i=0;i<14;i++)P(0xB0+i,next.key[i]);P(0xBE,0x9C);
        for(int i=0;i<256;i+=4)P(0x200+i,248);
        for(int i=0;i<44;i++)P(0x200+i,mm4_cart_prg(0x3C,0x9E51+i));
        for(int meter=14;meter>=0 && ok;meter--) {
            r.y=(uint8_t)meter;ok=cyc_mod_call((R(0xB0+meter)&128)?0x9885:0x9A86,&r);
            if(ok)ok=cyc_mod_call(0xC330,&r);
        }
        if(ok)ok=cyc_mod_call(0x98D0,&r);if(ok)ok=cyc_mod_call(0xC330,&r);
        P(0x138,next.cursor);P(0x139,next.cursor);P(0x95,mm4_coop_menu_phase());
        if(ok)ok=cyc_mod_call(0x9992,&r);if(ok)ok=cyc_mod_call(0xC330,&r);
        if(ok) {
            for(int i=0;i<108;i++)next.oam[i]=R(0x200+i);
            for(int i=0;i<8192;i++)next.chr[i]=cyc_render_chr((uint16_t)i);
            for(int i=0;i<32;i++)next.palette[i]=R(0x620+i)&63;
            for(int i=0;i<8;i++)next.body_colors[i]=next.key[50+i]&63;
            next.bg_table=cyc_render_line_bg_table(120);next.sprite_table=cyc_render_line_sprite_table(120);
            next.sprite16=(uint8_t)cyc_render_line_sprite16(120);
            next.character=(uint8_t)mm4_coop_character_at(seat);next.valid=1;
        }
        nes_mod_set_function_hook_enabled("mm4.menu.flush",0);cyc_mod_allow_isolated_hooks(false);s_capture=NULL;
        cyc_mod_isolate_end();if(!ok || !s_capture_ok)return 0;*old=next;
        if(!S.page[seat].valid)S.page[seat]=next;
    }
    return 1;
}
/* Like the ordinary actor compositor, present the page that belongs to the
 * native DMA, rather than the next idle loop's already-prepared animation. */
void mm4_menu_latch(void){if(mm4_coop_menu_ready())memcpy(S.page,S.build,sizeof S.page);}
static void scene(uint32_t *out,int seat) {
    const MenuPage *p=&S.page[seat];
    for(int y=0;y<240;y++)for(int x=0;x<256;x++) {
        int tile=p->tiles[y/8*32+x/8],attr=p->tiles[0x3C0+y/32*8+x/32],pal=(attr>>(((y&16)?4:0)+((x&16)?2:0)))&3;
        int a=p->bg_table+tile*16+(y&7),bit=7-(x&7),v=((p->chr[a]>>bit)&1)|(((p->chr[a+8]>>bit)&1)<<1);
        int mask=cyc_render_line_mask(y);if(!(mask&8) || (x<8 && !(mask&2)))v=0;
        out[y*256+x]=cyc_render_color_value(p->palette[v?pal*4+v:0]);
    }
    for(int slot=26;slot>=0;slot--) {
        const uint8_t *o=p->oam+slot*4;int top=o[0]+1,height=p->sprite16?16:8;
        for(int y=0;y<height && top+y<240;y++)for(int x=0;x<8 && o[3]+x<256;x++) {
            int row=(o[2]&128)?height-1-y:y,bit=(o[2]&64)?x:7-x;
            int a=p->sprite16?(o[1]&1)*4096+((o[1]&254)+row/8)*16+(row&7):p->sprite_table+o[1]*16+row;
            int v=((p->chr[a]>>bit)&1)|(((p->chr[a+8]>>bit)&1)<<1),mask=cyc_render_line_mask(top+y);
            if(v && (mask&16) && (o[3]+x>=8 || (mask&4)))out[(top+y)*256+o[3]+x]=cyc_render_color_value(p->palette[16+(o[2]&3)*4+v]);
        }
    }
    mm4_view_menu_character(out,256,0,p->cursor,p->character,p->body_colors);
}
int mm4_menu_draw(uint32_t *out,int width,int height) {
    int mode=mm4_coop_view_mode();if(!mode || !mm4_coop_menu_ready() || height!=240 || width<256 || !S.page[0].valid || !S.page[1].valid)return 0;
    if(mode==1) {
        scene(s_panel[0],0);scene(s_panel[1],1);int half=width/2;
        for(int y=0;y<240;y++)for(int x=0;x<width;x++){int seat=x>=half,local=x-(seat?half:0),span=seat?width-half:half;out[y*width+x]=s_panel[seat][y*256+local*256/span];}
    } else {int seat=mode==3;scene(s_panel[seat],seat);for(int y=0;y<240;y++)for(int x=0;x<width;x++)out[y*width+x]=s_panel[seat][y*256+x*256/width];}
    cyc_render_set_native_origin(0);return 1;
}
static int get(uint8_t *out,int cap){if(cap<sizeof S)return -1;memcpy(S.magic,"M4M2",4);memcpy(out,&S,sizeof S);return sizeof S;}
static int valid(const uint8_t *in,int len){
    if(!len)return !mm4_coop_independent();if(!in || len!=sizeof S || memcmp(in,"M4M2",4))return 0;
    MenuState p;memcpy(&p,in,sizeof p);if(p.base_valid>1)return 0;
    for(int s=0;s<4;s++){const MenuPage *q=s<2?&p.build[s]:&p.page[s-2];if(q->valid>1 || q->cursor>15 || q->character>1 || q->sprite16>1 || (q->bg_table!=0 && q->bg_table!=4096) || (q->sprite_table!=0 && q->sprite_table!=4096))return 0;for(int i=0;i<32;i++)if(q->palette[i]>63)return 0;for(int i=0;i<8;i++)if(q->body_colors[i]>63)return 0;}return 1;
}
static int set(const uint8_t *in,int len){if(!valid(in,len))return 0;if(len)memcpy(&S,in,sizeof S);else mm4_menu_reset();return 1;}
void mm4_menu_register(void){nes_mod_register_function_entry_plugin("mm4.menu.flush",0xC330,flush);nes_mod_register_savestate_hook("mm4.menu",get,set);nes_mod_register_savestate_validator("mm4.menu",valid);}
