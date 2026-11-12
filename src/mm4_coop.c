/* Personal native execution contexts; the real world engine runs once. */
#include "mm4_coop.h"
#include "mm4_world.h"
#include "mm4_proto.h"
#include "mm4_wide.h"

#include "cyc_mod.h"
#include "cyc_ring.h"
#include "cyc_render.h"
#include "cyc_video.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum { PERSONAL_CAP=384, PART_CAP=96 };
typedef struct { uint8_t ram[PERSONAL_CAP],previous,dead; } Player;
typedef struct { int16_t x,y;uint8_t owner,attr,colors[4],pixels[64]; } Part;
typedef struct { uint8_t valid,seat;uint16_t count,camera;Mm4ProtoBody body;Part parts[PART_CAP]; } Picture;
typedef struct {
    char magic[4];uint8_t enabled,initialized,stage,anchor,current,menu,pauser,pending_pause,roster,scene_active;
    uint8_t raw[2],raw_previous[2],fresh[2];
    Player player[2];Picture build,frame;
    uint64_t updates[2],world_updates,failures;
    uint8_t retry,retry_ammo[2][13],waited[2];
} Coop;
static Coop C;
static int s_on,s_roster,s_scope,s_personal_bhv,s_collision,s_death_wait,s_view;
static uint16_t s_addr[PERSONAL_CAP],s_index[2048];
static int s_count;
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
int mm4_coop_live(int seat) {return s_on && C.initialized && seat>=0 && seat<2 && !C.player[seat].dead;}
int mm4_coop_view_mode(void) {return s_on?s_view:0;}
int mm4_coop_independent(void) {return s_on && s_view!=0;}
int mm4_coop_scene_locked(void) {return C.scene_active || R(0x19) || R(0x1A) || R(0x30)>=8;}
void mm4_coop_view_set(int mode) {
    if(mode<0 || mode>3)return;
    s_view=mode;mm4_view_coop(s_on);mm4_wide_views_changed();
    cyc_video_override_width(mm4_coop_independent()?(s_view==1?512:256):0);
}
int mm4_coop_view_rects(int left[2]) {
    if(!s_on || !C.initialized || R(0x22)!=C.stage)return 0;
    int camera=(R(0xF9)<<8)|R(0xFC),start=R(0xF9)-R(0x2E);
    for(int seat=0;seat<2;seat++) {
        int who=C.player[seat].dead?C.anchor:seat;left[seat]=camera;
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
    return C.initialized && seat!=C.current?personal(&C.player[seat],a):R(a);
}
static int world_field(int a) {
    if(a>=0x300 && a<0x5D0)return (a-0x300)%24>=5;
    return (a>=0x100 && a<0x110) || (a>=0x600 && a<0x610) || (a>=0x620 && a<0x630) ||
        (a>=0x670 && a<0x6c0) || (a>=0x780 && a<0x800) || a==0x19 || a==0x1A || a==0xBF;
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
    int state=personal(&C.player[seat],0x30);
    if(state>6 || state==5 || R(0x30)>=8 || C.player[seat].dead)return 1;
    uint8_t before[2048],after[2048];snapshot_ram(before);
    if(!cyc_mod_isolate_begin())return 0;
    int owner=C.current;C.current=(uint8_t)seat;project(&C.player[seat]);center_camera();
    P(0x14,C.raw[seat]&~C.player[seat].previous&~0x30);P(0x16,C.raw[seat]&~0x30);P(0x15,0);P(0x17,0);
    s_scope=1;int enabled=hooks(1),ok=enabled;CycModRegs r;cyc_mod_regs(&r);
    P(0xf5,0x3c);P(0xf6,0x3d);if(ok)ok=cyc_mod_call(0xff37,&r);
    if(ok)ok=mm4_controls_art(R(0xa0));
    if(ok)mm4_controls_dispatch(0x80da);
    if(ok)ok=cyc_mod_call(0x8000,&r);
    if(ok) {P(0xf5,0x3a);P(0xf6,0x3b);ok=cyc_mod_call(0xff37,&r);}
    s_personal_bhv=1;if(ok)ok=cyc_mod_call(0x8003,&r);s_personal_bhv=0;
    if(ok){capture(&C.player[seat]);snapshot_ram(after);C.updates[seat]++;C.player[seat].previous=C.raw[seat];}
    if(enabled)hooks(0);s_scope=0;cyc_mod_isolate_end();C.current=(uint8_t)owner;
    if(ok)merge_world(before,after);return ok;
}
static void initialize(void) {
    C.anchor=C.current=0;C.stage=R(0x22);C.initialized=1;C.menu=0;C.pending_pause=255;
    capture(&C.player[0]);C.player[1]=C.player[0];
    C.player[0].dead=C.player[1].dead=0;
    if(C.retry){for(int seat=0;seat<2;seat++)for(int i=0;i<13;i++)set_personal(&C.player[seat],0xB1+i,C.retry_ammo[seat][i]);project(&C.player[0]);C.retry=0;}
    int x=(R(0x348)<<8)|R(0x330);x=x>=32?x-16:x+16;
    set_personal(&C.player[1],0x330,x);set_personal(&C.player[1],0x348,x>>8);
    for(int seat=0;seat<2;seat++)C.player[seat].previous=C.raw[seat];
}
static int controller(uint16_t address) {
    (void)address;if(!s_on || cyc_mod_isolated())return 0;
    C.waited[0]=C.waited[1]=0;
    if(C.menu && R(0x22)<16) {
        capture(&C.player[C.pauser]);C.current=C.anchor;project(&C.player[C.anchor]);C.menu=0;P(0x18,255);
    }
    if(C.initialized && R(0x22)<16 && R(0x22)!=C.stage)C.initialized=0;
    if(!C.initialized) {
        if(R(0x22)>=16 || R(0x300)!=1 || R(0x30)!=0 || !(R(0xB0)&31))return 0;
        initialize();
    }
    if(C.menu || R(0x22)>=16)return 0;
    C.current=C.anchor;capture(&C.player[C.anchor]);
    test_pokes();
    if(!private_step(C.anchor^1))C.failures++;
    C.current=C.anchor;P(0x14,C.raw[C.anchor]&~C.player[C.anchor].previous&~0x30);P(0x16,C.raw[C.anchor]&~0x30);
    P(0x15,0);P(0x17,0);return 0;
}
static int after_controller(uint16_t address) {
    (void)address;if(s_on && C.initialized && !cyc_mod_isolated() && !C.menu) {capture(&C.player[C.anchor]);C.updates[C.anchor]++;C.player[C.anchor].previous=C.raw[C.anchor];}return 0;
}
static int other_collision(int seat,int actor) {
    if(C.player[seat].dead || !(personal(&C.player[seat],0xB0)&31) || !R(0x300+actor) || R(0x30)>=8)return 1;
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
    /* Finish the native loop at its common terrain-hazard check, after slot4.
     * Returning from here would incorrectly omit the player's spike checks. */
    if(s_on && s_scope && s_personal_bhv && R(0xEB)>=4)P(0xEB,23);
    if(s_on && C.initialized && !C.menu && !cyc_mod_isolated() && R(0xEB)>=5 && R(0xEB)<24)
        if(!other_collision(C.anchor^1,R(0xEB)))C.failures++;
    return 0;
}
static int world_begin(uint16_t address) { (void)address;if(s_on && C.initialized && !cyc_mod_isolated())C.world_updates++;return 0; }
static int dying(uint16_t address){
    (void)address;if(!s_on || !C.initialized || (cyc_mod_isolated() && !s_scope))return 0;
    C.player[C.current].dead=1;
    for(int slot=1;slot<5;slot++)P(0x300+slot,0);
    return 0;
}
static int death_music(uint16_t address){
    (void)address;if(!s_on || !C.initialized || (cyc_mod_isolated() && !s_scope) || C.player[C.current^1].dead)return 0;
    CycModRegs r;cyc_mod_regs(&r);int caller=R(0x100|((r.s+1)&255))|(R(0x100|((r.s+2)&255))<<8);
    return r.a==0xF0 && (caller==0x824B || caller==0xDC29);
}
static int after_world(uint16_t address){
    (void)address;if(!s_on || !C.initialized || C.menu || cyc_mod_isolated())return 0;
    int other=C.anchor^1;
    if(!C.player[C.anchor].dead || (C.player[other].dead && !(C.player[other].dead==2 && C.player[C.anchor].dead!=2)))return 0;
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
    (void)address;if(s_on && !cyc_mod_isolated()){C.initialized=C.menu=C.scene_active=0;C.current=C.anchor=0;C.pending_pause=255;}return 0;
}
static int frame_end(uint16_t address){
    (void)address;if(!s_on || !C.initialized || C.menu || cyc_mod_isolated())return 0;
    int other=C.anchor^1;
    /* A native collision or DB72 draw may first declare death after the
     * personal controller ran. Count that frame once, like the native anchor. */
    if(C.player[other].dead==1 && !C.waited[other] && !dead_step(other))C.failures++;
    return after_world(address);
}
static int before_horizontal_scroll(uint16_t address) {
    (void)address;if(s_on && C.initialized && !C.menu && !cyc_mod_isolated())C.scene_active=1;return 0;
}
static int after_horizontal_scroll(uint16_t address) {
    (void)address;
    if(!s_on || !C.initialized || C.menu || cyc_mod_isolated() || !C.scene_active || R(0x22)>=16)return 0;
    C.scene_active=0;
    /* The original 128-frame door script moved the real player and camera.
     * Bring the partner into that completed destination, retaining inventory. */
    int seat=C.anchor^1;Player *p=&C.player[seat];
    if(p->dead || !(personal(p,0xB0)&31))return 0;
    C.current=C.anchor;capture(&C.player[C.anchor]);
    for(int base=0x300;base<=0x5B8;base+=24)set_personal(p,base,R(base));
    for(int a=0x30;a<=0x3F;a++)if(a!=0x3C)set_personal(p,a,R(a));
    int x=((R(0x348)<<8)|R(0x330))-16;if(x<0)x=0;
    set_personal(p,0x330,x);set_personal(p,0x348,x>>8);
    set_personal(p,0x32,0);set_personal(p,0x33,0);set_personal(p,0x35,0);set_personal(p,0x36,0);
    set_personal(p,0x3C,60);set_personal(p,0x5B8,60);
    for(int slot=1;slot<5;slot++)set_personal(p,0x300+slot,0);
    p->previous=C.raw[seat];return 0;
}
static int pause_entry(uint16_t address) {
    (void)address;
    if(!s_on || !C.initialized || C.menu || C.pending_pause>1 || cyc_mod_isolated())return 0;
    int seat=C.pending_pause;C.pending_pause=255;
    if(R(0x22)>=16 || (personal(&C.player[seat],0x30)>4) || !(personal(&C.player[seat],0xB0)&31))return 0;
    capture(&C.player[C.anchor]);C.current=C.pauser=(uint8_t)seat;project(&C.player[seat]);
    P(0x14,C.fresh[seat]);P(0x16,C.raw[seat]);C.menu=1;return 0;
}
void mm4_coop_input(uint8_t buttons[2]) {
    if(!s_on)return;
    for(int seat=0;seat<2;seat++){C.raw[seat]=buttons[seat];C.fresh[seat]=(uint8_t)(buttons[seat]&~C.raw_previous[seat]);C.raw_previous[seat]=buttons[seat];}
    if(C.menu)buttons[0]=C.raw[C.pauser];
    else if(!C.initialized || R(0x22)>=16 || (C.player[0].dead && C.player[1].dead)){
        buttons[0]=(uint8_t)(C.raw[0]|C.raw[1]);if((buttons[0]&3)==3)buttons[0]&=(uint8_t)~3;if((buttons[0]&12)==12)buttons[0]&=(uint8_t)~12;
    }else {
        C.pending_pause=255;
        buttons[0]=C.raw[C.anchor];
        for(int seat=0;seat<2;seat++)if(C.fresh[seat]&16){C.pending_pause=(uint8_t)seat;buttons[0]|=16;break;}
    }
    buttons[1]=0; /* Original pad-2 Select/B are native debug controls. */
}
void mm4_coop_end(void) {
    if(!s_on)return;
    /* Native inventory and stage select both use screen16. Only the
     * explicitly owned personal menu retains its live stage contexts. */
    if(C.initialized && !C.menu && R(0x22)>=16){C.initialized=0;C.scene_active=0;C.pending_pause=255;}
    if(C.initialized && !C.menu && R(0x22)<16){C.current=C.anchor;capture(&C.player[C.anchor]);}
    /* Consume personal button edges only when that native controller runs.
     * Advancing previous here loses a new press on an output-only lag frame. */
}
void mm4_coop_build(void) {
    memset(&C.build,0,sizeof C.build);
    if(!s_on || !C.initialized || C.menu || R(0x22)>=16)return;
    int seat=C.anchor^1,owner=C.current;
    if(C.player[seat].dead)return;
    uint8_t before[2048],after[2048];snapshot_ram(before);
    if(!cyc_mod_isolate_begin())return;
    C.build.camera=(uint16_t)((R(0xF9)<<8)|R(0xFC));C.current=(uint8_t)seat;project(&C.player[seat]);int camera=center_camera();
    s_scope=1;int enabled=hooks(1);
    int art=mm4_controls_art(R(0xa0));
    P(0x97,4);for(int i=0;i<256;i+=4)P(0x200+i,248);
    uint8_t part_owner[64];memset(part_owner,255,sizeof part_owner);
    int ok=art && enabled,body=0;
    for(int slot=0;slot<5 && ok;slot++) {
        if(!R(0x300+slot))continue;
        int begin=R(0x97);CycModRegs r;cyc_mod_regs(&r);r.x=(uint8_t)slot;P(0x96,slot);
        ok=cyc_mod_call(0xDB72,&r);int end=R(0x97);if(!end)end=256;
        for(int offset=begin;offset<end;offset+=4)part_owner[offset/4]=(uint8_t)slot;
        if(!slot && end>begin){mm4_proto_body(&C.build.body);C.build.body.x+=(int16_t)(camera-C.build.camera);body=1;}
        if(end==256)break;
    }
    int end=R(0x97);if(!end)end=256;
    for(int offset=4;ok && offset<end && C.build.count<PART_CAP;offset+=4) {
        int top=R(0x200+offset)+1;if(top>=240)continue;
        Part *p=&C.build.parts[C.build.count++];p->x=(int16_t)(camera+R(0x203+offset));p->y=(int16_t)top;
        p->owner=part_owner[offset/4];p->attr=R(0x202+offset);
        int tile=R(0x201+offset),table=cyc_render_line_sprite_table(top);
        for(int i=0;i<4;i++)p->colors[i]=R(0x610+(p->attr&3)*4+i)&63;
        for(int y=0;y<8;y++) {
            int row=(p->attr&128)?7-y:y,a=table+tile*16+row;
            int low=mm4_controls_pattern(R(0xa0),a),high=mm4_controls_pattern(R(0xa0),a+8);
            for(int x=0;x<8;x++){int bit=(p->attr&64)?x:7-x;p->pixels[y*8+x]=(uint8_t)(((low>>bit)&1)|(((high>>bit)&1)<<1));}
        }
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
        if(!p->owner && C.frame.body.active) {if(!painted){mm4_view_draw_character(out,width,origin,&C.frame.body);painted=1;}continue;}
        for(int y=0;y<8;y++)for(int x=0;x<8;x++) {
            int value=p->pixels[y*8+x],px=p->x-C.frame.camera+origin+x,py=p->y+y;
            if(!value || px<0 || px>=width || py<0 || py>=240)continue;
            int at=py*width+px;if(claimed[at])continue;claimed[at]=1;
            if(!(p->attr&32)||!opaque[at])out[at]=cyc_render_color_value(p->colors[value]);
        }
    }
}
void mm4_coop_power(void) { memset(&C,0,sizeof C);C.pending_pause=255;s_scope=s_personal_bhv=s_collision=s_death_wait=0;addresses(); }
static const char *const HOOKS[]={"mm4.coop.controller","mm4.coop.after-controller","mm4.coop.personal-end","mm4.coop.world-begin","mm4.coop.pause","mm4.coop.after-scroll","mm4.coop.before-scroll","mm4.coop.dying","mm4.coop.death-music","mm4.coop.after-world","mm4.coop.retry","mm4.coop.arrival","mm4.coop.death-wait","mm4.coop.pit","mm4.coop.frame-end"};
void mm4_coop_set(int enabled,int roster) {
    if(enabled)mm4_proto_cancel_swap();
    s_on=enabled!=0;s_roster=roster!=0;C.initialized=0;C.current=C.anchor=0;C.pending_pause=255;
    for(int i=0;i<sizeof HOOKS/sizeof *HOOKS;i++)nes_mod_set_function_hook_enabled(HOOKS[i],s_on);
    nes_mod_set_local_only("MM4 personal co-op (unqualified netplay)",s_on);mm4_view_coop(s_on);
    mm4_coop_view_set(s_view);
}
static int get(uint8_t *out,int cap) {if(cap<sizeof C)return -1;memcpy(C.magic,"M4C6",4);C.enabled=(uint8_t)s_on;C.roster=(uint8_t)s_roster;memcpy(out,&C,sizeof C);return sizeof C;}
static int valid(const uint8_t *in,int len) {
    if(!len)return !s_on;if(!in || len!=sizeof C)return 0;
    static Coop p;memcpy(&p,in,sizeof p);
    if(memcmp(p.magic,"M4C6",4)||p.enabled!=s_on||p.roster!=s_roster||p.initialized>1||p.anchor>1||p.current>1||p.menu>1||p.pauser>1||p.scene_active>1||p.retry>1||p.player[0].dead>2||p.player[1].dead>2||p.waited[0]>1||p.waited[1]>1)return 0;
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
static int set(const uint8_t *in,int len) {if(!valid(in,len))return 0;if(len)memcpy(&C,in,sizeof C);else mm4_coop_power();return 1;}
void mm4_coop_register(void) {
    addresses();nes_mod_register_function_entry_plugin(HOOKS[0],0x8000,controller);nes_mod_register_function_entry_plugin(HOOKS[1],0xC74D,after_controller);
    nes_mod_register_function_entry_plugin(HOOKS[2],0x8052,personal_end);nes_mod_register_function_entry_plugin(HOOKS[3],0x8003,world_begin);
    nes_mod_register_function_entry_plugin(HOOKS[4],0x9600,pause_entry);
    nes_mod_register_function_entry_plugin(HOOKS[5],0xCBFC,after_horizontal_scroll);
    nes_mod_register_function_entry_plugin(HOOKS[6],0xCB68,before_horizontal_scroll);
    nes_mod_register_function_entry_plugin(HOOKS[7],0x8243,dying);nes_mod_register_function_entry_plugin(HOOKS[8],0xF6BC,death_music);
    nes_mod_register_function_entry_plugin(HOOKS[9],0xC75A,after_world);nes_mod_register_function_entry_plugin(HOOKS[10],0xC7F6,retry);
    nes_mod_register_function_entry_plugin(HOOKS[11],0xC65B,arrival);nes_mod_register_function_entry_plugin(HOOKS[12],0xC7F3,death_wait_end);
    nes_mod_register_function_entry_plugin(HOOKS[13],0xDC1F,dying);nes_mod_register_function_entry_plugin(HOOKS[14],0xC7CB,frame_end);
    nes_mod_register_savestate_hook("mm4.coop",get,set);nes_mod_register_savestate_validator("mm4.coop",valid);
}
int mm4_coop_json(char *out,int cap) {
    return snprintf(out,cap,"\"coop\":{\"on\":%d,\"ready\":%u,\"anchor\":%u,\"menu\":%u,\"pauser\":%u,\"world\":%llu,\"updates\":[%llu,%llu],\"failed\":%llu,\"dead\":[%u,%u],\"retry\":%u,\"lives\":%u,\"players\":[[%d,%d,%d,%d,%d,%d],[%d,%d,%d,%d,%d,%d]]}",
        s_on,C.initialized,C.anchor,C.menu,C.pauser,(unsigned long long)C.world_updates,(unsigned long long)C.updates[0],(unsigned long long)C.updates[1],(unsigned long long)C.failures,C.player[0].dead,C.player[1].dead,C.retry,R(0xA1),
        personal(&C.player[0],0x330),personal(&C.player[0],0x378),personal(&C.player[0],0xB0)&31,personal(&C.player[0],0x32),personal(&C.player[0],0x30),personal(&C.player[0],0x348),
        personal(&C.player[1],0x330),personal(&C.player[1],0x378),personal(&C.player[1],0xB0)&31,personal(&C.player[1],0x32),personal(&C.player[1],0x30),personal(&C.player[1],0x348));
}
