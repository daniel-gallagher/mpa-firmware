#pragma once
#include <stdint.h>
// Simulated Qw/ST Pad: a TCA9555 with polarity inversion, so we can prove the
// driver's init sequence and bit masks are handled correctly end to end.
struct TwoWire {
  uint8_t regs[8] = {0xFF,0xFF,0xFF,0xFF,0,0,0xFF,0xFF};
  uint8_t rawSwitches0 = 0xFF, rawSwitches1 = 0xFF; // active-low, 1 = released
  uint8_t addr=0, cmd=0; int wcount=0; uint8_t wbuf[4];
  int rdIdx=0; uint8_t rdBuf[4]; int rdLen=0;
  bool present=true;
  void begin(){} void setClock(uint32_t){}
  void beginTransmission(uint8_t a){ addr=a; wcount=0; }
  void write(uint8_t v){ if(wcount<4) wbuf[wcount]=v; wcount++; }
  uint8_t endTransmission(bool stop=true){
    (void)stop;
    if(!present || addr!=0x21) return 2;
    if(wcount>=1) cmd=wbuf[0];
    if(wcount>=3){ regs[cmd&7]=wbuf[1]; regs[(cmd&7)^1]=wbuf[2]; }
    return 0;
  }
  uint8_t requestFrom(uint8_t a,uint8_t n){
    if(!present || a!=0x21) return 0;
    // Input registers: raw switch state XOR polarity register
    uint8_t in0 = rawSwitches0 ^ regs[4];
    uint8_t in1 = rawSwitches1 ^ regs[5];
    rdBuf[0]= (cmd==0)?in0:regs[cmd&7];
    rdBuf[1]= (cmd==0)?in1:regs[(cmd&7)^1];
    rdLen=n; rdIdx=0; return n;
  }
  uint8_t read(){ return rdIdx<rdLen ? rdBuf[rdIdx++] : 0; }
};
extern TwoWire Wire;
