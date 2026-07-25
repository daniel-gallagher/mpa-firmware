// Host-side stand-in for the Teensy 4 driver. Implements exactly the same
// packet-mutation semantics as core-patch/teensy4/usb_mpa.c so the firmware's
// report building can be tested off-target.
#include "usb_mpa.h"
#include <string.h>
#include <vector>
uint8_t usb_mpa_data[MPA_PACK_SIZE] = {
  0x00,0x00, MPA_HAT_NEUTRAL, 0x80,0x80,0x80,0x80,
  0,0,0,0,0,0, 0,0,0,0,0,0,
  0x00,0x02,0x00,0x02,0x00,0x02,0x00,0x02 };
volatile uint8_t usb_configuration = 1;
static uint8_t last[MPA_PACK_SIZE]; static bool lastValid=false;
std::vector<std::vector<uint8_t>> g_sent;
void usb_mpa_configure(void){}
void usb_mpa_reset_packet(void){ usb_mpa_data[0]=0; usb_mpa_data[1]=0;
  usb_mpa_data[2]=MPA_HAT_NEUTRAL; memset(usb_mpa_data+7,0,MPA_VEL_COUNT); }
void usb_mpa_set_button(int b){ if(b<0||b>=MPA_BUTTON_COUNT)return;
  if(b<8) usb_mpa_data[0]|=(uint8_t)(1<<b); else usb_mpa_data[1]|=(uint8_t)(1<<(b-8)); }
void usb_mpa_clear_button(int b){ if(b<0||b>=MPA_BUTTON_COUNT)return;
  if(b<8) usb_mpa_data[0]&=(uint8_t)~(1<<b); else usb_mpa_data[1]&=(uint8_t)~(1<<(b-8)); }
void usb_mpa_set_hat(uint8_t h){ if(h>MPA_HAT_NEUTRAL)h=MPA_HAT_NEUTRAL; usb_mpa_data[2]=h; }
void usb_mpa_set_axis(uint8_t a,uint8_t v){ if(a<MPA_AXIS_COUNT) usb_mpa_data[3+a]=v; }
void usb_mpa_set_velocity(uint8_t i,uint8_t v){ if(i<MPA_VEL_COUNT) usb_mpa_data[7+i]=v; }
int usb_mpa_send(void){ g_sent.push_back(std::vector<uint8_t>(usb_mpa_data,usb_mpa_data+MPA_PACK_SIZE));
  memcpy(last,usb_mpa_data,MPA_PACK_SIZE); lastValid=true; return 0; }
int usb_mpa_send_if_changed(void){ if(lastValid && !memcmp(last,usb_mpa_data,MPA_PACK_SIZE)) return 0;
  int r=usb_mpa_send(); return r<0?r:1; }
