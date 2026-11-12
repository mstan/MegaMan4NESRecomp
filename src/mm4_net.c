/* Use the shared pre-boot identity/capability barrier and rollback driver. */
#include "mm4_net.h"
#include "mm4_coop.h"
#include "cyc_core.h"
#include "cyc_video.h"
#include "cyc_session.h"
#include "mod_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int s_sealed,s_applying,s_width=256;
static unsigned s_cli;
enum {CLI_COOP=1u<<0,CLI_VIEW=1u<<1,CLI_ROSTER=1u<<2,CLI_PROTO=1u<<3,
      CLI_MODE=1u<<4,CLI_START=1u<<5,CLI_WEAPONS=1u<<6,CLI_WIDE=1u<<7,
      CLI_LINES=1u<<8,CLI_QA=1u<<9,CLI_FLICKER=1u<<10};
int mm4_net_locked(void) {return s_sealed && !s_applying;}
int mm4_net_simulation_width(int local) {return s_sealed?s_width:local;}
void mm4_net_note_option(const char *name) {
    if(!strcmp(name,"--coop"))s_cli|=CLI_COOP;
    if(!strcmp(name,"--coop-view"))s_cli|=CLI_VIEW;
    if(!strcmp(name,"--coop-roster"))s_cli|=CLI_ROSTER;
    if(!strcmp(name,"--protoman"))s_cli|=CLI_PROTO;
    if(!strcmp(name,"--proto-mode"))s_cli|=CLI_MODE;
    if(!strcmp(name,"--proto-start"))s_cli|=CLI_START;
    if(!strcmp(name,"--weapon-cycle"))s_cli|=CLI_WEAPONS;
    if(!strcmp(name,"--wide"))s_cli|=CLI_WIDE;
    if(!strcmp(name,"--slowdown-reduction"))s_cli|=CLI_LINES;
    if(!strcmp(name,"--no-flicker"))s_cli|=CLI_FLICKER;
    if(!strcmp(name,"--mm4-coop-poke") || !strcmp(name,"--mm4-qa-entry"))s_cli|=CLI_QA;
}
#ifdef NESRECOMP_NET
#include "cyc_net.h"
#include "nes_netplay.h"
#include "nes_netplay_identity.h"
#ifdef CYC_WITH_RECOMP_UI
#include "recomp_launcher.h"
#endif
#ifndef MM4_NETPLAY_QUALIFIED
#define MM4_NETPLAY_QUALIFIED 0
#endif
static Mm4Settings s_offer,s_offline,s_offline_offer;
static int s_saved,s_offline_view,s_offline_video,s_offline_width,s_offline_draw_w,s_offline_draw_h;
static int selected(const char *id,const char *feature) {return nes_mod_feature_selected(id,feature)!=0;}
static int option_is(const char *id,const char *feature,const char *key,const char *want,const char *fallback) {
    char value[32];if(!nes_mod_option_value(id,feature,key,value,sizeof value))snprintf(value,sizeof value,"%s",fallback);return !strcmp(value,want);
}
static Mm4Settings offer(void) {
    Mm4Settings c=mm4_host_settings();if(!cyc_session_mods_enabled())return c;
    const char *proto="mega-man-4-usa.character.protoman",*coop="mega-man-4-usa.coop";
    if(!(s_cli&CLI_COOP))c.coop=selected(coop,"coop");
    c.independent=(s_cli&CLI_VIEW)?mm4_host_view_preference()!=0:!option_is(coop,"coop","coop-view","shared","shared");
    if(!(s_cli&CLI_ROSTER))c.roster=option_is(coop,"coop","player-one","protoman","megaman");
    if(!(s_cli&CLI_PROTO))c.proto=selected(proto,"protoman");
    c.proto=c.proto || c.coop; /* co-op includes the character implementation */
    if(!(s_cli&CLI_MODE))c.mode=option_is(proto,"protoman","mode","protoman","protoman");
    if(!(s_cli&CLI_START))c.start=option_is(proto,"protoman","start","protoman","protoman");
    if(!(s_cli&CLI_WEAPONS))c.weapons=selected("mega-man-4-usa.enhancement.weapons","weapons");
    if(!(s_cli&CLI_WIDE))c.wide=selected("mega-man-4-usa.enhancement.widescreen","widescreen");
    if(!(s_cli&CLI_FLICKER))c.flicker=selected("mega-man-4-usa.enhancement.noflicker","noflicker");
    if(!(s_cli&CLI_LINES))c.lines=selected("mega-man-4-usa.enhancement.slowdown","slowdown")?262:0;
    return c;
}
static int catalog_supported(void) {
    if(!cyc_session_mods_enabled())return 1;
#ifdef CYC_WITH_RECOMP_UI
    const RecompLauncherCModProvider *p=nes_mod_runtime_launcher_provider_c();
    if(!p || !p->feature_count || !p->feature_get)return 0;
    static const char *known[][2]={
        {"mega-man-4-usa.coop","coop"},{"mega-man-4-usa.character.protoman","protoman"},
        {"mega-man-4-usa.enhancement.weapons","weapons"},{"mega-man-4-usa.enhancement.widescreen","widescreen"},
        {"mega-man-4-usa.enhancement.slowdown","slowdown"},
        {"mega-man-4-usa.enhancement.noflicker","noflicker"},
        {"nesrecomp.developer.replay","replay-capture"}};
    for(int i=0;i<p->feature_count(p->ctx);i++) {
        RecompLauncherCModFeature f;if(!p->feature_get(p->ctx,i,&f))return 0;if(!f.enabled)continue;
        int found=0;for(unsigned j=0;j<sizeof known/sizeof *known;j++)if(!strcmp(f.package_id,known[j][0]) && !strcmp(f.id,known[j][1]))found=1;
        if(!found){fprintf(stderr,"[netplay] unqualified feature: %s/%s\n",f.package_id,f.id);return 0;}
    }
    return 1;
#else
    return 0;
#endif
}
static int describe(Mm4Settings c,char *out,int cap) {
    return snprintf(out,cap,"2:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",c.coop,c.independent,c.roster,c.proto,c.mode,c.start,c.weapons,c.wide,c.flicker,c.lines);
}
static int settings_get(char *out,int cap) {return describe(mm4_host_settings(),out,cap);}
static int settings_offer(char *out,int cap) {s_offer=offer();return describe(s_offer,out,cap);}
static int settings_apply(const char *value) {
    Mm4Settings c;int version,n=0;
    if((s_cli&CLI_QA) || !catalog_supported())return 0;
    if(sscanf(value,"%1d:%1d:%1d:%1d:%1d:%1d:%1d:%1d:%1d:%1d:%3d%n",&version,&c.coop,&c.independent,&c.roster,&c.proto,&c.mode,&c.start,&c.weapons,&c.wide,&c.flicker,&c.lines,&n)!=11 || value[n] || version!=2)return 0;
    if(c.coop!=1 || c.proto!=1 || c.independent<0 || c.independent>1 || c.roster<0 || c.roster>1 || c.mode<0 || c.mode>1 ||
        c.start<0 || c.start>1 || c.weapons<0 || c.weapons>1 || c.wide<0 || c.wide>1 || c.flicker<0 || c.flicker>1 || (c.lines!=0 && c.lines!=262) || (c.independent && s_width!=256))return 0;
    if(!s_saved){s_offline=mm4_host_settings();s_offline_offer=offer();s_offline_view=mm4_host_view_preference();s_offline_video=cyc_video_mode();s_offline_width=cyc_video_width();cyc_video_drawable_size(&s_offline_draw_w,&s_offline_draw_h);s_saved=1;}
    s_applying=1;int ok=mm4_host_apply_settings(c,nes_netplay_local_slot());
    /* The online catalog defers activation plugins. Keep its local view
     * preference while the host's shared/independent class stays sealed. */
    if(ok && c.independent && !(s_cli&CLI_VIEW) && cyc_session_mods_enabled()) {
        const char *coop="mega-man-4-usa.coop";
        if(option_is(coop,"coop","coop-view","split","shared"))mm4_host_view_set(1);
        else if(option_is(coop,"coop","coop-view","p1","shared"))mm4_host_view_set(2);
        else if(option_is(coop,"coop","coop-view","p2","shared"))mm4_host_view_set(3);
    }
    s_applying=0;if(!ok)return 0;
    s_sealed=1;nes_netplay_require_capabilities(1);nes_netplay_require_player_count(2);
    if(!c.independent)cyc_video_request_width(s_width);
    if(!getenv("NES_NET_INPUT_PLAYER")) {
        char binding[16]="seat";if(cyc_session_mods_enabled())nes_mod_option_value("mega-man-4-usa.coop","coop","online-input",binding,sizeof binding);
        nes_netplay_set_input_player(!strcmp(binding,"device1")?0:!strcmp(binding,"device2")?1:nes_netplay_local_slot());
    }
#if MM4_NETPLAY_QUALIFIED
    static const char *names[]={"MM4 personal co-op (unqualified netplay)","MM4 Proto Man (unqualified netplay)","MM4 weapon switching (unqualified netplay)","MM4 widened world (unqualified netplay)","MM4 CPU budget (unqualified netplay)","MM4 complete sprites (unqualified netplay)"};
    for(unsigned i=0;i<sizeof names/sizeof *names;i++)nes_mod_set_local_only(names[i],0);
#else
    nes_mod_set_local_only("MM4 unqualified simulation profile",1);
#endif
    return cyc_extra_scanlines()==(unsigned)c.lines;
}
static void settings_restore(void) {
    s_sealed=0;if(s_saved){
        mm4_host_apply_settings(s_offline,0);mm4_host_view_set(s_offline_view);
        cyc_video_restore(s_offline_video,s_offline_width,s_offline_draw_w,s_offline_draw_h);
        /* Optional readback of the actual restore callback. Inactive catalog
         * preferences can differ from active runtime settings; preserve each. */
        const char *record=getenv("MM4_NET_RESTORE_RECORD");
        if(record && *record) {
            char expected[192],actual[192],original[192],next[192];
            describe(s_offline,expected,sizeof expected);settings_get(actual,sizeof actual);
            describe(s_offline_offer,original,sizeof original);describe(offer(),next,sizeof next);
            FILE *f=fopen(record,"w");if(f) {
                fprintf(f,"{\"expected\":\"%s\",\"actual\":\"%s\",\"original_offer\":\"%s\",\"next_offer\":\"%s\",\"expected_view\":%d,\"view\":%d,\"expected_video_mode\":%d,\"video_mode\":%d,\"expected_width\":%d,\"width\":%d}\n",
                    expected,actual,original,next,s_offline_view,mm4_host_view_preference(),s_offline_video,cyc_video_mode(),s_offline_width,cyc_video_width());fclose(f);
            }
        }
        s_saved=0;
    }
    nes_netplay_require_capabilities(0);nes_netplay_require_player_count(0);nes_netplay_set_input_player(-1);
    nes_mod_set_local_only("MM4 unqualified simulation profile",0);
}
static int width_get(char *out,int cap) {return snprintf(out,cap,"%d",s_sealed?s_width:(mm4_coop_independent()?256:cyc_video_width()));}
static int width_offer(char *out,int cap) {Mm4Settings c=offer();int w=c.independent?256:cyc_video_width();if(c.wide && !c.independent && w<=256)w=426;return snprintf(out,cap,"%d",w);}
static int width_apply(const char *v) {char *end;long n=strtol(v,&end,10);if(end==v || *end || n<256 || n>CYC_VIDEO_MAX_WIDTH || (n&1))return 0;s_width=(int)n;return 1;}
static int timing_get(char *out,int cap) {return snprintf(out,cap,"%d:%u",(int)cyc_region(),cyc_extra_scanlines());}
static int timing_offer(char *out,int cap) {return snprintf(out,cap,"%d:%d",(int)cyc_region(),offer().lines);}
static int timing_apply(const char *v) {int region;unsigned lines;char tail;return sscanf(v,"%1d:%3u%c",&region,&lines,&tail)==2 && region==(int)cyc_region() && (lines==0 || lines==262) && cyc_set_extra_scanlines(lines);}
static int sheet_get(char *out,int cap) {
    uint8_t digest[32];if(!mm4_proto_source_hash(digest))return 0;
    if(cap<65)return 0;for(int i=0;i<32;i++)snprintf(out+i*2,3,"%02x",digest[i]);return 64;
}
static int sheet_apply(const char *v) {char actual[65];return sheet_get(actual,sizeof actual)==64 && !strcmp(v,actual);}
NES_MOD_CONSTRUCTOR(register_mm4_net) {
    nes_runner_register_session_keys();
    nes_netplay_session_register("timing",timing_get,timing_apply,NULL);nes_netplay_session_set_offer("timing",timing_offer);
    nes_netplay_session_register("vw",width_get,width_apply,NULL);nes_netplay_session_set_offer("vw",width_offer);
    nes_netplay_session_register("mm4",settings_get,settings_apply,settings_restore);nes_netplay_session_set_offer("mm4",settings_offer);
    nes_netplay_session_register("sheet",sheet_get,sheet_apply,NULL);
}
#endif
