#pragma once
#include <stdint.h>
typedef struct {int coop,independent,roster,proto,mode,start,weapons,wide,lines;} Mm4Settings;
int mm4_net_locked(void);
int mm4_net_simulation_width(int local_width);
void mm4_net_note_option(const char *name);
Mm4Settings mm4_host_settings(void);
int mm4_host_apply_settings(Mm4Settings settings,int local_seat);
int mm4_proto_source_hash(uint8_t out[32]);
