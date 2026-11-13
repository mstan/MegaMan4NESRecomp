/* Native MM4 controls; cartridge graphics/tables are read from the loaded ROM. */
#include "mm4_proto.h"
#include "mm4_world.h"
#include "mm4_wide.h"
#include "mm4_coop.h"
#include "mm4_net.h"
#include "cyc_core.h"
#include "cyc_host_extras.h"
#include "cyc_input.h"
#include "cyc_ring.h"
#include "cyc_tcp.h"
#include "cyc_render.h"
#include "cyc_mod.h"
#include "logical_input.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_RECOMP_UI
#include "recomp_runtime_ui.h"
#endif
#include <stdlib.h>
#include <string.h>

typedef struct { char magic[4]; uint8_t on,previous,pending,reserved; uint64_t changes; } Controls;
static Controls s_players[2];
#define S (s_players[mm4_coop_seat()])
static int s_on;
static int s_proto_on,s_proto_mode=1,s_proto_start=1;
static int s_coop,s_roster,s_preferred_view;
static unsigned s_extra_lines;
static uint8_t *s_cart;
static size_t s_cart_size;
static FILE *s_log;
static char *s_log_path;
static int s_wide;
static char s_aspect[16]="fit";
static unsigned s_qa_entry[6];
static int s_qa_entry_pending;
static uint8_t ram(unsigned a) {return cyc_mod_peek((uint16_t)a);}
static void poke(unsigned a,unsigned v) {cyc_mod_poke((uint16_t)a,(uint8_t)v);}
static int qa_entry(uint16_t address) {
    (void)address;
    if(!s_qa_entry_pending || cyc_mod_isolated())return 0;
    s_qa_entry_pending=0;
    /* One-shot test positioning before the original graphics, terrain,
     * palette, entity and READY initialization. No boss/progression flags. */
    poke(0x22,s_qa_entry[0]);poke(0xF6,s_qa_entry[0]|0x20);
    poke(0x2A,s_qa_entry[1]);poke(0xF9,s_qa_entry[1]);poke(0x348,s_qa_entry[1]);
    poke(0x45,s_qa_entry[1]);poke(0x46,s_qa_entry[2]);
    poke(0x47,s_qa_entry[3]);poke(0x48,s_qa_entry[4]);poke(0x1F,s_qa_entry[5]);
    return 0;
}
static uint8_t prg(unsigned bank,unsigned address) {
    size_t p=(size_t)bank*0x2000+(address&0x1fff);
    return p<s_cart_size?s_cart[p]:0xff;
}
unsigned mm4_cart_prg(unsigned bank,unsigned address) {return prg(bank,address);}
int mm4_cart_ready(void) {return s_cart!=NULL;}
void mm4_controls_refresh(void) {nes_mod_set_function_hook_enabled("mm4.player.dispatch",s_on || mm4_proto_enabled());}
static void enabled(int on) {s_on=on!=0;nes_mod_set_local_only("MM4 weapon switching (unqualified netplay)",s_on);for(int i=0;i<2;i++)s_players[i].pending=s_players[i].previous=0;mm4_controls_refresh();}

int mm4_host_view_preference(void) {return s_preferred_view;}
void mm4_host_view_set(int mode) {
    if(mode<0 || mode>3)return;
    s_preferred_view=mode;mm4_coop_view_set(mode);
}
static void timing(unsigned lines) {
    s_extra_lines=lines;
    if(!lines || cyc_extra_scanlines_supported())cyc_set_extra_scanlines(lines);
    if(!mm4_net_locked())nes_mod_set_local_only("MM4 CPU budget (unqualified netplay)",lines!=0);
}
static void loaded(void *ctx) {(void)ctx;timing(s_extra_lines);}
Mm4Settings mm4_host_settings(void) {
    Mm4Settings c={s_coop,mm4_coop_independent(),s_roster,mm4_proto_enabled(),s_proto_mode,s_proto_start,s_on,s_wide,mm4_no_flicker_enabled(),(int)s_extra_lines,mm4_coop_wait_both()};return c;
}
int mm4_host_apply_settings(Mm4Settings c,int local_seat) {
    s_coop=c.coop;s_roster=c.roster;s_proto_on=c.proto;s_proto_mode=c.mode;s_proto_start=c.start;s_wide=c.wide;
    if(!mm4_proto_set(c.proto,c.mode,c.start))return 0;
    enabled(c.weapons);mm4_wide_set(c.wide,"fit");mm4_no_flicker_set(c.flicker);timing((unsigned)c.lines);
    int view=s_preferred_view;mm4_coop_set(c.coop,c.roster);mm4_coop_wait_set(c.wait);
    mm4_host_view_set(c.independent?(view?view:(local_seat==1?3:2)):0);return 1;
}
static void activate_slowdown(void) {if(!mm4_net_locked())timing(262);}
static void activate_noflicker(void) {if(!mm4_net_locked())mm4_no_flicker_set(1);}

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
        if(commit && !cyc_mod_isolated())for(size_t i=0;i<count;i++)if(!cyc_mod_chr_poke((uint16_t)(destination+i),s_cart[source+i]))return 0;
    }
    return 0;
}
static int safe_for_seat(int seat) {
    if(mm4_coop_enabled() && !mm4_coop_live(seat))return 0;
    unsigned state=mm4_coop_read(seat,0x30);
    return ram(0x22)<0x10 && mm4_coop_read(seat,0x300)==1 && (mm4_coop_read(seat,0xb0)&31) &&
        (state==0 || state==1 || state==3 || state==4) &&
        !(ram(0x19)|ram(0x1a)|mm4_coop_read(seat,0x13a)|mm4_coop_read(seat,0x13c)|mm4_coop_read(seat,0x13d));
}
static int safe(void) {return safe_for_seat(mm4_coop_seat());}
int mm4_controls_art(int weapon) {return weapon>=0 && weapon<=13 && graphics(weapon,0) && graphics(weapon,1);}
static int body_pattern(unsigned address) {
    /* Native boarding streams16; dismount streams4A. These common body
     * patterns are separate from weapon33's projectile/helper packets. */
    int mounted=ram(0x30)==4 || (ram(0x30)==6 && ram(0x468)==4);
    unsigned selector=mounted?0x16:0x4A;
    unsigned pointer=prg(0x29,0xB800+selector)|(unsigned)prg(0x29,0xB860+selector)<<8;
    if(pointer<0xA000 || pointer>=0xC000)return -1;
    unsigned cursor=prg(0x29,pointer)<128?8:1;
    for(unsigned n=0;n<32;n++,cursor+=4) {
        if(pointer+cursor+3>=0xC000)break;
        unsigned bank=prg(0x29,pointer+cursor);if(bank>=128)break;
        unsigned tiles=prg(0x29,pointer+cursor+1),from=prg(0x29,pointer+cursor+2),to=prg(0x29,pointer+cursor+3);
        size_t source=(size_t)bank*8192+(from-0x80u)*256u,start=to*256u,count=tiles*16u;
        if(from>=0x80 && from<0xC0 && source+count<=s_cart_size && address>=start && address-start<count)
            return s_cart[source+address-start];
    }
    return -1;
}
int mm4_controls_pattern(int weapon,unsigned address) {
    if(weapon<0 || weapon>13 || !s_cart)return cyc_render_chr((uint16_t)address);
    {int value=body_pattern(address);if(value>=0)return value;}
    unsigned selector=0x30u+(unsigned)weapon;
    unsigned pointer=prg(0x28,0xb800+selector)|(unsigned)prg(0x28,0xb870+selector)<<8;
    if(pointer<0xa000 || pointer>=0xc000)return cyc_render_chr((uint16_t)address);
    unsigned cursor=prg(0x28,pointer)<0x80?8:1;
    for(unsigned packets=0;packets<32;packets++,cursor+=4) {
        if(cursor+3>=256 || pointer+cursor+3>=0xc000)break;
        unsigned bank=prg(0x28,pointer+cursor);if(bank>=0x80)break;
        unsigned pages=prg(0x28,pointer+cursor+1),from=prg(0x28,pointer+cursor+2),to=prg(0x28,pointer+cursor+3);
        size_t start=to*256u,count=pages*256u,source=(size_t)bank*8192+(from-0x80u)*256u;
        if(from>=0x80 && from<0xc0 && source+count<=s_cart_size && address>=start && address-start<count)
            return s_cart[source+address-start];
    }
    return cyc_render_chr((uint16_t)address);
}
int mm4_controls_equip(int next) {
    if(!s_cart || next<0 || next>13 || (next && !(ram(0xB0+next)&128)) || !graphics(next,0) || !graphics(next,1))return 0;
    int mounted=ram(0x30)==4 && ram(0xA0)==3;
    poke(0xa0,next);poke(0x131,next?(0x80|next):0);
    if(mounted) {
        /* Native menu selection clears Marine state at973A. L/R must leave
         * the same on-foot controller and common body graphics behind. */
        poke(0x30,0);
        if(!cyc_mod_isolated())for(unsigned a=0x1100;a<0x1300;a++) {
            int value=body_pattern(a);if(value<0 || !cyc_mod_chr_poke((uint16_t)a,(uint8_t)value))return 0;
        }
    }
    poke(0x35,0);poke(0x36,0);poke(0x4c8,0);
    /* Same personal object cleanup as native menu exit DC00 (slots 1..4). */
    for(int slot=1;slot<=4;slot++) {
        poke(0x5a0+slot,0);poke(0x300+slot,0);poke(0x5b8+slot,0);poke(0x450+slot,0);poke(0x438+slot,0xff);
    }
    for(int i=0;i<3;i++) {unsigned c=prg(0x3c,0x9b04+next*4+i);poke(0x611+i,c);poke(0x631+i,c);}
    poke(0x18,0xff);
    return 1;
}
int mm4_controls_dispatch(uint16_t address) {
    (void)address;
    if(mm4_proto_dispatch()) {S.pending=0;return 1;}
    unsigned fresh=S.pending;S.pending=0;
    if(!s_on || !s_cart || !safe() || (fresh!=CYC_GAME_L && fresh!=CYC_GAME_R))return 0;
    int current=ram(0xa0),next=current,step=fresh==CYC_GAME_L?-1:1;
    if(current>13)return 0;
    do {next=(next+step+14)%14;}while(next!=current && next && !(ram(0xb0+next)&0x80));
    if(next!=current && mm4_controls_equip(next))S.changes++;
    return 0;
}
static void input(void *ctx,uint8_t buttons[2]) {
    (void)ctx;
    for(int seat=0;seat<2;seat++) {
        Controls *state=&s_players[seat];
        unsigned held=seat && !mm4_coop_enabled()?0:nes_input_game_buttons(seat+1)&(CYC_GAME_L|CYC_GAME_R);
        unsigned fresh=held&~state->previous;
        state->previous=(uint8_t)held;
        /* Keep a safe edge until the native controller consumes it. An
         * output-only lag frame must not turn a held new press into no edge.
         * Unsafe frames and simultaneous L+R discard it without queuing. */
        if(!s_on || held==(CYC_GAME_L|CYC_GAME_R) || !safe_for_seat(seat))state->pending=0;
        else state->pending|=(uint8_t)fresh;
    }
    mm4_coop_input(buttons);
}
static void power(void *ctx) {
    (void)ctx;memset(s_players,0,sizeof s_players);mm4_coop_power();mm4_proto_power();mm4_wide_power();timing(s_extra_lines);
    free(s_cart);s_cart=NULL;s_cart_size=cyc_mod_copy_cart_payload(NULL,0);
    if(s_cart_size==0x80000) {
        s_cart=(uint8_t*)malloc(s_cart_size);
        if(s_cart && cyc_mod_copy_cart_payload(s_cart,s_cart_size)!=s_cart_size){free(s_cart);s_cart=NULL;}
    }
}
static void frame_end(void *ctx) {
    (void)ctx;mm4_proto_end();mm4_coop_end();
    char fields[512],wide[384],coop[1024],timing_fields[64];
    snprintf(timing_fields,sizeof timing_fields,"\"timing\":{\"extra_lines\":%u}",cyc_extra_scanlines());mm4_proto_json(fields,sizeof fields);mm4_wide_json(wide,sizeof wide);mm4_coop_json(coop,sizeof coop);
    if(s_log)fprintf(s_log,"{\"frame\":%u,\"weapon\":%u,\"held\":%u,\"changes\":%llu,\"hp\":%u,\"state\":%u,\"stage\":%u,\"charge\":%u,%s,%s,%s,%s}\n",
        cyc_ring_frame,ram(0xa0),S.previous,(unsigned long long)S.changes,ram(0xb0),ram(0x30),ram(0x22),ram(0x35),fields,wide,coop,timing_fields);
}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;mm4_net_note_option(name);
    if(mm4_net_locked() && strcmp(name,"--coop-view"))return false;
    if(!strcmp(name,"--no-flicker")) {
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        mm4_no_flicker_set(!strcmp(value,"on"));return true;
    }
    if(!strcmp(name,"--slowdown-reduction")) {
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        timing(!strcmp(value,"on")?262:0);return true;
    }
    if(!strcmp(name,"--coop-view")) {
        int mode=!strcmp(value,"shared")?0:!strcmp(value,"split")?1:!strcmp(value,"player1")?2:!strcmp(value,"player2")?3:-1;
        if(mode<0 || (mm4_net_locked() && (mode!=0)!=mm4_coop_independent()))return false;mm4_host_view_set(mode);return true;
    }
    if(!strcmp(name,"--coop-wait")) {
        if(strcmp(value,"both") && strcmp(value,"either"))return false;
        mm4_coop_wait_set(!strcmp(value,"both"));return true;
    }
    if(!strcmp(name,"--mm4-coop-poke"))return mm4_coop_test_poke(value)!=0;
    if(!strcmp(name,"--mm4-qa-entry")) {
        unsigned v[6];char tail;
        if(sscanf(value,"%u,%u,%u,%u,%u,%u%c",v,v+1,v+2,v+3,v+4,v+5,&tail)!=6 ||
            v[0]>=16 || v[1]>=48 || v[2]>=16 || v[3]>=128 || v[4]>=16 || v[5]>2)return false;
        memcpy(s_qa_entry,v,sizeof v);s_qa_entry_pending=1;
        nes_mod_set_function_hook_enabled("mm4.qa.entry",1);
        nes_mod_set_local_only("MM4 QA entry positioning",1);return true;
    }
    if(!strcmp(name,"--coop") || !strcmp(name,"--coop-roster")) {
        if(!strcmp(name,"--coop")) {
            if(strcmp(value,"on") && strcmp(value,"off"))return false;
            s_coop=!strcmp(value,"on");
        } else {
            if(strcmp(value,"megaman-protoman") && strcmp(value,"protoman-megaman"))return false;
            s_roster=!strcmp(value,"protoman-megaman");
        }
        if(!mm4_proto_set(s_proto_on || s_coop,s_proto_mode,s_proto_start))return false;
        mm4_coop_set(s_coop,s_roster);return true;
    }
    if(!strcmp(name,"--world-audit"))return mm4_world_audit_file(value)!=0;
    if(!strcmp(name,"--wide")) {
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        s_wide=!strcmp(value,"on");mm4_wide_set(s_wide,s_aspect);return true;
    }
    if(!strcmp(name,"--aspect")) {
        if(strcmp(value,"fit") && strcmp(value,"stock") && strcmp(value,"16-9") && strcmp(value,"21-9") && strcmp(value,"32-9"))return false;
        snprintf(s_aspect,sizeof s_aspect,"%s",value);mm4_wide_set(s_wide,s_aspect);return true;
    }
    if(!strcmp(name,"--mm4-controls-log")) {
        /* Session restart/catalog replay repeats the same option. Keep its
         * stream: reopening truncates the file and leaves an older buffered
         * writer able to overwrite the new round when the process exits. */
        if(s_log && s_log_path && !strcmp(s_log_path,value))return true;
        char *path=malloc(strlen(value)+1);if(!path)return false;
        FILE *next=fopen(value,"w");if(!next){free(path);return false;}
        strcpy(path,value);if(s_log)fclose(s_log);free(s_log_path);
        s_log=next;s_log_path=path;return true;
    }
    if(!strcmp(name,"--protoman")) {
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        s_proto_on=!strcmp(value,"on");return mm4_proto_set(s_proto_on || s_coop,s_proto_mode,s_proto_start);
    }
    if(!strcmp(name,"--proto-mode") || !strcmp(name,"--proto-start")) {
        if(strcmp(value,"protoman") && strcmp(value,"megaman"))return false;
        if(!strcmp(name,"--proto-mode"))s_proto_mode=!strcmp(value,"protoman");else s_proto_start=!strcmp(value,"protoman");
        return mm4_proto_set(s_proto_on || s_coop,s_proto_mode,s_proto_start);
    }
    if(strcmp(name,"--weapon-cycle") || (strcmp(value,"on") && strcmp(value,"off")))return false;
    enabled(!strcmp(value,"on"));return true;
}
static int get(uint8_t *out,int cap) {
    if(cap<(int)sizeof s_players)return -1;
    for(int i=0;i<2;i++){memcpy(s_players[i].magic,"M4W2",4);s_players[i].on=(uint8_t)s_on;}
    memcpy(out,s_players,sizeof s_players);return sizeof s_players;
}
static int valid(const uint8_t *in,int len) {
    if(!len)return !s_on;
    if(!in || len!=sizeof s_players)return 0;
    Controls next[2];memcpy(next,in,sizeof next);
    for(int i=0;i<2;i++)if(memcmp(next[i].magic,"M4W2",4) || next[i].on!=s_on || next[i].previous>3 || next[i].pending>3)return 0;
    return 1;
}
static int set(const uint8_t *in,int len) {if(!valid(in,len))return 0;if(len)memcpy(s_players,in,sizeof s_players);else memset(s_players,0,sizeof s_players);return 1;}
static void reset(void) {
    if(mm4_net_locked())return;
    /* Activation follows this reset synchronously. Retain live co-op and
     * waiting bodies when the committed selection still includes co-op. */
    if(!nes_mod_feature_selected("mega-man-4-usa.coop","coop")) {
        mm4_coop_wait_set(0);s_coop=0;mm4_coop_set(0,s_roster);
    }
    timing(0);enabled(0);s_proto_on=0;mm4_proto_set(0,s_proto_mode,s_proto_start);
    s_wide=0;mm4_wide_set(0,s_aspect);mm4_no_flicker_set(0);
}
static void activate(void) {if(!mm4_net_locked())enabled(1);}
static void activate_wide(void) {
    if(mm4_net_locked())return;
    nes_mod_option_value("mega-man-4-usa.enhancement.widescreen","widescreen","aspect",s_aspect,sizeof s_aspect);
    s_wide=1;mm4_wide_set(1,s_aspect);
}
static void activate_proto(void) {
    if(mm4_net_locked())return;
    char mode[16]="protoman",start[16]="protoman";
    nes_mod_option_value("mega-man-4-usa.character.protoman","protoman","mode",mode,sizeof mode);
    nes_mod_option_value("mega-man-4-usa.character.protoman","protoman","start",start,sizeof start);
    s_proto_mode=!strcmp(mode,"protoman");s_proto_start=!strcmp(start,"protoman");
    s_proto_on=mm4_proto_set(1,s_proto_mode,s_proto_start);
}
static void activate_coop(void) {
    if(mm4_net_locked())return;
    const char *package="mega-man-4-usa.coop";char roster[16]="megaman",view[16]="shared";
    nes_mod_option_value(package,"coop","player-one",roster,sizeof roster);
    nes_mod_option_value(package,"coop","coop-view",view,sizeof view);
    activate_proto();s_coop=1;s_roster=!strcmp(roster,"protoman");mm4_coop_set(1,s_roster);
    char wait[16]="either";nes_mod_option_value(package,"coop","wait-for-both",wait,sizeof wait);mm4_coop_wait_set(!strcmp(wait,"both"));
    mm4_host_view_set(!strcmp(view,"split")?1:!strcmp(view,"p1")?2:!strcmp(view,"p2")?3:0);
}
static void tcp_state(int id,const char *line) {
    char view[16];
    if(cyc_tcp_str(line,"view",view,sizeof view)) {
        int mode=!strcmp(view,"shared")?0:!strcmp(view,"split")?1:!strcmp(view,"player1")?2:!strcmp(view,"player2")?3:-1;
        if(mode<0 || !mm4_coop_enabled() || (mm4_net_locked() && (mode!=0)!=mm4_coop_independent())){cyc_tcp_err(id,"view needs active co-op and shared|split|player1|player2");return;}
        mm4_host_view_set(mode);
    }
    char fields[1024];int n=snprintf(fields,sizeof fields,"\"view\":%d,",mm4_coop_view_mode());
    mm4_coop_json(fields+n,sizeof fields-n);cyc_tcp_ok(id,fields);
}
static void tcp(void *ctx) {(void)ctx;cyc_tcp_register("mm4_state","MM4 co-op state and local view selection",tcp_state);}
#ifdef CYC_WITH_RECOMP_UI
static int menu_get(void *ctx,const RecompRuntimeUiItem *item,int *out) {
    (void)ctx;if(strcmp(item->key,"mm4.coop_view"))return 0;
    *out=mm4_coop_view_mode();return 1;
}
static int menu_set(void *ctx,const RecompRuntimeUiItem *item,int value) {
    (void)ctx;if(strcmp(item->key,"mm4.coop_view") || !mm4_coop_enabled() || value<0 || value>3 || (mm4_net_locked() && (value!=0)!=mm4_coop_independent()))return 0;
    mm4_host_view_set(value);return mm4_coop_view_mode()==value;
}
static int menu_enabled(void *ctx,const RecompRuntimeUiItem *item) {
    (void)ctx;return !strcmp(item->key,"mm4.coop_view") && mm4_coop_enabled();
}
#ifdef CYC_HOST_EXTRAS_HAS_NETPLAY_VALUE_POLICY
static bool menu_netplay_value_allowed(void *ctx,const RecompRuntimeUiItem *item,int value) {
    (void)ctx;return !strcmp(item->key,"mm4.coop_view") && mm4_net_locked() && mm4_coop_independent() && value>=1 && value<=3;
}
#endif
static const char *const view_choices[]={"Shared screen","Independent split screen","Player one","Player two"};
static const RecompRuntimeUiItem menu_items[]={
    {.key="mm4.coop_view",.section="Display",.label="Two-player view",
     .description="Choose a shared screen, two independent panes, or one player's view.",
     .type=RECOMP_RUNTIME_UI_CHOICE,.minimum=0,.maximum=3,.step=1,.choices=view_choices,.choice_count=4}
};
static const RecompRuntimeUiCallbacks menu_callbacks={.get_value=menu_get,.set_value=menu_set,.is_enabled=menu_enabled};
#endif
NES_MOD_CONSTRUCTOR(register_mm4_controls) {
    mm4_proto_register();
    mm4_wide_register();
    mm4_coop_register();
    nes_mod_register_function_entry_plugin("mm4.player.dispatch",0x80da,mm4_controls_dispatch);
    nes_mod_register_function_entry_plugin("mm4.qa.entry",0xC592,qa_entry);
    nes_mod_register_savestate_hook("mm4.controls",get,set);
    nes_mod_register_savestate_validator("mm4.controls",valid);
    nes_mod_register_reset_callback(reset);
    nes_mod_register_activation_plugin("mega-man-4-usa.weapons",activate);
    nes_mod_register_activation_plugin("mega-man-4-usa.protoman",activate_proto);
    nes_mod_register_activation_plugin("mega-man-4-usa.widescreen",activate_wide);
    nes_mod_register_activation_plugin("mega-man-4-usa.coop",activate_coop);
    nes_mod_register_activation_plugin("mega-man-4-usa.slowdown",activate_slowdown);
    nes_mod_register_activation_plugin("mega-man-4-usa.noflicker",activate_noflicker);
}
const CycHostExtras *cyc_host_extras(void) {
    static const CycInputProfile profile={.id="megaman-weapons-1",.game_label={"Previous weapon (L)","Next weapon (R)"},
        .game_pad={"leftshoulder","rightshoulder"},.shortcut_pad={"none","leftstick+rightstick",NULL,NULL,NULL,NULL,NULL}};
    static const CycHostOption options[]={{"--no-flicker",true,"on|off: complete native actors without sprite-limit flicker"},
        {"--slowdown-reduction",true,"on|off: extra native CPU budget at the normal video/audio rate"},
        {"--weapon-cycle",true,"on|off: cycle acquired weapons and helpers with L/R"},
        {"--coop-view",true,"shared|split|player1|player2: local co-op presentation"},
        {"--coop-wait",true,"either|both: native exit admission policy"},
        {"--mm4-coop-poke",true,"FRAME:SEAT:HEX_ADDRESS:HEX_VALUE: personal QA prerequisite"},
        {"--mm4-qa-entry",true,"stage,page,room,art,palette,checkpoint: one-time native QA entry"},
        {"--mm4-controls-log",true,"FILE: per-frame control state for QA"},
        {"--protoman",true,"on|off: solo Proto Man and Select swapping"},
        {"--proto-mode",true,"protoman|megaman: character traits or reskin"},
        {"--proto-start",true,"protoman|megaman: solo starting character"},
        {"--wide",true,"on|off: widen authored terrain and native actors"},
        {"--aspect",true,"fit|stock|16-9|21-9|32-9: gameplay aspect"},
        {"--world-audit",true,"FILE: compare decoded terrain to the native viewport"},
        {"--coop",true,"on|off: development native personal co-op"},
        {"--coop-roster",true,"megaman-protoman|protoman-megaman: player order"}};
    static const CycHostExtras extras={.power_on=power,.state_loaded=loaded,.frame_end=frame_end,.input=input,.option=option,.tcp_setup=tcp,
        .options=options,.option_count=sizeof options/sizeof *options,.input_profile=&profile,
#ifdef CYC_WITH_RECOMP_UI
        .menu_items=menu_items,.menu_item_count=sizeof menu_items/sizeof *menu_items,.menu_callbacks=&menu_callbacks,
#ifdef CYC_HOST_EXTRAS_HAS_NETPLAY_VALUE_POLICY
        .menu_netplay_value_allowed=menu_netplay_value_allowed,
#endif
#endif
    };
    return &extras;
}
