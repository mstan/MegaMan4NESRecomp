#include "mm4_helpers.h"
#include "mm4_coop.h"
#include "cyc_mod.h"
#include "mod_function_hooks.h"
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
/* This journal exists only across an unclocked call and its enclosing
 * isolation restore. All persistent body/helper/fuel data already belongs to
 * the native personal snapshot; no second helper simulation is introduced. */
enum { RIDER_FIELDS=54 };
static struct {int active,owner,rider,weapon;uint8_t helper[30],ammo,rider_after[RIDER_FIELDS];} pending;
static uint16_t rider_address(int i) {return (uint16_t)(i<30?0x300+i*24:0x30+i-30);}

int mm4_helpers_floor(unsigned short address) {
    (void)address;
    int rider=mm4_coop_seat(),owner=rider^1;
    if(!mm4_coop_live(rider) || !mm4_coop_live(owner) || mm4_coop_menu_active() || mm4_coop_scene_locked() ||
       (cyc_mod_isolated() && !mm4_coop_native_scope()))return 0;
    CycModRegs original,call;cyc_mod_regs(&original);if(original.x)return 0;
    int balloon[4]={0},count=0;
    for(int slot=1;slot<=3;slot++)if(mm4_coop_read(owner,0x300+slot)==6){balloon[slot]=1;count++;}
    if(!count)return 0;
    int isolated=cyc_mod_isolated();if(!isolated && !cyc_mod_isolate_begin())return 0;
    nes_mod_set_function_hook_enabled("mm4.coop.balloon-floor",0);
    call=original;int ok=cyc_mod_call(0xF7D8,&call);
    if(ok && !(call.p&1)) {
        /* Own helpers and native world platforms keep their normal priority.
         * Repeat only the original floor probe with the foreign Balloons;
         * their motion, lifetime and owner's fuel never run a second time. */
        uint8_t saved[4][30];
        for(int slot=1;slot<=3;slot++)if(balloon[slot])for(int i=0;i<30;i++) {
            uint16_t a=(uint16_t)(0x300+slot+i*24);saved[slot][i]=R(a);P(a,mm4_coop_read(owner,a));
        }
        call=original;ok=cyc_mod_call(0xF7D8,&call);
        for(int slot=1;slot<=3;slot++)if(balloon[slot])for(int i=0;i<30;i++)P(0x300+slot+i*24,saved[slot][i]);
    }
    uint8_t scratch[20];for(int i=0;i<20;i++)scratch[i]=R(i);
    if(!isolated)cyc_mod_isolate_end();
    nes_mod_set_function_hook_enabled("mm4.coop.balloon-floor",1);
    if(!ok)return 0;
    for(int i=0;i<20;i++)P(i,scratch[i]);call.s=original.s;cyc_mod_set_regs(&call);return 1;
}

static int contact_ready(void) {
    int owner=mm4_coop_seat(),rider=owner^1;
    return mm4_coop_enabled() && mm4_coop_live(owner) && mm4_coop_live(rider) &&
        !mm4_coop_menu_active() && !mm4_coop_scene_locked() &&
        (R(0xA0)==1 || R(0xA0)==2) && R(0x304)==4 &&
        ((R(0x58C)==0xE3 && R(0x5A4)==0x89 && !R(0x544) && (R(0x52C)&1)) ||
         (R(0xA0)==2 && R(0x58C)==0x86 && R(0x5A4)==0x8A)) &&
        mm4_coop_read(rider,0x30)<=2 && (mm4_coop_read(rider,0xB0)&31);
}
static int own_jet_contact(int rider,CycModRegs *call) {
    if(mm4_coop_read(rider,0xA0)!=2 || mm4_coop_read(rider,0x304)!=4 ||
       mm4_coop_read(rider,0x58C)!=0x86 || mm4_coop_read(rider,0x5A4)!=0x8A)return 0;
    uint8_t owner[30];
    for(int i=0;i<30;i++){uint16_t a=(uint16_t)(0x304+i*24);owner[i]=R(a);P(a,mm4_coop_read(rider,a));}
    P(0x37C,R(0x37C)-1);call->x=4;int ok=cyc_mod_call(0xF95D,call);
    for(int i=0;i<30;i++)P(0x304+i*24,owner[i]);
    return ok?!(call->p&1):-1;
}
/* $3A:89E3 has already ticked the owner's waiting timer. $8A06 repeats only
 * its original collision/spring tail, including native $FD75 owner fuel. */
static int contact(void) {
    if(!contact_ready())return 1;
    CycModRegs original={0},call;cyc_mod_regs(&original);call=original;call.x=4;
    int owner=mm4_coop_seat(),rider=owner^1,weapon=R(0xA0),flying=R(0x58C)==0x86;
    uint8_t body[RIDER_FIELDS],force=(uint8_t)mm4_coop_read(rider,0x498);
    for(int i=0;i<RIDER_FIELDS;i++){uint16_t a=rider_address(i);body[i]=R(a);P(a,mm4_coop_read(rider,a));}
    int ok=1,changed=0;
    if(weapon==2) {
        /* Overlapping personal Jets must not carry the same hero twice.
         * Its own native Jet wins; the exact native shape tests contact. */
        int own=own_jet_contact(rider,&call);
        if(own){ok=own>0;goto restore;}
    }
    if(flying) {
        /* The original Jet has already moved/drained once. Repeat only its
         * native contact and horizontal passenger carry, never its AI. */
        uint8_t y=R(0x37C);P(0x37C,y-1);ok=cyc_mod_call(0xF95D,&call);P(0x37C,y);
        if(ok && !(call.p&1)){call.x=4;ok=cyc_mod_call(0xF32F,&call);changed=ok;}
    } else {
        ok=cyc_mod_call(0x8A06,&call);
        changed=ok && ((weapon==1 && R(0x3D8)==0xEE && R(0x3F0)==6 && R(0x498)==(uint8_t)(force+1)) ||
            (weapon==2 && R(0x58C)==0x86 && R(0x5A4)==0x8A));
    }
    if(changed) {
        pending.active=1;pending.owner=owner;pending.rider=rider;pending.weapon=weapon;pending.ammo=R(0xB0+weapon);
        for(int i=0;i<30;i++)pending.helper[i]=R(0x304+i*24);
        for(int i=0;i<RIDER_FIELDS;i++)pending.rider_after[i]=R(rider_address(i));
    }
restore:
    for(int i=0;i<RIDER_FIELDS;i++)P(rider_address(i),body[i]);
    cyc_mod_set_regs(&original);return ok;
}
void mm4_helpers_commit(int success) {
    if(success && pending.active) {
        for(int i=0;i<30;i++)mm4_coop_write(pending.owner,(uint16_t)(0x304+i*24),pending.helper[i]);
        mm4_coop_write(pending.owner,(uint16_t)(0xB0+pending.weapon),pending.ammo);
        for(int i=0;i<RIDER_FIELDS;i++)mm4_coop_write(pending.rider,rider_address(i),pending.rider_after[i]);
    }
    memset(&pending,0,sizeof pending);
}
int mm4_helpers_private_contact(void) {
    memset(&pending,0,sizeof pending);return contact();
}
int mm4_helpers_shared_contact(void) {
    if(!contact_ready())return 1;
    uint8_t before[8],after[8],producer=R(0xDA);for(int i=0;i<8;i++)before[i]=R(0xDC+i);
    memset(&pending,0,sizeof pending);
    if(!cyc_mod_isolate_begin())return 0;
    int ok=contact();for(int i=0;i<8;i++)after[i]=R(0xDC+i);
    cyc_mod_isolate_end();mm4_helpers_commit(ok);
    if(ok)for(int i=0;i<8;i++) {
        int q=(producer+i)&7,dst=R(0xDA)&7;
        if(before[q]==0x88 && after[q]!=0x88 && R(0xDC+dst)==0x88){P(0xDC+dst,after[q]);P(0xDA,(dst+1)&7);}
    }
    return ok;
}
