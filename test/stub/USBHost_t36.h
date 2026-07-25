#pragma once
#include <stdint.h>
struct USBHost { void begin(){} void Task(){} };
struct USBHub { USBHub(USBHost&){} };
struct MIDIDevice {
  MIDIDevice(USBHost&){}
  void (*noteOn)(uint8_t,uint8_t,uint8_t)=nullptr;
  void (*cc)(uint8_t,uint8_t,uint8_t)=nullptr;
  void setHandleNoteOn(void(*f)(uint8_t,uint8_t,uint8_t)){ noteOn=f; }
  void setHandleControlChange(void(*f)(uint8_t,uint8_t,uint8_t)){ cc=f; }
  bool read(){ return false; }
};
