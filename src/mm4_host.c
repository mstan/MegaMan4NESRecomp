/* Native MM4 controls; cartridge graphics/tables are read from the loaded ROM. */
#include "mm4_proto.h"
#include "cyc_host_extras.h"
#include "cyc_input.h"
#include "cyc_ring.h"
#include "cyc_mod.h"
#include "logical_input.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdlib.h>
#include <string.h>

typedef struct { char magic[4]; uint8_t on,previous,pending,reserved; uint64_t changes; } Controls;
static Controls S;
static int s_on;
static int s_proto_on,s_proto_mode=1,s_proto_start=1;
static uint8_t *s_cart;
static size_t s_cart_size;
static FILE *s_log;
static uint8_t ram(unsigned a) {return cyc_mod_peek((uint16_t)a);}
static void poke(unsigned a,unsigned v) {cyc_mod_poke((uint16_t)a,(uint8_t)v);}
static uint8_t prg(unsigned bank,unsigned address) {
    size_t p=(size_t)bank*0x2000+(address&0x1fff);
    return p<s_cart_size?s_cart[p]:0xff;
}
void mm4_controls_refresh(void) {nes_mod_set_function_hook_enabled("mm4.player.dispatch",s_on || mm4_proto_enabled());}
static void enabled(int on) {s_on=on!=0;S.pending=S.previous=0;mm4_controls_refresh();}

/* Native DA05's graphics packets contain bank, page count, source page and
 * destination PPU page. Decode the same user-owned bytes without executing
 * its PPU uploads during active rendering or changing mapper/PPU registers. */
static int graphics(int weapon,int commit) {
    unsigned selector=0x30u+(unsigned)weapon;
    unsigned pointer=prg(0x28,0xb800+selector)|(unsigned)prg(0x28,0xb870+selector)<<8;
    if(pointer<0xa000 || pointer>=0xc000)return 0;
    unsigned cursor=prg(0x28,pointer)<0x80?8:1;
    for(unsigned packets=0;packets<32;packets++,cursor+=4) {
        if(cursor+3>=256 || pointer+cursor+3>=0xc000)return 0;
        unsigned bank=prg(0x28,pointer+cursor);
        if(bank>=0x80) {
            if(commit && prg(0x28,pointer)<0x80)for(unsigned i=0;i<8;i++) {
                unsigned c=prg(0x28,pointer+i);poke(0x618+i,c);poke(0x638+i,c);
            }
            return 1;
        }
        unsigned pages=prg(0x28,pointer+cursor+1),from=prg(0x28,pointer+cursor+2),to=prg(0x28,pointer+cursor+3);
        size_t source=(size_t)bank*0x2000+(from-0x80u)*0x100u;
        size_t destination=(size_t)to*0x100u,count=(size_t)pages*256;
        if(!pages || from<0x80 || from>=0xc0 || source+count>s_cart_size || destination+count>0x2000)return 0;
        if(commit)for(size_t i=0;i<count;i++)if(!cyc_mod_chr_poke((uint16_t)(destination+i),s_cart[source+i]))return 0;
    }
    return 0;
}
static int safe(void) {
    unsigned state=ram(0x30);
    return ram(0x22)<0x10 && ram(0x300)==1 && (ram(0xb0)&31) &&
        (state==0 || state==1 || state==3 || state==4) &&
        !(ram(0x19)|ram(0x1a)|ram(0x13a)|ram(0x13c)|ram(0x13d));
}
static int dispatch(uint16_t address) {
    (void)address;
    if(mm4_proto_dispatch()) {S.pending=0;return 1;}
    unsigned fresh=S.pending;S.pending=0;
    if(!s_on || !s_cart || !safe() || (fresh!=CYC_GAME_L && fresh!=CYC_GAME_R))return 0;
    int current=ram(0xa0),next=current,step=fresh==CYC_GAME_L?-1:1;
    if(current>13)return 0;
    do {next=(next+step+14)%14;}while(next!=current && next && !(ram(0xb0+next)&0x80));
    if(next==current || !graphics(next,0))return 0;
    if(!graphics(next,1))return 0;
    poke(0xa0,next);poke(0x131,next?(0x80|next):0);
    poke(0x35,0);poke(0x36,0);poke(0x4c8,0);
    /* Same personal object cleanup as native menu exit DC00 (slots 1..4). */
    for(int slot=1;slot<=4;slot++) {
        poke(0x5a0+slot,0);poke(0x300+slot,0);poke(0x5b8+slot,0);poke(0x450+slot,0);poke(0x438+slot,0xff);
    }
    for(int i=0;i<3;i++) {unsigned c=prg(0x3c,0x9b04+next*4+i);poke(0x611+i,c);poke(0x631+i,c);}
    poke(0x18,0xff);
    S.changes++;
    return 0;
}
static void input(void *ctx,uint8_t buttons[2]) {
    (void)ctx;(void)buttons;
    unsigned held=nes_input_game_buttons(1)&(CYC_GAME_L|CYC_GAME_R);
    S.pending=(uint8_t)(s_on && held!=(CYC_GAME_L|CYC_GAME_R)?held&~S.previous:0);
    S.previous=(uint8_t)held;
}
static void power(void *ctx) {
    (void)ctx;memset(&S,0,sizeof S);mm4_proto_power();
    free(s_cart);s_cart=NULL;s_cart_size=cyc_mod_copy_cart_payload(NULL,0);
    if(s_cart_size==0x80000) {
        s_cart=(uint8_t*)malloc(s_cart_size);
        if(s_cart && cyc_mod_copy_cart_payload(s_cart,s_cart_size)!=s_cart_size){free(s_cart);s_cart=NULL;}
    }
}
static void frame_end(void *ctx) {
    (void)ctx;S.pending=0;mm4_proto_end();
    char fields[512];mm4_proto_json(fields,sizeof fields);
    if(s_log)fprintf(s_log,"{\"frame\":%u,\"weapon\":%u,\"held\":%u,\"changes\":%llu,\"hp\":%u,\"state\":%u,\"stage\":%u,\"charge\":%u,%s}\n",
        cyc_ring_frame,ram(0xa0),S.previous,(unsigned long long)S.changes,ram(0xb0),ram(0x30),ram(0x22),ram(0x35),fields);
}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;
    if(!strcmp(name,"--mm4-controls-log")){s_log=fopen(value,"w");return s_log!=NULL;}
    if(!strcmp(name,"--protoman")) {
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        s_proto_on=!strcmp(value,"on");return mm4_proto_set(s_proto_on,s_proto_mode,s_proto_start);
    }
    if(!strcmp(name,"--proto-mode") || !strcmp(name,"--proto-start")) {
        if(strcmp(value,"protoman") && strcmp(value,"megaman"))return false;
        if(!strcmp(name,"--proto-mode"))s_proto_mode=!strcmp(value,"protoman");else s_proto_start=!strcmp(value,"protoman");
        return mm4_proto_set(s_proto_on,s_proto_mode,s_proto_start);
    }
    if(strcmp(name,"--weapon-cycle") || (strcmp(value,"on") && strcmp(value,"off")))return false;
    enabled(!strcmp(value,"on"));return true;
}
static int get(uint8_t *out,int cap) {if(cap<(int)sizeof S)return -1;memcpy(S.magic,"M4WC",4);S.on=(uint8_t)s_on;memcpy(out,&S,sizeof S);return sizeof S;}
static int valid(const uint8_t *in,int len) {
    if(!len)return !s_on;
    if(!in || len!=sizeof S)return 0;
    Controls next;memcpy(&next,in,sizeof next);
    return !memcmp(next.magic,"M4WC",4) && next.on==s_on && next.previous<=3 && next.pending<=3;
}
static int set(const uint8_t *in,int len) {if(!valid(in,len))return 0;if(len)memcpy(&S,in,sizeof S);else memset(&S,0,sizeof S);return 1;}
static void reset(void) {enabled(0);s_proto_on=0;mm4_proto_set(0,s_proto_mode,s_proto_start);}
static void activate(void) {enabled(1);}
static void activate_proto(void) {
    char mode[16]="protoman",start[16]="protoman";
    nes_mod_option_value("mega-man-4-usa.character.protoman","protoman","mode",mode,sizeof mode);
    nes_mod_option_value("mega-man-4-usa.character.protoman","protoman","start",start,sizeof start);
    s_proto_mode=!strcmp(mode,"protoman");s_proto_start=!strcmp(start,"protoman");
    s_proto_on=mm4_proto_set(1,s_proto_mode,s_proto_start);
}
NES_MOD_CONSTRUCTOR(register_mm4_controls) {
    mm4_proto_register();
    nes_mod_register_function_entry_plugin("mm4.player.dispatch",0x80da,dispatch);
    nes_mod_register_savestate_hook("mm4.controls",get,set);
    nes_mod_register_savestate_validator("mm4.controls",valid);
    nes_mod_register_reset_callback(reset);
    nes_mod_register_activation_plugin("mega-man-4-usa.weapons",activate);
    nes_mod_register_activation_plugin("mega-man-4-usa.protoman",activate_proto);
}
const CycHostExtras *cyc_host_extras(void) {
    static const CycInputProfile profile={.id="megaman-weapons-1",.game_label={"Previous weapon (L)","Next weapon (R)"},
        .game_pad={"leftshoulder","rightshoulder"},.shortcut_pad={"none","leftstick+rightstick",NULL,NULL,NULL,NULL,NULL}};
    static const CycHostOption options[]={{"--weapon-cycle",true,"on|off: cycle acquired weapons and helpers with L/R"},
        {"--mm4-controls-log",true,"FILE: per-frame control state for QA"},
        {"--protoman",true,"on|off: solo Proto Man and Select swapping"},
        {"--proto-mode",true,"protoman|megaman: character traits or reskin"},
        {"--proto-start",true,"protoman|megaman: solo starting character"}};
    static const CycHostExtras extras={.power_on=power,.frame_end=frame_end,.input=input,.option=option,
        .options=options,.option_count=5,.input_profile=&profile};
    return &extras;
}
