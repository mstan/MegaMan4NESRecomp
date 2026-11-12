#include "mm4_helpers.h"
#include "mm4_coop.h"
#include "cyc_mod.h"
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
/* This journal exists only across an unclocked call and its enclosing
 * isolation restore. All persistent body/helper/fuel data already belongs to
 * the native personal snapshot; no second helper simulation is introduced. */
static struct {int active,owner,rider;uint8_t helper[30],ammo,impulse[3];} pending;
static const uint16_t IMPULSE[]={0x3D8,0x3F0,0x498};

static int waiting_coil(void) {
    int owner=mm4_coop_seat(),rider=owner^1;
    return mm4_coop_enabled() && mm4_coop_live(owner) && mm4_coop_live(rider) &&
        !mm4_coop_menu_active() && !mm4_coop_scene_locked() && R(0xA0)==1 &&
        R(0x304)==4 && R(0x58C)==0xE3 && R(0x5A4)==0x89 && !R(0x544) &&
        (R(0x52C)&1) && mm4_coop_read(rider,0x30)==0 && (mm4_coop_read(rider,0xB0)&31);
}
/* $3A:89E3 has already ticked the owner's waiting timer. $8A06 repeats only
 * its original collision/spring tail, including native $FD75 owner fuel. */
static int contact(void) {
    if(!waiting_coil())return 1;
    CycModRegs original={0},call;cyc_mod_regs(&original);call=original;call.x=4;
    int owner=mm4_coop_seat(),rider=owner^1;
    uint8_t body[30],state=R(0x30),force=(uint8_t)mm4_coop_read(rider,0x498);
    for(int i=0;i<30;i++){int a=0x300+i*24;body[i]=R(a);P(a,mm4_coop_read(rider,(uint16_t)a));}
    P(0x30,mm4_coop_read(rider,0x30));
    int ok=cyc_mod_call(0x8A06,&call);
    if(ok && R(0x3D8)==0xEE && R(0x3F0)==6 && R(0x498)==(uint8_t)(force+1)) {
        pending.active=1;pending.owner=owner;pending.rider=rider;pending.ammo=R(0xB1);
        for(int i=0;i<30;i++)pending.helper[i]=R(0x304+i*24);
        for(int i=0;i<3;i++)pending.impulse[i]=R(IMPULSE[i]);
    }
    for(int i=0;i<30;i++)P(0x300+i*24,body[i]);P(0x30,state);
    cyc_mod_set_regs(&original);return ok;
}
void mm4_helpers_commit(int success) {
    if(success && pending.active) {
        for(int i=0;i<30;i++)mm4_coop_write(pending.owner,(uint16_t)(0x304+i*24),pending.helper[i]);
        mm4_coop_write(pending.owner,0xB1,pending.ammo);
        for(int i=0;i<3;i++)mm4_coop_write(pending.rider,IMPULSE[i],pending.impulse[i]);
    }
    memset(&pending,0,sizeof pending);
}
int mm4_helpers_private_contact(void) {
    memset(&pending,0,sizeof pending);return contact();
}
int mm4_helpers_shared_contact(void) {
    if(!waiting_coil())return 1;
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
