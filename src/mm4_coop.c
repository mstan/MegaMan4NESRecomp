/* Personal native execution contexts; the real world engine runs once. */
#include "mm4_coop.h"
#include "mm4_world.h"
#include "mm4_proto.h"
#include "mm4_wide.h"
#include "mm4_helpers.h"
#include "mm4_menu.h"

#include "cyc_mod.h"
#include "cyc_ring.h"
#include "cyc_render.h"
#include "cyc_video.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum { PERSONAL_CAP=384, PART_CAP=5*63 };
enum { TELE_NONE, TELE_OUT, TELE_HIDDEN, TELE_IN };
typedef struct { uint8_t ram[PERSONAL_CAP],previous,dead; } Player;
typedef struct { int16_t x,y;uint8_t owner,attr,colors[4],pixels[64]; } Part;
typedef struct { uint8_t valid,seat;uint16_t count,camera;Mm4ProtoBody body;Part parts[PART_CAP]; } Picture;
typedef struct {
    char magic[4];uint8_t enabled,initialized,stage,anchor,current,menu,pauser,pending_pause,roster,scene_active;
    uint8_t raw[2],raw_previous[2],fresh[2];
    Player player[2];Picture build,frame;
    uint64_t updates[2],world_updates,failures;
    uint8_t retry,retry_ammo[2][13],waited[2];
    uint8_t withdrawn,select_hold[2],select_armed[2],teleport[2],arrival_floor[2];
    uint64_t withdrawals[2],rejoins[2];
    uint16_t previous_x[2];
    uint8_t scene_stack;
    uint8_t checkpoint,revive_pending;
    uint8_t wait_mode,waiting,wait_direction[2],wait_page[2],wait_out[2],wait_flags[2];
    uint8_t menu_ready,menu_busy,menu_user,menu_pending[2],menu_phase;
    uint8_t source_pending,source_page,source_offset,source_nt,source_ram[8],source_stack[10];
    CycModRegs source_regs;
    uint64_t source_handoffs[2];
} Coop;
static Coop C;
static int s_on,s_roster,s_scope,s_personal_bhv,s_collision,s_death_wait,s_view;
/* A native controller can yield through NMI; retain its one-clock journal
 * across snapshots as well as the two personal controller calls. */
typedef struct {char magic[4];uint8_t ticked,reserved[3];int32_t restore;} Effects;
static Effects E={{0},0,{0},-1};
static uint16_t s_addr[PERSONAL_CAP],s_index[2048];
static int s_count;
static int s_wait_both;
static void finish_wait(void);
static void source_hooks(void);
static void effect_hooks(void);
typedef struct {
    char magic[4];uint8_t active,owner,seat,stack,input[4],old_x,old_page,pressed[2];
    uint64_t targets[2];
} Target;
typedef struct {char magic[4];uint8_t active,source_room,reserved[2];uint64_t starts[2];} Warp;
static Target A;
static Warp W;
typedef struct {uint32_t frame;uint16_t address;uint8_t seat,value;} TestPoke;
static TestPoke s_test_pokes[64];
static int s_test_poke_count;
typedef struct {const char *id;int enabled;} HookSetting;
static HookSetting s_hooks[128];
static int s_hook_count;

int mm4_coop_enabled(void) { return s_on; }
int mm4_coop_seat(void) { return s_on?C.current:0; }
int mm4_coop_character(void) { return s_on?(s_roster^C.current):-1; }
int mm4_coop_character_at(int seat) {return s_on && seat>=0 && seat<2?(s_roster^seat):0;}
int mm4_coop_live(int seat) {return s_on && C.initialized && seat>=0 && seat<2 && !C.player[seat].dead && !((C.withdrawn|C.waiting)&(1u<<seat));}
int mm4_coop_wait_both(void) {return s_wait_both;}
void mm4_coop_wait_set(int both) {s_wait_both=both!=0;if(!s_wait_both)finish_wait();}
int mm4_coop_view_mode(void) {return s_on?s_view:0;}
int mm4_coop_independent(void) {return s_on && s_view!=0;}
int mm4_coop_scene_locked(void) {return C.source_pending || C.scene_active || R(0x19) || R(0x1A) || R(0x30)>=8;}
int mm4_coop_source_pending(void) {return C.source_pending;}
void mm4_coop_view_set(int mode) {
    if(mode<0 || mode>3)return;
    s_view=mode;mm4_view_coop(s_on);mm4_wide_views_changed();
    cyc_video_override_width(mm4_coop_independent()?(s_view==1?512:256):0);
}
int mm4_coop_view_rects(int left[2]) {
    if(!s_on || !C.initialized || R(0x22)!=C.stage)return 0;
    int camera=(R(0xF9)<<8)|R(0xFC),start=R(0xF9)-R(0x2E);
    for(int seat=0;seat<2;seat++) {
        int who=(C.player[seat].dead || (C.withdrawn&(1u<<seat)))?C.anchor:seat;left[seat]=camera;
        if(!mm4_coop_scene_locked() && !(R(0x2B)&0xC0) && start>=0) {
            int x=(mm4_coop_read(who,0x348)<<8)|mm4_coop_read(who,0x330);
            int lo=start*256,hi=(start+R(0x2D))*256;
            left[seat]=x-128;if(left[seat]<lo)left[seat]=lo;if(left[seat]>hi)left[seat]=hi;
        }
    }
    return 2;
}
int mm4_coop_native_scope(void) { return s_on && s_scope; }
int mm4_coop_menu_active(void) { return s_on && C.menu; }
int mm4_coop_menu_ready(void) { return s_on && C.menu && C.menu_ready; }
int mm4_coop_menu_phase(void) {return C.menu_phase;}
static void addresses(void) {
    if(s_count)return;
    /* MM4 reserves5..7 for shared actors too (native collision CPX#05).
     * Only actor0 and personal weapons/helpers1..4 belong to a seat. */
    for(int base=0x300;base<=0x5B8;base+=0x18)for(int slot=0;slot<5;slot++)s_addr[s_count++]=(uint16_t)(base+slot);
    for(int a=0x30;a<=0x3F;a++)s_addr[s_count++]=(uint16_t)a;
    /* Player's retained terrain probes, weapon choice and movement lock.
     * Camera, scene, lives/tanks, RNG, bank shadows and clocks stay shared. */
    static const uint8_t zp[]={0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0xA0,0xAD};
    for(int i=0;i<sizeof zp;i++)s_addr[s_count++]=zp[i];
    for(int a=0xB0;a<=0xBD;a++)s_addr[s_count++]=(uint16_t)a;
    for(int i=0;i<8;i++){s_addr[s_count++]=(uint16_t)(0x610+i);s_addr[s_count++]=(uint16_t)(0x630+i);}
    s_addr[s_count++]=0x131;s_addr[s_count++]=0x138;
    s_addr[s_count++]=0x133;s_addr[s_count++]=0x134;
    for(int i=0;i<s_count;i++)s_index[s_addr[i]]=(uint16_t)(i+1);
}
static void capture(Player *p) { for(int i=0;i<s_count;i++)p->ram[i]=R(s_addr[i]); }
static void project(const Player *p) { for(int i=0;i<s_count;i++)P(s_addr[i],p->ram[i]); }
/* Actor AI needs the recipient's body, inventory and control state. The
 * physical native palette remains the anchor's while that AI runs. */
static int target_field(int a) {
    if(A.active==2)return (a>=0x300 && a<=0x5B8 && (a-0x300)%24==0) || (a>=0x30 && a<=0x47);
    return !(a>=0x610 && a<0x638);
}
static void target_capture(Player *p) {for(int i=0;i<s_count;i++)if(target_field(s_addr[i]))p->ram[i]=R(s_addr[i]);}
static void target_project(const Player *p) {for(int i=0;i<s_count;i++)if(target_field(s_addr[i]))P(s_addr[i],p->ram[i]);}
static int personal(const Player *p,int a) { int i=s_index[a];return i?p->ram[i-1]:R(a); }
static void set_personal(Player *p,int a,int value) { int i=s_index[a];if(i)p->ram[i-1]=(uint8_t)value; }
int mm4_coop_test_poke(const char *value) {
    unsigned frame,seat,address,byte;int n=0;addresses();
    if(!value || s_test_poke_count>=64 || sscanf(value,"%u:%u:%x:%x%n",&frame,&seat,&address,&byte,&n)!=4 || value[n] ||
        seat>1 || address>=2048 || !s_index[address] || byte>255)return 0;
    s_test_pokes[s_test_poke_count++]=(TestPoke){frame,(uint16_t)address,(uint8_t)seat,(uint8_t)byte};
    nes_mod_set_local_only("MM4 personal RAM QA fixture",1);return 1;
}
static void test_pokes(void) {
    for(int i=0;i<s_test_poke_count;i++)if(s_test_pokes[i].frame==cyc_ring_frame) {
        const TestPoke *p=&s_test_pokes[i];set_personal(&C.player[p->seat],p->address,p->value);
        if(p->seat==C.current)P(p->address,p->value);
    }
}
int mm4_coop_read(int seat,uint16_t a) {
    if(a>=2048 || seat<0 || seat>1)return 0;
    return C.initialized && (seat!=C.current || (A.active && !target_field(a)))?personal(&C.player[seat],a):R(a);
}
int mm4_coop_write(int seat,uint16_t a,uint8_t value) {
    if(!s_on || !C.initialized || seat<0 || seat>1 || a>=2048 || !s_index[a])return 0;
    C.player[seat].ram[s_index[a]-1]=value;
    if(seat==C.current && (!A.active || target_field(a)))P(a,value);
    return 1;
}
static void warp_follow(void) {
    int seat=C.anchor^1;Player *p=&C.player[seat];
    if(p->dead || (C.withdrawn&(1u<<seat)) || !(personal(p,0xB0)&31))return;
    /* Only the native body/transition fields follow. Personal HP, equipment,
     * ammo, palette and weapon graphics retain their original owners. */
    for(int base=0x300;base<=0x5B8;base+=24)set_personal(p,base,R(base));
    for(int a=0x30;a<=0x3F;a++)if(a!=0x3C)set_personal(p,a,R(a));
    int x=(R(0x348)<<8)|R(0x330),local=x-((R(0xF9)<<8)|R(0xFC));
    x+=local<48?16:-16;if(x<0)x=0;
    set_personal(p,0x330,x);set_personal(p,0x348,x>>8);
    for(int slot=1;slot<5;slot++)set_personal(p,0x300+slot,0);
    p->previous=C.raw[seat];
}
static int target_begin(uint16_t address) {
    (void)address;
    if(!s_on || !C.initialized || C.menu || A.active || W.active || R(0x30)>=8)return 0;
    CycModRegs r={0};cyc_mod_regs(&r);
    int balloon=r.x>=1 && r.x<=3 && R(0x300+r.x)==6;
    if(!balloon && (s_scope || cyc_mod_isolated() || r.x<5 || r.x>=24 || !R(0x300+r.x)))return 0;
    target_capture(&C.player[C.current]);
    int chosen=C.anchor,best=0x7fffffff,x=R(0x330+r.x)+256*R(0x348+r.x),y=R(0x378+r.x);
    if(balloon) {
        chosen=C.current;
        /* Same native F95D shape used by the Balloon's landing response.
         * Prefer its owner on contact, otherwise use the touching partner.
         * Only actor0/body fields are projected; its native helper pool,
         * energy and single motion/lifetime update retain their owner. */
        for(int i=0;i<2;i++) {
            int seat=C.current^i;Player *p=&C.player[seat];
            int height=mm4_cart_prg(0x3F,0xF9B4+13)-(personal(p,0x558)==0x10?8:0);
            if(mm4_coop_live(seat) && !personal(p,0x390) && !R(0x390+r.x) &&
               (R(0x528+r.x)&0x84)==0x80 &&
               abs(x-personal(p,0x330)-256*personal(p,0x348))<mm4_cart_prg(0x3F,0xF9F4+13) &&
               abs(y-personal(p,0x378))<=height){chosen=seat;break;}
        }
        if(chosen==C.current)return 0;
        best=0;
    }
    else for(int seat=0;seat<2;seat++)if(!C.player[seat].dead && !(C.withdrawn&(1u<<seat)) && (personal(&C.player[seat],0xB0)&31)) {
        int distance=abs(x-personal(&C.player[seat],0x330)-256*personal(&C.player[seat],0x348))+abs(y-personal(&C.player[seat],0x378));
        if(distance<best){chosen=seat;best=distance;}
    }
    if(best==0x7fffffff)return 0;
    A.active=(uint8_t)(balloon?2:1);A.owner=C.current;A.seat=(uint8_t)chosen;A.stack=(uint8_t)(r.s+2);A.old_x=R(0x27);A.old_page=R(0x28);
    for(int i=0;i<4;i++)A.input[i]=R(0x14+i);
    C.current=(uint8_t)chosen;target_project(&C.player[chosen]);
    P(0x14,A.pressed[chosen]);P(0x16,C.raw[chosen]&~0x30);P(0x15,0);P(0x17,0);
    P(0x27,R(0x330));P(0x28,R(0x348));A.targets[chosen]++;return 0;
}
static int target_end(uint16_t address) {
    (void)address;
    if(!s_on || !A.active || (cyc_mod_isolated() && A.active!=2))return 0;
    CycModRegs r={0};cyc_mod_regs(&r);if(r.s!=A.stack)return 0;
    int chosen=C.current,warp=R(0x22)==14 && R(0x30)==15 && R(0xEB)<24 && R(0x300+R(0xEB))==0xB3;
    int intro=R(0x30)==9 && R(0xEB)==R(0x146);
    target_capture(&C.player[chosen]);C.current=A.owner;target_project(&C.player[C.current]);
    for(int i=0;i<4;i++)P(0x14+i,A.input[i]);P(0x27,A.old_x);P(0x28,A.old_page);A.active=0;
    if(warp || intro) {
        /* Keep the real return chain, mapper and shared map script. The
         * actual portal recipient becomes its native player immediately. */
        C.anchor=C.current=(uint8_t)chosen;project(&C.player[chosen]);
        if(!mm4_controls_art(R(0xA0)))C.failures++;P(0x18,255);
        P(0x14,C.fresh[chosen]&~0x30);P(0x16,C.raw[chosen]&~0x30);P(0x15,0);P(0x17,0);
        W.active=(uint8_t)(warp?1:2);W.source_room=R(0x2A);if(warp)W.starts[chosen]++;warp_follow();
    }
    return 0;
}
static int world_field(int a) {
    if(a>=0x300 && a<0x5D0)return (a-0x300)%24>=5;
    return (a>=0x100 && a<0x110) || (a>=0x600 && a<0x610) || (a>=0x620 && a<0x630) ||
        (a>=0x670 && a<0x6c0) || (a>=0x780 && a<0x800) || a==0x19 || a==0x1A || a==0xBF ||
        /* Rain's native DF5F advances these on the one shared clock. */
        a==0x13A || a==0x13B || a==0x145 || a==0x13C || a==0x13D;
}
static void snapshot_ram(uint8_t out[2048]) { for(int a=0;a<2048;a++)out[a]=R(a); }
static void merge_world(const uint8_t before[2048],const uint8_t after[2048]) {
    int palette=0;
    for(int a=0;a<2048;a++)if(before[a]!=after[a] && world_field(a)) {
        P(a,after[a]);if((a>=0x600 && a<0x610)||(a>=0x620 && a<0x630))palette=1;
    }
    if(palette)P(0x18,255);
    /* Merge new requests in their native producer order, without replaying
     * existing requests or committing the private CPU stack/bank shadows. */
    for(int i=0;i<8;i++) {
        int q=(before[0xDA]+i)&7;
        if(before[0xDC+q]==0x88 && after[0xDC+q]!=0x88) {
            int dst=R(0xDA)&7;
            if(R(0xDC+dst)==0x88){P(0xDC+dst,after[0xDC+q]);P(0xDA,(dst+1)&7);}
        }
    }
}
static int hooks(int enable) {
    if(enable) {
        s_hook_count=nes_mod_function_hook_count();if(s_hook_count>128)return 0;
        for(int i=0;i<s_hook_count;i++) {
            HookSetting *h=&s_hooks[i];nes_mod_function_hook_at(i,&h->id,NULL,&h->enabled,NULL);
            int allowed=!strcmp(h->id,"mm4.proto.shot") || !strcmp(h->id,"mm4.proto.muzzle") || !strcmp(h->id,"mm4.proto.damage");
            allowed|=!strcmp(h->id,"mm4.proto.shield") || !strcmp(h->id,"mm4.proto.skip-reflected");
            allowed|=!strcmp(h->id,"mm4.coop.personal-end");
            allowed|=!strcmp(h->id,"mm4.coop.marine-out");
            allowed|=!strncmp(h->id,"mm4.coop.bright-",16);
            allowed|=!strcmp(h->id,"mm4.coop.balloon-floor");
            allowed|=!strcmp(h->id,"mm4.coop.rain-frozen");
            allowed|=!strcmp(h->id,"mm4.coop.target-begin") || !strcmp(h->id,"mm4.coop.target-end");
            allowed|=!strcmp(h->id,"mm4.coop.dying") || !strcmp(h->id,"mm4.coop.pit") || !strcmp(h->id,"mm4.coop.death-music") || !strcmp(h->id,"mm4.coop.death-wait");
            if(h->enabled && !allowed)nes_mod_set_function_hook_enabled(h->id,0);
        }
        cyc_mod_allow_isolated_hooks(true);
    } else {
        cyc_mod_allow_isolated_hooks(false);
        for(int i=0;i<s_hook_count;i++)nes_mod_set_function_hook_enabled(s_hooks[i].id,s_hooks[i].enabled);
    }
    return 1;
}
static int center_camera(void) {
    int start=R(0xF9)-R(0x2E),camera=((R(0x348)<<8)|R(0x330))-128;
    int left=start*256,right=(start+R(0x2D))*256;
    if(start>=0 && !(R(0x2B)&0xc0)) {if(camera<left)camera=left;if(camera>right)camera=right;}
    if(camera<0)camera=0;
    if(start>=0 && !(R(0x2B)&0xc0))P(0x2E,(camera>>8)-start);
    P(0xFC,camera);P(0xF9,camera>>8);return camera;
}
static int death_wait_end(uint16_t address){(void)address;return s_death_wait && cyc_mod_isolated();}
static int dead_step(int seat){
    if(C.player[seat].dead!=1)return 1;
    C.waited[seat]=1;
    int timer=personal(&C.player[seat],0x133)|(personal(&C.player[seat],0x134)<<8);
    /* Keep the final native decrement for the real shared retry path. */
    if(timer<=1){C.player[seat].dead=2;return 1;}
    if(!cyc_mod_isolate_begin())return 0;
    int owner=C.current;C.current=(uint8_t)seat;project(&C.player[seat]);
    int enabled=hooks(1),ok=enabled;s_death_wait=1;CycModRegs r;cyc_mod_regs(&r);
    if(ok)ok=cyc_mod_call(0xC7DD,&r);s_death_wait=0;
    if(ok){capture(&C.player[seat]);C.updates[seat]++;}
    if(enabled)hooks(0);cyc_mod_isolate_end();C.current=(uint8_t)owner;return ok;
}
static int private_step(int seat) {
    if(C.player[seat].dead)return dead_step(seat);
    if(C.teleport[seat]==TELE_HIDDEN)return 1;
    if(C.teleport[seat]==TELE_OUT && personal(&C.player[seat],0x390)) {
        C.teleport[seat]=TELE_HIDDEN;set_personal(&C.player[seat],0x300,0);return 1;
    }
    int state=personal(&C.player[seat],0x30);
    if((!C.teleport[seat] && state>6) || R(0x30)>=8)return 1;
    Target target_before=A;Player partner_before=C.player[seat^1],own_before=C.player[seat];
    uint8_t before[2048],after[2048];snapshot_ram(before);
    if(!cyc_mod_isolate_begin())return 0;
    int owner=C.current;C.current=(uint8_t)seat;project(&C.player[seat]);center_camera();
    A.pressed[seat]=(uint8_t)(C.raw[seat]&~C.player[seat].previous&~0x30);
    P(0x14,A.pressed[seat]);P(0x16,C.raw[seat]&~0x30);P(0x15,0);P(0x17,0);
    int bright_ticked=E.ticked;
    s_scope=1;int enabled=hooks(1),ok=enabled;CycModRegs r;cyc_mod_regs(&r);
    P(0xf5,0x3c);P(0xf6,0x3d);if(ok)ok=cyc_mod_call(0xff37,&r);
    if(ok)ok=mm4_controls_art(R(0xa0));
    if(ok)mm4_controls_dispatch(0x80da);
    if(ok && C.teleport[seat]==TELE_IN) {
        /* A rejoin beam may pass roofs above its checked landing. Native
         * acceleration/motion runs until that floor, then its original
         * terrain probe and opening tail take over (also above native Y64). */
        r.x=0;
        if(R(0x378)<C.arrival_floor[seat]) {
            ok=cyc_mod_call(0xF2A7,&r);
            if(R(0x390) || R(0x378)>C.arrival_floor[seat])P(0x378,C.arrival_floor[seat]);
            P(0x390,0);P(0x3E,R(0x378));P(0x3F,0);P(0x540,0);P(0x570,0);
        } else {
            r.y=2;ok=cyc_mod_call(0xD428,&r);P(0x44,R(0x10));
            if(ok){r.x=0;ok=cyc_mod_call(0x873A,&r);}
        }
    } else if(ok)ok=cyc_mod_call(0x8000,&r);
    if(ok) {P(0xf5,0x3a);P(0xf6,0x3b);ok=cyc_mod_call(0xff37,&r);}
    s_personal_bhv=1;if(ok)ok=cyc_mod_call(0x8003,&r);s_personal_bhv=0;
    if(ok)ok=mm4_helpers_private_contact();
    if(ok){
        if(C.teleport[seat]==TELE_IN && R(0x30)!=10) {
            C.teleport[seat]=TELE_NONE;C.withdrawn&=(uint8_t)~(1u<<seat);C.rejoins[seat]++;
            P(0x3C,60);P(0x133,0);P(0x134,0);
        }
        capture(&C.player[seat]);snapshot_ram(after);C.updates[seat]++;C.player[seat].previous=C.raw[seat];
    }
    if(enabled)hooks(0);s_scope=0;cyc_mod_isolate_end();C.current=(uint8_t)owner;
    mm4_helpers_commit(ok);
    if(!ok){E.ticked=bright_ticked;E.restore=-1;A=target_before;C.player[seat^1]=partner_before;C.player[seat]=own_before;}
    if(ok)merge_world(before,after);return ok;
}
static void initialize(void) {
    C.anchor=C.current=0;C.stage=R(0x22);C.initialized=1;C.menu=0;C.pending_pause=255;
    capture(&C.player[0]);C.player[1]=C.player[0];
    C.player[0].dead=C.player[1].dead=0;
    C.withdrawn=C.waiting=0;memset(C.select_hold,0,sizeof C.select_hold);memset(C.select_armed,0,sizeof C.select_armed);memset(C.teleport,0,sizeof C.teleport);
    C.checkpoint=R(0x1F);C.revive_pending=0;
    if(C.retry){for(int seat=0;seat<2;seat++)for(int i=0;i<13;i++)set_personal(&C.player[seat],0xB1+i,C.retry_ammo[seat][i]);project(&C.player[0]);C.retry=0;}
    int x=(R(0x348)<<8)|R(0x330);x=x>=32?x-16:x+16;
    set_personal(&C.player[1],0x330,x);set_personal(&C.player[1],0x348,x>>8);
    for(int seat=0;seat<2;seat++)C.player[seat].previous=C.raw[seat];
}
static int safe_arrival(int *x,int *y) {
    if(R(0x30)!=0 || R(0x390))return 0;
    static const int offsets[]={24,-24,0,8,-8,16,-16,32,-32};
    int base=(R(0x348)<<8)|R(0x330),yy=R(0x378),start=R(0xF9)-R(0x2E);
    if(start<0 || yy<16 || yy>222)return 0;
    int lo=start*256+8,hi=(start+R(0x2D)+1)*256-9;
    if(R(0x2B)&0xC0){lo=R(0x348)*256+8;hi=lo+239;}
    for(unsigned i=0;i<sizeof offsets/sizeof *offsets;i++) {
        int xx=base+offsets[i];if(xx<lo || xx>hi)continue;
        if(mm4_world_class(xx-7,yy-12)==0 && mm4_world_class(xx+7,yy-12)==0 && mm4_world_class(xx,yy)==0 &&
           mm4_world_class(xx-6,yy+12)==0x10 && mm4_world_class(xx+6,yy+12)==0x10){*x=xx;*y=yy;return 1;}
    }
    return 0;
}
static void start_arrival(int seat,int x,int y) {
    Player *p=&C.player[seat];
    /* Only body/motion follows the survivor. HP, weapon, ammo and palette
     * retain their personal owner throughout the native entry beam. */
    for(int base=0x300;base<=0x5B8;base+=24) {
        set_personal(p,base,R(base));for(int slot=1;slot<5;slot++)set_personal(p,base+slot,0);
    }
    for(int a=0x30;a<=0x3F;a++)set_personal(p,a,0);
    set_personal(p,0x30,10);set_personal(p,0x31,0);
    set_personal(p,0x330,x);set_personal(p,0x348,x>>8);set_personal(p,0x378,0);set_personal(p,0x390,0);
    /* MM4's native entry starts at -1 in its upward-positive velocity
     * convention. The original gravity routine accelerates it downward. */
    set_personal(p,0x318,0);set_personal(p,0x360,0);set_personal(p,0x3D8,0);set_personal(p,0x3F0,0xFF);
    set_personal(p,0x558,0x13);set_personal(p,0x540,0);set_personal(p,0x570,0);set_personal(p,0x5B8,0);
    set_personal(p,0x528,0x80|(R(0x528)&0x60));set_personal(p,0x133,0);set_personal(p,0x134,0);
    C.teleport[seat]=TELE_IN;C.arrival_floor[seat]=(uint8_t)y;p->previous=C.raw[seat];
    memset(C.select_hold,0,sizeof C.select_hold);memset(C.select_armed,0,sizeof C.select_armed);
}
static void checkpoint_recovery(void) {
    int checkpoint=R(0x1F);
    if(checkpoint!=C.checkpoint){
        /* Observe the original C7B2 checkpoint result. The survivor must
         * actually reach it; positioning never supplies a recovery flag. */
        if(checkpoint>C.checkpoint)for(int seat=0;seat<2;seat++)
            if(C.player[seat].dead)C.revive_pending|=(uint8_t)(1u<<seat);
        C.checkpoint=(uint8_t)checkpoint;
    }
    if(!C.revive_pending || C.scene_active || A.active || W.active || R(0x19) || R(0x1A))return;
    for(int seat=0;seat<2;seat++)if(C.revive_pending&(1u<<seat)){
        if(!C.player[seat].dead){C.revive_pending&=(uint8_t)~(1u<<seat);continue;}
        if(!mm4_coop_live(seat^1))continue;
        int x,y;if(!safe_arrival(&x,&y))continue;
        Player *p=&C.player[seat];
        set_personal(p,0xB0,(personal(p,0xB0)&0xE0)|28);set_personal(p,0xA0,0);
        p->dead=0;C.withdrawn|=(uint8_t)(1u<<seat);
        start_arrival(seat,x,y);C.revive_pending&=(uint8_t)~(1u<<seat);
    }
}
static void depart(int seat) {
    Player *p=&C.player[seat];C.withdrawn|=(uint8_t)(1u<<seat);C.withdrawals[seat]++;C.teleport[seat]=TELE_OUT;
    for(int slot=1;slot<5;slot++)set_personal(p,0x300+slot,0);
    set_personal(p,0x30,8);set_personal(p,0x558,0);set_personal(p,0x35,0);set_personal(p,0x36,0);set_personal(p,0x3C,2);
    memset(C.select_hold,0,sizeof C.select_hold);memset(C.select_armed,0,sizeof C.select_armed);
    if(seat==C.anchor) {
        C.current=C.anchor=(uint8_t)(seat^1);project(&C.player[C.anchor]);
        if(!mm4_controls_art(R(0xA0)))C.failures++;P(0x18,255);
    }
}
static void select_tick(void) {
    int gameplay=!C.menu && !A.active && !W.active && !C.scene_active && R(0x22)<16 && R(0x30)<=4 && !R(0x19) && !R(0x1A);
    for(int seat=0;seat<2;seat++) {
        Player *p=&C.player[seat];int held=(C.raw[seat]&32)!=0;
        if(!gameplay || p->dead || (C.waiting&(1u<<seat)) || (C.teleport[seat]!=TELE_NONE && C.teleport[seat]!=TELE_HIDDEN)) {C.select_hold[seat]=0;continue;}
        if(!held){C.select_armed[seat]=1;C.select_hold[seat]=0;}
        if(!mm4_coop_live(seat^1) || C.teleport[seat^1]!=TELE_NONE){C.select_hold[seat]=0;continue;}
        if(C.withdrawn&(1u<<seat)) {
            if(held && C.select_armed[seat]) {
                int x,y;C.select_armed[seat]=0;
                if(safe_arrival(&x,&y))start_arrival(seat,x,y);
            }
        } else if(held && C.select_armed[seat] && personal(p,0x30)<=3 && ++C.select_hold[seat]>=90)depart(seat);
    }
}
static int controller(uint16_t address) {
    (void)address;if(!s_on || cyc_mod_isolated())return 0;
    E.ticked=0;E.restore=-1;
    C.waited[0]=C.waited[1]=0;
    if(C.menu && R(0x22)<16) {
        capture(&C.player[C.pauser]);C.current=C.anchor;project(&C.player[C.anchor]);C.menu=C.menu_ready=C.menu_busy=0;P(0x18,255);
    }
    if(C.initialized && R(0x22)<16 && R(0x22)!=C.stage)C.initialized=0;
    if(!C.initialized) {
        if(R(0x22)>=16 || R(0x300)!=1 || R(0x30)!=0 || !(R(0xB0)&31))return 0;
        initialize();
    }
    if(C.menu || R(0x22)>=16)return 0;
    C.current=C.anchor;capture(&C.player[C.anchor]);
    test_pokes();
    select_tick();
    checkpoint_recovery();
    for(int seat=0;seat<2;seat++)C.previous_x[seat]=(uint16_t)(personal(&C.player[seat],0x330)+256*personal(&C.player[seat],0x348));
    if(!private_step(C.anchor^1))C.failures++;
    C.current=C.anchor;A.pressed[C.anchor]=(uint8_t)(C.raw[C.anchor]&~C.player[C.anchor].previous&~0x30);
    P(0x14,A.pressed[C.anchor]);P(0x16,C.raw[C.anchor]&~0x30);
    P(0x15,0);P(0x17,0);return 0;
}
static int after_controller(uint16_t address) {
    if(!cyc_mod_isolated()) {
        unsigned timer=(unsigned)R(0x13C)|((unsigned)R(0x13D)<<8);
        int missing=s_on?(C.initialized && !C.menu && !E.ticked):R(0xA0)!=12;
        /* A hidden/dead/waiting caster, or co-op deactivation, cannot leave
         * the shared world permanently frozen. Native callers still own
         * their normal decrement; this fills only a missing world tick. */
        if(timer && missing){timer--;P(0x13C,timer);P(0x13D,timer>>8);E.ticked=1;if(!timer && !s_on)effect_hooks();}
    }
    (void)address;if(s_on && C.initialized && !cyc_mod_isolated() && !C.menu) {
        capture(&C.player[C.anchor]);C.updates[C.anchor]++;C.player[C.anchor].previous=C.raw[C.anchor];
        if(W.active){warp_follow();if(R(0x30)<8 && (W.active==2 || R(0x2A)!=W.source_room))W.active=0;}
    }return 0;
}
static int other_collision(int seat,int actor) {
    if(C.player[seat].dead || C.teleport[seat] || (C.withdrawn&(1u<<seat)) || !(personal(&C.player[seat],0xB0)&31) || !R(0x300+actor) || R(0x30)>=8)return 1;
    int boss=actor==R(0x146) && (R(0x132)&128) && R(0x450+actor)>0,finisher=0;
    /* Native reflected-shot calls need their own isolated machine snapshot.
     * Run them with the guest projected before opening its collision scope;
     * the real native shield contact hook consumes a prepared constructor. */
    int owner=C.current;
    if(mm4_proto_enabled()){
        Player primary;capture(&primary);C.current=(uint8_t)seat;project(&C.player[seat]);
        mm4_proto_guest_collision(actor);capture(&C.player[seat]);
        C.current=(uint8_t)owner;project(&primary);
    }
    uint8_t before[2048],after[2048];snapshot_ram(before);
    if(!cyc_mod_isolate_begin()){mm4_proto_guest_collision_end();return 0;}
    C.current=(uint8_t)seat;project(&C.player[seat]);
    s_scope=1;int enabled=hooks(1),ok=enabled;CycModRegs r;cyc_mod_regs(&r);
    P(0xF5,0x3A);P(0xF6,0x3B);if(ok)ok=cyc_mod_call(0xFF37,&r);
    r.x=(uint8_t)actor;P(0xEB,actor);s_collision=1;
    if(ok && R(0x300+actor))ok=cyc_mod_call(0x8097,&r);s_collision=0;
    if(ok){finisher=boss && !R(0x450+actor);capture(&C.player[seat]);snapshot_ram(after);}
    if(enabled)hooks(0);s_scope=0;cyc_mod_isolate_end();C.current=(uint8_t)owner;
    mm4_proto_guest_collision_end();
    if(ok)merge_world(before,after);
    if(finisher) {
        /* The winning seat owns the original victory/weapon-get script.
         * Keep the live CPU, stack, banks and shared world on their real clock. */
        capture(&C.player[owner]);C.anchor=C.current=(uint8_t)seat;project(&C.player[seat]);
        /* Physical CHR and the next native palette upload must belong to
         * the promoted seat as well as its personal CPU RAM. */
        if(!mm4_controls_art(R(0xA0)))C.failures++;
        P(0x18,255);
        P(0x14,C.fresh[seat]&~0x30);P(0x16,C.raw[seat]&~0x30);P(0x15,0);P(0x17,0);
    }
    return ok;
}
static int personal_end(uint16_t address) {
    (void)address;
    /* The common collision tail jumps here. A scoped collision call has
     * finished; return to its caller before the native actor loop advances. */
    if(s_collision && cyc_mod_isolated())return 1;
    if(s_on && C.initialized && !C.menu && !cyc_mod_isolated() && R(0xEB)==4)
        if(!mm4_helpers_shared_contact())C.failures++;
    /* Finish the native loop at its common terrain-hazard check, after slot4.
     * Returning from here would incorrectly omit the player's spike checks. */
    if(s_on && s_scope && s_personal_bhv && R(0xEB)>=4)P(0xEB,23);
    if(s_on && C.initialized && !C.menu && !cyc_mod_isolated() && R(0xEB)>=5 && R(0xEB)<24)
        if(!other_collision(C.anchor^1,R(0xEB)))C.failures++;
    return 0;
}
static int bright_tick_before(uint16_t address) {
    (void)address;if(!s_on || !C.initialized || (cyc_mod_isolated() && !s_scope))return 0;
    E.restore=E.ticked?(R(0x13C)|(R(0x13D)<<8)):-1;return 0;
}
static int rain_frozen(uint16_t address) {
    (void)address;if(!s_on || !C.initialized || !R(0x13A) || !(R(0x13C)|R(0x13D)))return 0;
    CycModRegs r;cyc_mod_regs(&r);if(r.x<5 || r.x>=24)return 0;
    int flag=R(0x5B8+r.x);if(flag!=0x81 && flag!=0x82)return 0;
    /* Native Rain rejects an existing damage timer. Flash Stopper's
     * separate freeze values must not make the target immune to damage.
     * Leave RAM untouched so immune targets retain their original freeze. */
    r.a=0;r.p=(uint8_t)((r.p&~0x82)|2);cyc_mod_set_regs(&r);return 0;
}
static int bright_tick_after(uint16_t address) {
    (void)address;if(!s_on || !C.initialized || (cyc_mod_isolated() && !s_scope))return 0;
    /* Original init/cost, Buster and per-player flash still run. A second
     * Bright controller cannot decrement the same shared freeze twice. */
    if(E.restore>=0){P(0x13C,E.restore);P(0x13D,E.restore>>8);}
    E.restore=-1;E.ticked=1;return 0;
}
static int world_begin(uint16_t address) { (void)address;if(s_on && C.initialized && !cyc_mod_isolated())C.world_updates++;return 0; }
static int marine_out(uint16_t address) {
    (void)address;
    if(!s_on || !s_scope || !cyc_mod_isolated() || R(0x30)!=4 || R(0xA0)!=3 || (R(0xB3)&127))return 0;
    /* Native FD75 has spent the last fuel and F446 selected the on-foot
     * animation. A guest uses immutable cartridge patterns, so its native
     * CHR upload and FF1C/NMI wait have no PPU work to publish. Finish the
     * original controller tail without blocking this unclocked call. */
    P(0x30,0);CycModRegs r;cyc_mod_regs(&r);r.a=r.x=0;
    r.p=(uint8_t)((r.p&~0x82)|2);cyc_mod_set_regs(&r);return 1;
}
static int dying(uint16_t address){
    (void)address;if(!s_on || !C.initialized || (cyc_mod_isolated() && !s_scope))return 0;
    C.player[C.current].dead=1;
    for(int slot=1;slot<5;slot++)P(0x300+slot,0);
    return 0;
}
static int death_music(uint16_t address){
    (void)address;if(!s_on || !C.initialized || (cyc_mod_isolated() && !s_scope) || !mm4_coop_live(C.current^1))return 0;
    CycModRegs r;cyc_mod_regs(&r);int caller=R(0x100|((r.s+1)&255))|(R(0x100|((r.s+2)&255))<<8);
    return r.a==0xF0 && (caller==0x824B || caller==0xDC29);
}
static int after_world(uint16_t address){
    (void)address;if(!s_on || !C.initialized || C.menu || cyc_mod_isolated())return 0;
    if(C.waiting && (C.player[0].dead || C.player[1].dead || C.withdrawn))finish_wait();
    int other=C.anchor^1;
    if(!C.player[C.anchor].dead || (C.withdrawn&(1u<<other)) || (C.player[other].dead && !(C.player[other].dead==2 && C.player[C.anchor].dead!=2)))return 0;
    capture(&C.player[C.anchor]);
    /* Promotion replaces the old anchor before the real frame-end counter.
     * Give that seat its original decrement before projecting the survivor. */
    if(!dead_step(C.anchor))C.failures++;
    C.current=C.anchor=(uint8_t)other;project(&C.player[other]);
    if(mm4_coop_independent() && !(R(0x2B)&0xC0) && !mm4_coop_scene_locked()) {
        int previous=(R(0xF9)<<8)|R(0xFC),camera=center_camera();
        /* The survivor is the real native player immediately. Its source
         * camera and previous-body probes must transfer before the original
         * scroll/draw pass can treat that distant body as an offscreen actor. */
        P(0x25,R(0xFC));P(0x27,R(0x330));P(0x28,R(0x348));
        P(0x2F,(R(0x2F)&~1)|(camera>=previous));
    }
    if(!mm4_controls_art(R(0xA0)))C.failures++;P(0x18,255);
    P(0x14,C.fresh[other]&~0x30);P(0x16,C.raw[other]&~0x30);P(0x15,0);P(0x17,0);return 0;
}
static int retry(uint16_t address){
    (void)address;if(!s_on || !C.initialized || cyc_mod_isolated() || R(0x30)!=7 || C.retry)return 0;
    capture(&C.player[C.current]);C.player[C.current].dead=2;C.retry=1;
    for(int seat=0;seat<2;seat++)for(int i=0;i<13;i++)C.retry_ammo[seat][i]=(uint8_t)mm4_coop_read(seat,(uint16_t)(0xB1+i));return 0;
}
static int arrival(uint16_t address){
    (void)address;if(s_on && !cyc_mod_isolated()){A.active=W.active=0;C.initialized=C.menu=C.scene_active=0;C.current=C.anchor=0;C.pending_pause=255;}return 0;
}
static int frame_end(uint16_t address){
    (void)address;if(!s_on || !C.initialized || C.menu || cyc_mod_isolated())return 0;
    int other=C.anchor^1;
    /* A native collision or DB72 draw may first declare death after the
     * personal controller ran. Count that frame once, like the native anchor. */
    if(C.player[other].dead==1 && !C.waited[other] && !dead_step(other))C.failures++;
    return after_world(address);
}
static void scene_follow(void) {
    if(!s_on || !C.initialized || C.menu || cyc_mod_isolated() || !C.scene_active || R(0x22)>=16)return;
    int seat=C.anchor^1;Player *p=&C.player[seat];
    if(p->dead || (C.withdrawn&(1u<<seat)) || !(personal(p,0xB0)&31))return;
    /* The native script advances once. Carry the other body on that same
     * clock, including intermediate vertical/door animation frames. */
    C.current=C.anchor;capture(&C.player[C.anchor]);
    for(int base=0x300;base<=0x5B8;base+=24)set_personal(p,base,R(base));
    for(int a=0x30;a<=0x3F;a++)if(a!=0x3C)set_personal(p,a,R(a));
    int x=(R(0x348)<<8)|R(0x330);if(C.scene_active==1)x-=16;if(x<0)x=0;
    set_personal(p,0x330,x);set_personal(p,0x348,x>>8);
    set_personal(p,0x3C,60);set_personal(p,0x5B8,60);
    for(int slot=1;slot<5;slot++)set_personal(p,0x300+slot,0);
    p->previous=C.raw[seat];
}
static int before_horizontal_scroll(uint16_t address) {
    (void)address;if(s_on && C.initialized && !C.menu && !cyc_mod_isolated()){
        CycModRegs r;cyc_mod_regs(&r);C.scene_stack=r.s;C.scene_active=1;scene_follow();
    }return 0;
}
static int vertical_intent(const Player *p) {
    int page=personal(p,0x348),independent=mm4_coop_independent();
    if(personal(p,0x390) || (page!=R(0xF9) && (!independent || (R(0x2B)&0xC0))))return 0;
    int offset=page-R(0xF9)+R(0x2E);if(offset<0 || offset>R(0x2D))return 0;
    int y=personal(p,0x378),direction=y>=0xE8?4:(y<8 && personal(p,0x30)==3?8:0);
    if(!direction)return 0;
    if(R(0x2B)&0xC0)return 1;
    if(!independent && R(0xFC))return 0;
    int room=R(0x2B)&15,bank=R(0x22)|0x20,flags=direction<<4,forward=0;
    /* CD24's horizontal/vertical junctions and CCD0's authored links. */
    if(!R(0x2D))forward=(mm4_cart_prg(bank,0xB531+room)&flags)!=0;
    else if(offset){
        if(offset!=R(0x2D))return 0;
        forward=1;
    }
    if(forward){if(mm4_cart_prg(bank,0xB531+room)&flags&0xC0)return 1;}
    else if(room){
        int previous=mm4_cart_prg(bank,0xB52F+room),current=mm4_cart_prg(bank,0xB530+room);
        if(((current&0xC0) || !(previous&0x20)) && (previous&0xC0) && ((previous^0xC0)&flags))return 1;
    }
    for(int at=0;at<256;at+=4){
        int linked_page=mm4_cart_prg(bank,0xB540+at);if(linked_page&128)break;
        if(linked_page==page && (mm4_cart_prg(bank,0xB541+at)&flags))return 1;
    }
    return 0;
}
static void finish_wait(void) {
    if(!C.waiting)return;
    for(int seat=0;seat<2;seat++)if(C.waiting&(1u<<seat)) {
        Player *p=&C.player[seat];C.teleport[seat]=TELE_NONE;
        set_personal(p,0x390,C.wait_out[seat]);set_personal(p,0x528,C.wait_flags[seat]);
        if(C.initialized && C.current==seat)project(p);
    }
    C.waiting=0;memset(C.select_hold,0,sizeof C.select_hold);memset(C.select_armed,0,sizeof C.select_armed);
}
static int partner_present(int seat) {
    return !C.player[seat].dead && !(C.withdrawn&(1u<<seat)) && (personal(&C.player[seat],0xB0)&31);
}
static int wait_for_partner(int seat,int direction) {
    int other=seat^1;Player *p=&C.player[seat],*peer=&C.player[other];
    if(!s_wait_both || !partner_present(other)){finish_wait();return 0;}
    if(direction==1) {
        if(!C.teleport[other] && !personal(peer,0x390) && personal(peer,0x348)==personal(p,0x348) && personal(peer,0x330)>=224)return 0;
        /* Hold just inside the native shutter probe, so a real Right step
         * retries its motion-sensitive admission after the partner arrives. */
        if(personal(p,0x330)>231)set_personal(p,0x330,231);
        set_personal(p,0x318,0);
    } else {
        if((C.waiting&(1u<<other)) && C.wait_direction[other]==direction && C.wait_page[other]==personal(p,0x348)){finish_wait();return 0;}
        if(C.waiting&(1u<<other)) {
            /* A different exit remains reversible inside the current room. */
            set_personal(p,0x378,direction==8?24:216);set_personal(p,0x390,0);set_personal(p,0x30,0);
            set_personal(p,0x360,0);set_personal(p,0x3D8,0);set_personal(p,0x3F0,0);
        } else {
            C.wait_direction[seat]=(uint8_t)direction;C.wait_page[seat]=(uint8_t)personal(p,0x348);
            C.wait_out[seat]=(uint8_t)personal(p,0x390);C.wait_flags[seat]=(uint8_t)personal(p,0x528);
            for(int base=0x300;base<=0x5B8;base+=24)for(int slot=1;slot<5;slot++)set_personal(p,base+slot,0);
            set_personal(p,0x528,0);C.teleport[seat]=TELE_HIDDEN;C.waiting|=(uint8_t)(1u<<seat);
            memset(C.select_hold,0,sizeof C.select_hold);memset(C.select_armed,0,sizeof C.select_armed);
        }
    }
    if(seat==C.current)project(p);
    if(C.teleport[seat]==TELE_HIDDEN && seat==C.anchor) {
        C.anchor=C.current=(uint8_t)other;project(peer);P(0x18,255);
        if(!mm4_controls_art(R(0xA0)))C.failures++;
    }
    return 1;
}
/* The original C5A6 column loop yields through FF1C/NMI. Refill the newly
 * selected source page on that real clock, then publish its native PPU origin
 * before the unchanged room director resumes. Player/world AI does not rerun. */
static void source_hooks(void) {
    nes_mod_set_function_hook_enabled("mm4.coop.before-director",s_on || C.source_pending);
    nes_mod_set_function_hook_enabled("mm4.coop.source-start",C.source_pending!=0);
    nes_mod_set_function_hook_enabled("mm4.coop.source-loaded",C.source_pending!=0);
}
static int source_start(uint16_t address) {
    (void)address;if(C.source_pending!=3 || cyc_mod_isolated())return 0;
    CycModRegs r={0};cyc_mod_regs(&r);if(r.s!=(uint8_t)(C.source_regs.s-6))return 0;
    P(0x2A,C.source_page);P(0x45,C.source_page+1);C.source_pending=1;return 0;
}
static int source_loaded(uint16_t address) {
    (void)address;if(C.source_pending!=1 || cyc_mod_isolated())return 0;
    CycModRegs r={0};cyc_mod_regs(&r);if(r.s!=(uint8_t)(C.source_regs.s-6))return 0;
    P(0xF9,C.source_page);P(0xFC,0);P(0x25,0);P(0x2E,C.source_offset);
    /* Use the initializer's complete viewport origin, including its next
     * column cursor. Keeping the old ring phase corrupts the ensuing scroll. */
    P(0x23,1);P(0x2F,1);P(0xFA,0);P(0xFF,R(0xFF)&~3);
    P(0x9E,mm4_cart_prg(R(0x22)|0x20,0xB300+C.source_page));P(0x9F,R(0x9E));
    P(0xF5,C.source_ram[1]);P(0xF6,C.source_ram[2]);
    C.source_pending=2;return 1; /* RTS enters one final native FF1C publication. */
}
static int source_begin(int page) {
    CycModRegs r={0};cyc_mod_regs(&r);C.source_regs=r;
    C.source_page=(uint8_t)page;C.source_offset=(uint8_t)(page-R(0xF9)+R(0x2E));
    C.source_nt=0;
    C.source_ram[0]=R(0x45);C.source_ram[1]=R(0xF5);C.source_ram[2]=R(0xF6);
    for(int i=0;i<4;i++)C.source_ram[3+i]=R(0x14+i);
    /* C96E runs inside the native update lock. NMI must process the real
     * column packets while this transaction yields, as it does at boot. */
    C.source_ram[7]=R(0x9A);P(0x9A,0);
    for(int i=0;i<10;i++)C.source_stack[i]=R(0x100|((r.s-9+i)&255));
    /* Drain the original pending queue before CFB1 reuses $0780, then load,
     * publish, restore the mapper and resume this exact director entry. */
    static const uint8_t chain[]={0x1B,0xFF,0xA5,0xC5,0x1B,0xFF,0x36,0xFF,0x6D,0xC9};
    for(int i=0;i<10;i++)P(0x100|((r.s-9+i)&255),chain[i]);
    C.source_pending=3;C.source_handoffs[C.anchor]++;
    source_hooks();r.s=(uint8_t)(r.s-10);cyc_mod_set_regs(&r);return 1;
}
static int source_needed(const Player *p) {
    return mm4_coop_independent() && !(R(0x2B)&0xC0) &&
        (personal(p,0x348)!=R(0xF9) || R(0xFC));
}
static int before_director(uint16_t address) {
    (void)address;
    if(C.source_pending==2 && !cyc_mod_isolated()) {
        CycModRegs r={0};cyc_mod_regs(&r);if(r.s!=C.source_regs.s)return 0;
        for(int i=0;i<10;i++)P(0x100|((r.s-9+i)&255),C.source_stack[i]);
        P(0x45,C.source_ram[0]);P(0xF5,C.source_ram[1]);P(0xF6,C.source_ram[2]);
        for(int i=0;i<4;i++)P(0x14+i,C.source_ram[3+i]);
        P(0x9A,C.source_ram[7]);
        cyc_mod_set_regs(&C.source_regs);C.source_pending=0;source_hooks();return 0;
    }
    if(!s_on || !C.initialized || C.menu || A.active || W.active || C.scene_active || cyc_mod_isolated() ||
       R(0x22)>=16 || R(0x30)>=8)return 0;
    capture(&C.player[C.anchor]);
    if(C.waiting)for(int seat=0;seat<2;seat++)if((C.waiting&(1u<<seat)) && !partner_present(seat^1)){finish_wait();break;}
    int anchor=C.anchor,anchor_x=personal(&C.player[anchor],0x330)+256*personal(&C.player[anchor],0x348);
    int end_page=R(0xF9)-R(0x2E)+R(0x2D);
    int anchor_right=!(R(0x2B)&0xC0) && (anchor_x>>8)==end_page && (personal(&C.player[anchor],0x3F)==0x50 || (anchor_x&255)>0xF0);
    int anchor_vertical=vertical_intent(&C.player[anchor]);
    if(anchor_right || anchor_vertical) {
        if(wait_for_partner(anchor,anchor_right?1:personal(&C.player[anchor],0x378)>=0xE8?4:8))return 1;
        if(source_needed(&C.player[anchor]))return source_begin(personal(&C.player[anchor],0x348));
        /* Admission now belongs to this real native body. Checking the
         * just-released peer again would put it back into waiting. */
        if(s_wait_both)return 0;
    }
    int seat=C.anchor^1;Player *p=&C.player[seat];
    if(!mm4_coop_live(seat) || C.teleport[seat] || personal(p,0x30)>4)return 0;
    int x=personal(p,0x330)+256*personal(p,0x348),end=R(0xF9)-R(0x2E)+R(0x2D);
    /* C96E/CA2E admit right exits only after real rightward motion on the
     * last room page, at a native shutter probe or beyond the screen edge.
     * Transfer the actual player before that director runs, leaving its
     * original stack, room selection and multi-frame scroll script intact. */
    int right=!(R(0x2B)&0xC0) && (x>C.previous_x[seat] || (s_wait_both && personal(p,0x3F)==0x50)) && (x>>8)==end &&
        (personal(p,0x3F)==0x50 || (x&255)>0xF0);
    if(!right && !vertical_intent(p))return 0;
    if(wait_for_partner(seat,right?1:personal(p,0x378)>=0xE8?4:8))return 1;
    capture(&C.player[C.anchor]);C.anchor=C.current=(uint8_t)seat;project(p);
    P(0x27,C.previous_x[seat]);P(0x28,C.previous_x[seat]>>8);
    if(!mm4_controls_art(R(0xA0)))C.failures++;P(0x18,255);
    P(0x14,A.pressed[seat]);P(0x16,C.raw[seat]&~0x30);P(0x15,0);P(0x17,0);
    if(source_needed(p))return source_begin(personal(p,0x348));
    if(right && R(0x2E)!=R(0x2D)) {
        center_camera();P(0x25,R(0xFC));
    }
    return 0;
}
static int after_horizontal_scroll(uint16_t address) {
    (void)address;
    if(!s_on || !C.initialized || C.menu || cyc_mod_isolated() || C.scene_active!=1 || R(0x22)>=16)return 0;
    CycModRegs r;cyc_mod_regs(&r);if(r.s!=C.scene_stack)return 0;
    /* The original 128-frame door script moved the real player and camera.
     * Bring the partner into that completed destination, retaining inventory. */
    scene_follow();C.scene_active=0;return 0;
}
static int before_vertical_scroll(uint16_t address) {
    (void)address;if(s_on && C.initialized && !C.menu && !cyc_mod_isolated()){
        CycModRegs r;cyc_mod_regs(&r);C.scene_stack=r.s;C.scene_active=2;scene_follow();
    }return 0;
}
static int after_vertical_scroll(uint16_t address) {
    (void)address;if(s_on && !cyc_mod_isolated() && C.scene_active==2){
        /* CE96 also returns from nested tile-streaming calls. Only the
         * original director's return completes this multi-frame scene. */
        CycModRegs r;cyc_mod_regs(&r);if(r.s==C.scene_stack){scene_follow();C.scene_active=0;}
    }return 0;
}
static int scene_draw(uint16_t address) {(void)address;scene_follow();return 0;}
static int pause_entry(uint16_t address) {
    (void)address;
    if(!s_on || !C.initialized || C.menu || C.pending_pause>1 || cyc_mod_isolated())return 0;
    int seat=C.pending_pause;C.pending_pause=255;
    if(R(0x22)>=16 || (personal(&C.player[seat],0x30)>4) || !(personal(&C.player[seat],0xB0)&31))return 0;
    capture(&C.player[C.anchor]);C.current=C.pauser=(uint8_t)seat;project(&C.player[seat]);
    C.menu_ready=C.menu_busy=0;C.menu_user=(uint8_t)seat;C.menu_pending[0]=C.menu_pending[1]=0;mm4_menu_reset();
    P(0x14,C.fresh[seat]);P(0x16,C.raw[seat]);C.menu=1;return 0;
}
static void menu_inventory_save(int seat) {
    for(int i=0;i<14;i++)set_personal(&C.player[seat],0xB0+i,R(0xB0+i));
    set_personal(&C.player[seat],0xA0,R(0xA0));set_personal(&C.player[seat],0x131,R(0x131));
    set_personal(&C.player[seat],0x138,R(0x138));
}
static void menu_inventory_project(int seat) {
    for(int i=0;i<14;i++)P(0xB0+i,personal(&C.player[seat],0xB0+i));
    for(int i=0;i<8;i++)P(0x610+i,personal(&C.player[seat],0x610+i));
    P(0xA0,personal(&C.player[seat],0xA0));P(0x131,personal(&C.player[seat],0x131));
    P(0x138,personal(&C.player[seat],0x138));C.current=C.menu_user=(uint8_t)seat;
}
static int menu_move(int seat,int buttons) {
    if(!(buttons&15))return 1;
    if((buttons&3)==3)buttons&=~3;if((buttons&12)==12)buttons&=~12;
    if(!buttons)return 1;if(!cyc_mod_isolate_begin())return 0;
    CycModRegs r;cyc_mod_regs(&r);P(0xF5,0x3C);P(0xF6,0x3D);int ok=cyc_mod_call(0xFF37,&r);
    for(int i=0;i<14;i++)P(0xB0+i,personal(&C.player[seat],0xB0+i));
    P(0xBE,0x9C);P(0x138,personal(&C.player[seat],0x138));P(0x139,R(0x138));P(0x14,buttons&15);
    if(ok)ok=cyc_mod_call(0x9922,&r);int cursor=R(0x138);
    cyc_mod_isolate_end();if(ok && cursor<16)set_personal(&C.player[seat],0x138,cursor);return ok && cursor<16;
}
static int menu_equip(int seat) {
    int cursor=personal(&C.player[seat],0x138),weapon=mm4_cart_prg(0x3C,0x9B71+cursor);
    if(weapon>13)return 1;if(!cyc_mod_isolate_begin())return 0;
    int owner=C.current;C.current=(uint8_t)seat;project(&C.player[seat]);
    int ok=mm4_controls_equip(weapon);if(ok)capture(&C.player[seat]);
    cyc_mod_isolate_end();C.current=(uint8_t)owner;return ok;
}
static int menu_idle(uint16_t address) {
    (void)address;if(!s_on || !C.menu || !mm4_coop_independent() || cyc_mod_isolated())return 0;
    C.menu_ready=1;C.menu_phase=R(0x95);
    if(C.menu_busy) {
        menu_inventory_save(C.menu_user);menu_inventory_project(C.pauser);C.menu_busy=0;
        CycModRegs r;cyc_mod_regs(&r);if(!cyc_mod_call_commit(0x9992,&r))C.failures++;
    }
    menu_inventory_save(C.pauser);
    int other=C.pauser^1;
    if(mm4_coop_live(other) && !menu_move(other,C.menu_pending[other]&15))C.failures++;
    P(0x14,R(0x14)|(C.menu_pending[C.pauser]&128));
    /* Equipment choices do not contend for shared stock. Commit the guest's
     * choice even when the pauser closes the menu on the same native tick. */
    if(mm4_coop_live(other) && (C.menu_pending[other]&128) && personal(&C.player[other],0x138)!=7)
        if(!menu_equip(other))C.failures++;
    /* A simultaneous E-tank request is admitted in stable seat order. Its
     * original real-clock refill must finish before another action runs. */
    for(int seat=0;seat<2;seat++)if(mm4_coop_live(seat) && (C.menu_pending[seat]&128)) {
        if(seat==C.pauser)break;
        int cursor=personal(&C.player[seat],0x138);
        if(cursor==7) {
            menu_inventory_project(seat);C.menu_busy=1;P(0x14,128);P(0x16,128);break;
        }
    }
    C.menu_pending[0]=C.menu_pending[1]=0;return 0;
}
void mm4_coop_input(uint8_t buttons[2]) {
    if(!s_on)return;
    for(int seat=0;seat<2;seat++){C.raw[seat]=buttons[seat];C.fresh[seat]=(uint8_t)(buttons[seat]&~C.raw_previous[seat]);C.raw_previous[seat]=buttons[seat];}
    for(int seat=0;seat<2;seat++)if(C.menu_ready && !C.menu_busy)C.menu_pending[seat]|=C.fresh[seat]&143;else C.menu_pending[seat]=0;
    if(C.menu)buttons[0]=C.raw[C.pauser];
    else if(!C.initialized || R(0x22)>=16 || (!mm4_coop_live(0) && !mm4_coop_live(1))){
        buttons[0]=(uint8_t)(C.raw[0]|C.raw[1]);if((buttons[0]&3)==3)buttons[0]&=(uint8_t)~3;if((buttons[0]&12)==12)buttons[0]&=(uint8_t)~12;
    }else {
        C.pending_pause=255;
        buttons[0]=C.raw[C.anchor];
        for(int seat=0;seat<2;seat++)if(mm4_coop_live(seat) && !C.teleport[seat] && (C.fresh[seat]&16)) {
            C.pending_pause=(uint8_t)seat;buttons[0]|=16;break;
        }
    }
    buttons[1]=0; /* Original pad-2 Select/B are native debug controls. */
}
void mm4_coop_end(void) {
    if(!s_on || A.active || C.source_pending)return;
    /* Native inventory and stage select both use screen16. Only the
     * explicitly owned personal menu retains its live stage contexts. */
    if(C.initialized && !C.menu && R(0x22)>=16){C.initialized=0;C.scene_active=0;C.pending_pause=255;}
    if(C.initialized && !C.menu && R(0x22)<16){C.current=C.anchor;capture(&C.player[C.anchor]);}
    if(!mm4_menu_capture())C.failures++;
    /* Consume personal button edges only when that native controller runs.
     * Advancing previous here loses a new press on an output-only lag frame. */
}
void mm4_coop_build(void) {
    if(C.source_pending)return;
    memset(&C.build,0,sizeof C.build);
    if(!s_on || !C.initialized || C.menu || R(0x22)>=16)return;
    int seat=C.anchor^1,owner=C.current;
    if(C.player[seat].dead || C.teleport[seat]==TELE_HIDDEN)return;
    uint8_t before[2048],after[2048];snapshot_ram(before);
    if(!cyc_mod_isolate_begin())return;
    C.build.camera=(uint16_t)((R(0xF9)<<8)|R(0xFC));C.current=(uint8_t)seat;project(&C.player[seat]);int camera=center_camera();
    s_scope=1;int enabled=hooks(1);
    int art=mm4_controls_art(R(0xa0));
    P(0x97,4);for(int i=0;i<256;i+=4)P(0x200+i,248);
    int ok=art && enabled,body=0,complete=mm4_no_flicker_enabled() || mm4_coop_independent();
    for(int slot=0;slot<5 && ok;slot++) {
        if(!R(0x300+slot))continue;
        /* Capture each actor into its own native OAM scratch when drawing
         * complete sprites. One busy helper cannot truncate later weapons. */
        if(complete){P(0x97,4);for(int i=0;i<256;i+=4)P(0x200+i,248);}
        int begin=R(0x97);CycModRegs r;cyc_mod_regs(&r);r.x=(uint8_t)slot;P(0x96,slot);
        ok=cyc_mod_call(0xDB72,&r);int end=R(0x97);if(!end)end=256;
        if(!slot && end>begin){mm4_proto_body(&C.build.body);C.build.body.x+=(int16_t)(camera-C.build.camera);body=1;}
        for(int offset=begin;ok && offset<end && C.build.count<PART_CAP;offset+=4) {
            int top=R(0x200+offset)+1;if(top>=240)continue;
            Part *p=&C.build.parts[C.build.count++];p->x=(int16_t)(camera+R(0x203+offset));p->y=(int16_t)top;
            p->owner=(uint8_t)slot;p->attr=R(0x202+offset);
            int tile=R(0x201+offset),table=cyc_render_line_sprite_table(top);
            for(int i=0;i<4;i++)p->colors[i]=R(0x610+(p->attr&3)*4+i)&63;
            for(int y=0;y<8;y++) {
                int row=(p->attr&128)?7-y:y,a=table+tile*16+row;
                int low=mm4_controls_pattern(R(0xa0),a),high=mm4_controls_pattern(R(0xa0),a+8);
                for(int x=0;x<8;x++){int bit=(p->attr&64)?x:7-x;p->pixels[y*8+x]=(uint8_t)(((low>>bit)&1)|(((high>>bit)&1)<<1));}
            }
        }
        if(!complete && end==256)break;
    }
    int died=C.player[seat].dead!=0;
    if(ok){capture(&C.player[seat]);C.build.valid=1;C.build.seat=(uint8_t)seat;if(!body)C.build.body.active=0;if(died)snapshot_ram(after);}else C.failures++;
    if(enabled)hooks(0);s_scope=0;cyc_mod_isolate_end();C.current=(uint8_t)owner;
    if(ok && died)merge_world(before,after);
}
void mm4_coop_latch(void) { C.frame=C.build; }
void mm4_coop_draw(uint32_t *out,int width,int origin,const uint8_t *opaque,uint8_t *claimed) {
    if(!s_on || !C.frame.valid || C.menu)return;
    int painted=0;
    for(int i=0;i<C.frame.count;i++) {
        const Part *p=&C.frame.parts[i];
        if(!p->owner && C.frame.body.active) {
            if(!painted){mm4_view_draw_character(out,width,origin,&C.frame.body);painted=1;}
            if(C.frame.body.state!=4 || C.frame.body.weapon!=3)continue;
        }
        for(int y=0;y<8;y++)for(int x=0;x<8;x++) {
            int value=p->pixels[y*8+x],px=p->x-C.frame.camera+origin+x,py=p->y+y;
            if(!value || px<0 || px>=width || py<0 || py>=240)continue;
            if(!p->owner && C.frame.body.active && !mm4_view_native_body_pixel(&C.frame.body,origin,px,py))continue;
            unsigned mask=cyc_render_line_mask(py);
            if(!(mask&16) || (!mm4_coop_independent() && px>=origin && px<origin+8 && !(mask&4)))continue;
            int at=py*width+px;if(claimed[at])continue;claimed[at]=1;
            if(!(p->attr&32)||!opaque[at])out[at]=cyc_render_color_value(p->colors[value]);
        }
    }
}
void mm4_coop_power(void) { memset(&C,0,sizeof C);memset(&A,0,sizeof A);memset(&W,0,sizeof W);memset(&E,0,sizeof E);E.restore=-1;C.pending_pause=255;s_scope=s_personal_bhv=s_collision=s_death_wait=0;addresses();mm4_menu_reset(); }
static const char *const HOOKS[]={"mm4.coop.controller","mm4.coop.after-controller","mm4.coop.personal-end","mm4.coop.world-begin","mm4.coop.pause","mm4.coop.after-scroll","mm4.coop.before-scroll","mm4.coop.dying","mm4.coop.death-music","mm4.coop.after-world","mm4.coop.retry","mm4.coop.arrival","mm4.coop.death-wait","mm4.coop.pit","mm4.coop.frame-end","mm4.coop.before-director","mm4.coop.before-vertical","mm4.coop.after-vertical","mm4.coop.scene-draw"};
void mm4_coop_set(int enabled,int roster) {
    int on=enabled!=0,roles=roster!=0;
    /* The catalog reapplies every feature after any option changes. An
     * unrelated presentation setting must retain both live player contexts. */
    if(on!=s_on || roles!=s_roster) {
        finish_wait();if(on)mm4_proto_cancel_swap();
        C.initialized=0;C.current=C.anchor=0;C.pending_pause=255;
        C.menu=C.menu_ready=C.menu_busy=0;
    }
    s_on=on;s_roster=roles;
    nes_mod_set_function_hook_enabled("mm4.coop.menu-idle",s_on);
    nes_mod_set_function_hook_enabled("mm4.coop.marine-out",s_on);
    nes_mod_set_function_hook_enabled("mm4.coop.bright-before",s_on);
    nes_mod_set_function_hook_enabled("mm4.coop.bright-after",s_on);
    nes_mod_set_function_hook_enabled("mm4.coop.balloon-floor",s_on);
    nes_mod_set_function_hook_enabled("mm4.coop.rain-frozen",s_on);
    for(int i=0;i<sizeof HOOKS/sizeof *HOOKS;i++)nes_mod_set_function_hook_enabled(HOOKS[i],s_on);
    source_hooks();
    effect_hooks();
    nes_mod_set_function_hook_enabled("mm4.coop.target-begin",s_on);nes_mod_set_function_hook_enabled("mm4.coop.target-end",s_on);
    nes_mod_set_local_only("MM4 personal co-op (unqualified netplay)",s_on);mm4_view_coop(s_on);
    mm4_coop_view_set(s_view);
}
static int get(uint8_t *out,int cap) {if(cap<sizeof C)return -1;memcpy(C.magic,"M4D1",4);C.enabled=(uint8_t)s_on;C.roster=(uint8_t)s_roster;C.wait_mode=(uint8_t)s_wait_both;memcpy(out,&C,sizeof C);return sizeof C;}
static int valid(const uint8_t *in,int len) {
    if(!len)return !s_on;if(!in || len!=sizeof C)return 0;
    static Coop p;memcpy(&p,in,sizeof p);
    if(memcmp(p.magic,"M4D1",4)||p.enabled!=s_on||p.roster!=s_roster||p.wait_mode!=s_wait_both||p.waiting>3||((!p.enabled||!p.wait_mode)&&p.waiting)||p.initialized>1||p.anchor>1||p.current>1||p.menu>1||p.pauser>1||p.menu_ready>1||p.menu_busy>1||p.menu_user>1||(!p.menu && (p.menu_ready||p.menu_busy))||p.scene_active>2||p.retry>1||p.player[0].dead>2||p.player[1].dead>2||p.waited[0]>1||p.waited[1]>1||p.withdrawn>2||p.checkpoint>2||p.revive_pending>3)return 0;
    if(p.source_pending>3 || p.source_page>=48 || p.source_offset>=48 || p.source_nt ||
       (p.source_pending && (p.source_regs.pc!=0xC96E || p.menu || p.scene_active)))return 0;
    if(p.initialized && (p.withdrawn&(1u<<p.anchor)))return 0;
    for(int seat=0;seat<2;seat++) {
        if(p.menu_pending[seat]&~143)return 0;
        if(p.select_hold[seat]>90||p.select_armed[seat]>1||p.teleport[seat]>TELE_IN||p.arrival_floor[seat]>=240||(!p.teleport[seat])!=!((p.withdrawn|p.waiting)&(1u<<seat)))return 0;
        if((p.waiting&(1u<<seat)) && (p.teleport[seat]!=TELE_HIDDEN || (p.wait_direction[seat]!=4 && p.wait_direction[seat]!=8)))return 0;
    }
    const Picture *f[2]={&p.build,&p.frame};
    for(int i=0;i<2;i++) {
        if(f[i]->valid>1||f[i]->seat>1||f[i]->count>PART_CAP||f[i]->body.pose>=CYC_PROTO_POSE_COUNT||f[i]->body.active>1||f[i]->body.right>1||f[i]->body.flipped>1||f[i]->body.behind>1)return 0;
        for(int j=0;j<f[i]->count;j++) {
            const Part *part=&f[i]->parts[j];if(part->owner>4 && part->owner!=255)return 0;
            for(int k=0;k<4;k++)if(part->colors[k]>63)return 0;
            for(int k=0;k<64;k++)if(part->pixels[k]>3)return 0;
        }
    }
    return 1;
}
static int set(const uint8_t *in,int len) {if(!valid(in,len))return 0;if(len)memcpy(&C,in,sizeof C);else mm4_coop_power();source_hooks();effect_hooks();return 1;}
static void effect_hooks(void){nes_mod_set_function_hook_enabled("mm4.coop.after-controller",s_on || R(0x13C) || R(0x13D));}
static int effects_get(uint8_t *out,int cap){if(cap<sizeof E)return -1;memcpy(E.magic,"M4EF",4);memcpy(out,&E,sizeof E);return sizeof E;}
static int effects_valid(const uint8_t *in,int n){Effects p;if(!n)return !s_on;if(!in||n!=sizeof p)return 0;memcpy(&p,in,sizeof p);return !memcmp(p.magic,"M4EF",4)&&p.ticked<=1&&!p.reserved[0]&&!p.reserved[1]&&!p.reserved[2]&&p.restore>=-1&&p.restore<=65535;}
static int effects_set(const uint8_t *in,int n){if(!effects_valid(in,n))return 0;if(n)memcpy(&E,in,sizeof E);else{memset(&E,0,sizeof E);E.restore=-1;}effect_hooks();return 1;}
static int target_get(uint8_t *out,int cap){if(cap<sizeof A)return -1;memcpy(A.magic,"M4AT",4);memcpy(out,&A,sizeof A);return sizeof A;}
static int target_valid(const uint8_t *in,int n){Target p;if(!n)return !A.active;if(!in||n!=sizeof p)return 0;memcpy(&p,in,sizeof p);return !memcmp(p.magic,"M4AT",4)&&p.active<=2&&p.owner<2&&p.seat<2;}
static int target_set(const uint8_t *in,int n){if(!target_valid(in,n))return 0;if(n)memcpy(&A,in,sizeof A);else memset(&A,0,sizeof A);return 1;}
static int warp_get(uint8_t *out,int cap){if(cap<sizeof W)return -1;memcpy(W.magic,"M4WT",4);memcpy(out,&W,sizeof W);return sizeof W;}
static int warp_valid(const uint8_t *in,int n){Warp p;if(!n)return !W.active;if(!in||n!=sizeof p)return 0;memcpy(&p,in,sizeof p);return !memcmp(p.magic,"M4WT",4)&&p.active<=2&&!p.reserved[0]&&!p.reserved[1];}
static int warp_set(const uint8_t *in,int n){if(!warp_valid(in,n))return 0;if(n)memcpy(&W,in,sizeof W);else memset(&W,0,sizeof W);return 1;}
void mm4_coop_register(void) {
    nes_mod_register_function_entry_plugin("mm4.coop.marine-out",0x8530,marine_out);
    nes_mod_register_function_entry_plugin("mm4.coop.bright-before",0x9195,bright_tick_before);
    nes_mod_register_function_entry_plugin("mm4.coop.bright-after",0x91A6,bright_tick_after);
    nes_mod_register_function_entry_plugin("mm4.coop.balloon-floor",0xF7D8,mm4_helpers_floor);
    nes_mod_register_function_entry_plugin("mm4.coop.rain-frozen",0x80EA,rain_frozen);
    nes_mod_register_function_entry_plugin("mm4.coop.source-start",0xC5A6,source_start);
    nes_mod_register_function_entry_plugin("mm4.coop.source-loaded",0xC5C4,source_loaded);
    mm4_menu_register();nes_mod_register_function_entry_plugin("mm4.coop.menu-idle",0x96D7,menu_idle);
    addresses();nes_mod_register_function_entry_plugin(HOOKS[0],0x8000,controller);nes_mod_register_function_entry_plugin(HOOKS[1],0xC74D,after_controller);
    nes_mod_register_function_entry_plugin(HOOKS[2],0x8052,personal_end);nes_mod_register_function_entry_plugin(HOOKS[3],0x8003,world_begin);
    nes_mod_register_function_entry_plugin(HOOKS[4],0x9600,pause_entry);
    nes_mod_register_function_entry_plugin(HOOKS[5],0xCBFC,after_horizontal_scroll);
    nes_mod_register_function_entry_plugin(HOOKS[6],0xCB68,before_horizontal_scroll);
    nes_mod_register_function_entry_plugin(HOOKS[7],0x8243,dying);nes_mod_register_function_entry_plugin(HOOKS[8],0xF6BC,death_music);
    nes_mod_register_function_entry_plugin(HOOKS[9],0xC75A,after_world);nes_mod_register_function_entry_plugin(HOOKS[10],0xC7F6,retry);
    nes_mod_register_function_entry_plugin(HOOKS[11],0xC65B,arrival);nes_mod_register_function_entry_plugin(HOOKS[12],0xC7F3,death_wait_end);
    nes_mod_register_function_entry_plugin(HOOKS[13],0xDC1F,dying);nes_mod_register_function_entry_plugin(HOOKS[14],0xC7CB,frame_end);
    nes_mod_register_function_entry_plugin(HOOKS[15],0xC96E,before_director);
    nes_mod_register_function_entry_plugin(HOOKS[16],0xCDD5,before_vertical_scroll);
    nes_mod_register_function_entry_plugin(HOOKS[17],0xCE96,after_vertical_scroll);
    nes_mod_register_function_entry_plugin(HOOKS[18],0xDB3A,scene_draw);
    nes_mod_register_savestate_hook("mm4.coop",get,set);nes_mod_register_savestate_validator("mm4.coop",valid);
    nes_mod_register_function_entry_plugin("mm4.coop.target-begin",0x804F,target_begin);
    nes_mod_register_function_entry_plugin("mm4.coop.target-end",0x8097,target_end);
    nes_mod_register_savestate_hook("mm4.coop-target",target_get,target_set);nes_mod_register_savestate_validator("mm4.coop-target",target_valid);
    nes_mod_register_savestate_hook("mm4.coop-warp",warp_get,warp_set);nes_mod_register_savestate_validator("mm4.coop-warp",warp_valid);
    nes_mod_register_savestate_hook("mm4.coop-effects",effects_get,effects_set);nes_mod_register_savestate_validator("mm4.coop-effects",effects_valid);
}
int mm4_coop_json(char *out,int cap) {
    return snprintf(out,cap,"\"coop\":{\"on\":%d,\"ready\":%u,\"anchor\":%u,\"menu\":%u,\"pauser\":%u,\"world\":%llu,\"updates\":[%llu,%llu],\"failed\":%llu,\"dead\":[%u,%u],\"retry\":%u,\"lives\":%u,\"inventory\":[%u,%u,%u,%d,%d,%d,%d,%d,%d,%u],\"source\":[%u,%u,%u,%llu,%llu],\"wait\":[%u,%u],\"transport\":[%u,%u,%u,%u,%u],\"withdrawals\":[%llu,%llu],\"rejoins\":[%llu,%llu],\"players\":[[%d,%d,%d,%d,%d,%d],[%d,%d,%d,%d,%d,%d]]}",
        s_on,C.initialized,C.anchor,C.menu,C.pauser,(unsigned long long)C.world_updates,(unsigned long long)C.updates[0],(unsigned long long)C.updates[1],(unsigned long long)C.failures,C.player[0].dead,C.player[1].dead,C.retry,R(0xA1),
        C.menu_ready,C.menu_busy,C.current,mm4_coop_read(0,0x138),mm4_coop_read(1,0x138),mm4_coop_read(0,0xA0),mm4_coop_read(1,0xA0),mm4_coop_read(0,0xB0)&31,mm4_coop_read(1,0xB0)&31,R(0xA2),C.source_pending,C.source_page,C.source_nt,(unsigned long long)C.source_handoffs[0],(unsigned long long)C.source_handoffs[1],s_wait_both,C.waiting,C.withdrawn,C.teleport[0],C.teleport[1],C.select_hold[0],C.select_hold[1],(unsigned long long)C.withdrawals[0],(unsigned long long)C.withdrawals[1],(unsigned long long)C.rejoins[0],(unsigned long long)C.rejoins[1],
        personal(&C.player[0],0x330),personal(&C.player[0],0x378),personal(&C.player[0],0xB0)&31,personal(&C.player[0],0x32),personal(&C.player[0],0x30),personal(&C.player[0],0x348),
        personal(&C.player[1],0x330),personal(&C.player[1],0x378),personal(&C.player[1],0xB0)&31,personal(&C.player[1],0x32),personal(&C.player[1],0x30),personal(&C.player[1],0x348));
}
