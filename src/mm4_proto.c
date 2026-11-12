/* Native MM4 movement, charge, slide, recoil and death scripts remain in charge. */
#include "mm4_proto.h"
#include "mm4_coop.h"
#include "mm4_proto_sheet.h"
#include "cyc_mod.h"
#include "cyc_ring.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
static CycProtoAtlas s_atlas;
static int s_loaded,s_on,s_mode=1,s_start=1;
static uint8_t *s_cart;
static size_t s_size;
typedef struct {
    char magic[4];
    uint8_t enabled,mode,selected,teleport,saved_stun,saved_invincible,reflected[3];
    uint64_t switches,limited,blocks,reflections,hits;
} ProtoState;
static ProtoState s_players[2];
#define S (s_players[mm4_coop_seat()])
static uint8_t prg(int bank,int address) {
    size_t at=(size_t)bank*0x2000+(address&0x1fff);
    return at<s_size?s_cart[at]:0xff;
}
static int drawn_frame(void) {
    if(!s_cart)return -1;
    int bank=prg(0x3e,0xdffa+R(0x300)),anim=R(0x558);
    if(bank>=64 || !anim)return -1;
    int pointer=prg(bank,0xbe00+anim)|(prg(bank,0xbf00+anim)<<8);
    if(pointer<0x8000 || pointer>=0xbffe)return -1;
    return prg(bank-(pointer<0xa000),pointer+2+(R(0x540)&127));
}
int mm4_proto_enabled(void) {return s_on;}
int mm4_proto_selected(void) {int character=mm4_coop_character();return s_on && (character>=0?character:S.selected);}
int mm4_proto_swapping(void) {return s_on && !mm4_coop_enabled() && S.teleport;}
int mm4_proto_active(void) {return mm4_proto_selected() || mm4_proto_swapping();}
const CycProtoAtlas *mm4_proto_atlas(void) {return s_loaded?&s_atlas:NULL;}
static int safe(void) {
    int state=R(0x30);
    return R(0x22)<16 && R(0x300)==1 && (R(0xb0)&31) &&
        (state==0 || state==1 || state==3) && !R(0x390) &&
        !(R(0x19)|R(0x1a)|R(0xad)|R(0x133)|R(0x13a)|R(0x13c)|R(0x13d));
}
static void sound(int id) {
    /* MM4's native queue, shared with cartridge SFX; no host audio replacement. */
    int q=R(0xda)&7;
    if(R(0xdc+q)==0x88) {P(0xdc+q,id);P(0xda,(q+1)&7);}
}
static int shield_up(void) {
    return mm4_proto_selected() && s_mode && !S.teleport && R(0x30)==1 &&
        !R(0x32) && !(R(0x19)|R(0x1a)|R(0xad)|R(0x133)) && !R(0x390);
}
int mm4_proto_dispatch(void) {
    if(!s_on || mm4_coop_enabled() || cyc_mod_isolated())return 0;
    if(!S.teleport && (R(0x14)&0x20) && safe()) {
        S.teleport=1;S.saved_stun=R(0x5b8);S.switches++;
        S.saved_invincible=R(0x3c);
        P(0x35,0);P(0x36,0);sound(0x33);
    }
    if(!S.teleport)return 0;
    P(0x5b8,0x80);
    P(0x3c,2);
    return 1;
}
static int shot(uint16_t address) {
    (void)address;
    CycModRegs r;cyc_mod_regs(&r);
    if(r.y>=1 && r.y<=3)S.reflected[r.y-1]=0;
    if(!mm4_proto_selected() || !s_mode || (cyc_mod_isolated() && !mm4_coop_native_scope()) || R(0x11)!=0x18)return 0;
    int count=0;
    for(int slot=1;slot<=3;slot++)count+=R(0x300+slot)==2 && R(0x558+slot)==0x18 && !S.reflected[slot-1];
    if(count<2)return 0;
    S.limited++;return 1;
}
static int muzzle(uint16_t address) {
    (void)address;
    if(!mm4_proto_selected() || (cyc_mod_isolated() && !mm4_coop_native_scope()))return 0;
    CycModRegs r;cyc_mod_regs(&r);
    int state=R(0x30),offset=state==3?-4:3;
    if(state==0 && R(0x558)-R(0x33)==4 && (R(0x540)&3)==1)offset--;
    int y=R(0x378)+offset;
    if(r.y>=1 && r.y<=3 && y>=0 && y<240)P(0x378+r.y,y);
    return 0;
}
static int damage(uint16_t address) {
    (void)address;
    if(!mm4_proto_selected() || !s_mode || S.teleport || (cyc_mod_isolated() && !mm4_coop_native_scope()))return 0;
    CycModRegs r;cyc_mod_regs(&r);
    int amount=R(0x8413+r.y);
    /* SEC has run and A is the masked unit count, not bit-7 ownership.
     * Subtract one extra dose; native SBC decides zero/underflow and death. */
    r.a=(uint8_t)(r.a>amount?r.a-amount:0);
    cyc_mod_set_regs(&r);return 0;
}
static int reflected(int slot) {return slot>=1 && slot<=3 && S.reflected[slot-1] && R(0x300+slot)==2;}
static int shield(uint16_t address) {
    (void)address;
    if(!shield_up() || cyc_mod_isolated())return 0;
    CycModRegs caller;cyc_mod_regs(&caller);int enemy=caller.x;
    if(enemy<8 || enemy>=24)return 0;
    int type=R(0x300+enemy);
    /* Native straight bullets from the hiding shooter and wall turret. */
    if(type!=0x51 && type!=0xb1)return 0;
    int x=(R(0x348)<<8)|R(0x330),ex=(R(0x348+enemy)<<8)|R(0x330+enemy);
    int dx=ex-x,dy=(int)R(0x378+enemy)-R(0x378);
    if((R(0x528)&64)?dx<0 || dx>20:dx>0 || dx<-20)return 0;
    if(dy<-16 || dy>12 || R(0x390+enemy))return 0;
    int slot;for(slot=1;slot<=3;slot++)if(!R(0x300+slot))break;
    if(slot<=3 && cyc_mod_isolate_begin()) {
        CycModRegs r=caller;
        P(0xf5,0x3c);P(0xf6,0x3d);
        int ok=cyc_mod_call(0xff37,&r);
        P(0xa0,0);P(0x35,0);P(0x36,7);P(0x11,0x18);P(0x12,0x21);
        r.x=0;r.y=(uint8_t)slot;
        if(ok)ok=cyc_mod_call(0x8d5c,&r);
        uint8_t data[30];int n=0;
        for(int base=0x300;base<=0x5b8;base+=24)data[n++]=R(base+slot);
        cyc_mod_isolate_end();
        if(ok) {
            n=0;for(int base=0x300;base<=0x5b8;base+=24)P(base+slot,data[n++]);
            P(0x330+slot,R(0x330+enemy));P(0x348+slot,R(0x348+enemy));
            P(0x378+slot,R(0x378+enemy));P(0x390+slot,0);
            S.reflected[slot-1]=1;S.reflections++;
        }
    }
    P(0x300+enemy,0);P(0x5a0+enemy,0);P(0x528+enemy,0);
    S.blocks++;sound(0x2b);return 1;
}
static int skip_reflected(uint16_t address) {
    (void)address;
    if(!s_on || cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);
    /* The next BPL skips this shot. Its separate native Buster collision
     * below also runs while Rain Flush/Flash Stopper owns the weapon path. */
    if(reflected(r.y)){r.p&=(uint8_t)~0x80;cyc_mod_set_regs(&r);}
    return 0;
}
static int reflected_hits(uint16_t address) {
    (void)address;
    if(!s_on || cyc_mod_isolated())return 0;
    CycModRegs caller;cyc_mod_regs(&caller);int enemy=caller.x;
    if(enemy<5 || enemy>=24 || !R(0x300+enemy) || !(R(0x528+enemy)&128) || (R(0x528+enemy)&4))return 0;
    for(int slot=3;slot>=1;slot--) {
        if(!reflected(slot) || !(R(0x528+slot)&128))continue;
        int type=R(0x300+enemy),damage=prg(0x20,0xb700+type),vulnerable=(R(0x408+enemy)&64)!=0;
        if(!cyc_mod_isolate_begin())continue;
        CycModRegs r=caller;r.x=(uint8_t)enemy;
        int box=R(0x408+slot)*2;
        P(0x00,R(0x330+slot));P(0x01,R(0x348+slot));P(0x02,R(0x378+slot));
        P(0xe6,prg(0x3f,0xface + box));P(0xe7,prg(0x3f,0xfacf + box));
        int ok=cyc_mod_call(0xfa82,&r),hit=ok && !(r.p&1);
        if(hit) {
            P(0xa0,0);P(0x10,slot);P(0x12,damage);r.x=(uint8_t)enemy;r.y=(uint8_t)slot;
            if(vulnerable && damage)ok=cyc_mod_call(0x82a4,&r);
            else {
                r.a=0x2b;ok=cyc_mod_call(0xf6be,&r);r.y=(uint8_t)slot;
                if(ok)ok=cyc_mod_call(0xfccf,&r);
                P(0x300+slot,3);P(0x420+slot,(R(0x420+slot)^3)|8);
                P(0x3a8+slot,0);P(0x3d8+slot,0);P(0x3c0+slot,4);P(0x3f0+slot,4);
            }
        }
        uint8_t actors[2][30],audio[9],boss=R(0xbf);int n=0;
        for(int base=0x300;base<=0x5b8;base+=24){actors[0][n]=R(base+slot);actors[1][n++]=R(base+enemy);}
        audio[0]=R(0xda);for(int i=0;i<8;i++)audio[i+1]=R(0xdc+i);
        cyc_mod_isolate_end();
        if(!ok || !hit)continue;
        /* Only the two actors, native boss meter and SFX queue are results.
         * Mapper, clocks, stack, scratch and the selected weapon stay intact. */
        n=0;for(int base=0x300;base<=0x5b8;base+=24){P(base+slot,actors[0][n]);P(base+enemy,actors[1][n++]);}
        P(0xbf,boss);P(0xda,audio[0]);for(int i=0;i<8;i++)P(0xdc+i,audio[i+1]);
        S.hits++;
        if(R(0x300+slot)!=2)S.reflected[slot-1]=0;
        if(!R(0x300+enemy) || !(R(0x408+enemy)&64))break;
    }
    return 0;
}
void mm4_proto_body(Mm4ProtoBody *b) {
    memset(b,0,sizeof *b);
    b->active=(uint8_t)(mm4_proto_active() && R(0x22)<16);b->state=R(0x30);
    b->x=R(0x13);b->y=R(0x12);b->right=(R(0x528)&64)!=0;
    b->behind=(R(0x528)&32)!=0;b->weapon=R(0xa0);b->charge=R(0x35);
    for(int i=0;i<8;i++){b->palette[i]=R(0x610+i);b->master[i]=R(0x630+i);}
    int state=b->state,firing=R(0x32)!=0,throwing=R(0x33)>=2 && b->weapon;
    int base=R(0x558)-R(0x33),phase=R(0x540)&3,native_frame=drawn_frame();
    static const int run[]={0,1,2,1};
    if(S.teleport) {
        b->pose=CYC_PROTO_TELEPORT;
        b->y-=(S.teleport<=16?S.teleport-1:32-S.teleport)*16;
    } else if(state==0x0a) {
        /* Animation33 is the native blinking READY text owned by actor0.
         * Entry13 emits beam12, full13, beam12, partial14, partial14. */
        if(R(0x558)==0x33)b->active=0;
        b->pose=CYC_PROTO_TELEPORT;
        if(native_frame==0x13)b->pose=CYC_PROTO_TELEPORT_FULL;
        if(native_frame==0x14)b->pose=CYC_PROTO_TELEPORT_PARTIAL;
    } else if(state==6) {
        if(native_frame==0x0e)b->active=0; /* native sparks, not a body */
        b->pose=CYC_PROTO_HURT;
    }
    else if(state==2)b->pose=CYC_PROTO_SLIDE;
    else if(state==3) {
        b->pose=firing?CYC_PROTO_LADDER_FIRE0:base==0x14?CYC_PROTO_LADDER_TOP0:CYC_PROTO_LADDER0;
        if(!firing)b->right=(uint8_t)(phase&1);
    } else if(state==1)b->pose=firing?(throwing?CYC_PROTO_THROW_JUMP:CYC_PROTO_JUMP_FIRE):
        shield_up()?CYC_PROTO_JUMP_GUARD:CYC_PROTO_JUMP_OPEN;
    else if(base==4)b->pose=(uint8_t)((throwing?CYC_PROTO_THROW_RUN0:firing?CYC_PROTO_FIRE_RUN0:CYC_PROTO_RUN0)+run[phase]);
    else if(base==0x0d)b->pose=(uint8_t)(CYC_PROTO_STEP0+(phase&1));
    else b->pose=(uint8_t)(throwing?CYC_PROTO_THROW_IDLE:firing?CYC_PROTO_FIRE_IDLE0:CYC_PROTO_IDLE0+((R(0x94)>>3)&3));
}
void mm4_proto_power(void) {
    memset(s_players,0,sizeof s_players);for(int i=0;i<2;i++)s_players[i].selected=(uint8_t)s_start;
    free(s_cart);s_cart=NULL;s_size=cyc_mod_copy_cart_payload(NULL,0);
    if(s_size==0x80000) {
        s_cart=(uint8_t*)malloc(s_size);
        if(s_cart && cyc_mod_copy_cart_payload(s_cart,s_size)!=s_size){free(s_cart);s_cart=NULL;}
    }
    mm4_view_power();
}
void mm4_proto_end(void) {
    for(int seat=0;seat<(mm4_coop_enabled()?2:1);seat++)for(int slot=1;slot<=3;slot++)
        if(mm4_coop_read(seat,0x300+slot)!=2)s_players[seat].reflected[slot-1]=0;
    if(mm4_coop_enabled())return;
    if(!s_on || !S.teleport)return;
    if(S.teleport==16){S.selected^=1;sound(0x33);}
    if(++S.teleport>32){S.teleport=0;P(0x5b8,S.saved_stun);P(0x3c,S.saved_invincible);}
}
void mm4_proto_cancel_swap(void) {
    if(S.teleport){P(0x5b8,S.saved_stun);P(0x3c,S.saved_invincible);S.teleport=0;}
}
bool mm4_proto_set(int enabled,int mode,int start) {
    if(enabled && !s_loaded) {
        char why[160];
        if(!cyc_protoman_atlas_load(&s_atlas,MM4_PROTO_PNG,sizeof MM4_PROTO_PNG,MM4_PROTO_JUMP_PNG,sizeof MM4_PROTO_JUMP_PNG,why,sizeof why)) {
            fprintf(stderr,"[Proto Man] %s\n",why);return false;
        }
        s_loaded=1;
    }
    s_start=start!=0;s_mode=mode!=0;
    if(enabled && (!s_on || !cyc_ring_frame))S.selected=(uint8_t)s_start;
    if(!enabled && S.teleport){P(0x5b8,S.saved_stun);P(0x3c,S.saved_invincible);S.teleport=0;}
    s_on=enabled!=0;
    nes_mod_set_function_hook_enabled("mm4.proto.shot",s_on);
    nes_mod_set_function_hook_enabled("mm4.proto.muzzle",s_on);
    nes_mod_set_function_hook_enabled("mm4.proto.damage",s_on);
    nes_mod_set_function_hook_enabled("mm4.proto.shield",s_on);
    nes_mod_set_function_hook_enabled("mm4.proto.skip-reflected",s_on);
    nes_mod_set_function_hook_enabled("mm4.proto.reflected-hits",s_on);
    nes_mod_set_local_only("MM4 Proto Man (unqualified netplay)",s_on);
    mm4_controls_refresh();mm4_view_enable(s_on);return true;
}
static int get(uint8_t *out,int cap) {
    if(cap<sizeof s_players)return -1;
    for(int i=0;i<2;i++){memcpy(s_players[i].magic,"M4P3",4);s_players[i].enabled=(uint8_t)s_on;s_players[i].mode=(uint8_t)s_mode;}
    memcpy(out,s_players,sizeof s_players);return sizeof s_players;
}
static int valid(const uint8_t *in,int len) {
    if(!len)return !s_on;
    if(!in || len!=sizeof s_players)return 0;
    ProtoState p[2];memcpy(p,in,sizeof p);
    for(int i=0;i<2;i++)if(memcmp(p[i].magic,"M4P3",4) || p[i].enabled!=s_on || p[i].mode!=s_mode || p[i].selected>1 || p[i].teleport>32 ||
        p[i].reflected[0]>1 || p[i].reflected[1]>1 || p[i].reflected[2]>1)return 0;
    return 1;
}
static int set(const uint8_t *in,int len) {if(!valid(in,len))return 0;if(len)memcpy(s_players,in,sizeof s_players);else memset(s_players,0,sizeof s_players);return 1;}
int mm4_proto_json(char *out,int cap) {
    return snprintf(out,cap,"\"proto\":%d,\"mode\":%d,\"swap\":%u,\"switches\":%llu,\"limited\":%llu,\"pose\":%d,\"blocks\":%llu,\"reflections\":%llu,\"hits\":%llu,\"shots\":[%u,%u,%u]",
        mm4_proto_selected(),s_mode,S.teleport,(unsigned long long)S.switches,(unsigned long long)S.limited,drawn_frame(),
        (unsigned long long)S.blocks,(unsigned long long)S.reflections,(unsigned long long)S.hits,R(0x301),R(0x302),R(0x303));
}
void mm4_proto_register(void) {
    nes_mod_register_function_entry_plugin("mm4.proto.shot",0x8d5c,shot);
    nes_mod_register_function_entry_plugin("mm4.proto.muzzle",0x8dc3,muzzle);
    nes_mod_register_function_entry_plugin("mm4.proto.damage",0x81e4,damage);
    nes_mod_register_function_entry_plugin("mm4.proto.shield",0x81cc,shield);
    nes_mod_register_function_entry_plugin("mm4.proto.skip-reflected",0xfa47,skip_reflected);
    nes_mod_register_function_entry_plugin("mm4.proto.reflected-hits",0x81b2,reflected_hits);
    nes_mod_register_savestate_hook("mm4.proto",get,set);nes_mod_register_savestate_validator("mm4.proto",valid);
    mm4_view_register();
}
