#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t byte;
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t*)(a))
extern uint32_t g_millis;
inline uint32_t millis(){ return g_millis; }
inline void delay(uint32_t){}
inline void pinMode(int,int){}
inline void digitalWrite(int,int){}
extern int g_pinState[64];
inline int digitalRead(int p){ return g_pinState[p&63]; }
struct FakeSerial {
  void begin(uint32_t){}
  template<class T> void print(T v){ (void)v; }
  void println(){}
  template<class T> void println(T v){ (void)v; }
  template<class... A> void printf(const char* f, A... a){ if(getenv("VERBOSE")) fprintf(stderr,f,a...); }
};
extern FakeSerial Serial1;
#include <stdlib.h>
