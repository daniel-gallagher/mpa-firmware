// ---------------------------------------------------------------------------
// teensympa - MIDI note -> Rock Band zone mapping
//
// The original firmware did this with a switch statement whose case labels were
// a union of several modules' note numbers. That works, but it means notes that
// mean different things on different brains collide - on an Alesis kit, note 58
// is the tom 3 rim, but the shared table treated it as the blue (ride) cymbal,
// so hitting the floor tom rim fired a cymbal.
//
// Here each module gets its own explicit table, and the tables are expanded
// once at startup into a flat 128-entry lookup. Dispatch is a single array
// index instead of a jump table walk, and adding a module is a data change.
//
// Sources for the Alesis tables are the official Alesis user guides, which
// print a "Pad MIDI Note Numbers" / "Default Trigger MIDI Note Assignments"
// table. See docs/NOTE_MAPS.md for the quoted tables and links.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include "config.h"

// Rock Band controller zones. Order matters only in that ZONE_COUNT must be
// last; everything else is looked up by name.
enum : uint8_t {
  ZONE_RED = 0,     // snare
  ZONE_YELLOW,      // yellow pad (high tom)
  ZONE_BLUE,        // blue pad (mid tom)
  ZONE_GREEN,       // green pad (floor tom)
  ZONE_YELLOW_CYM,  // hi-hat
  ZONE_BLUE_CYM,    // ride
  ZONE_GREEN_CYM,   // crash
  ZONE_KICK,
  ZONE_KICK2,
  ZONE_COUNT,
  ZONE_NONE = 0xFF
};

// Set to 1 in config.h to let a hi-hat pedal "chick" fire the yellow cymbal.
// Off by default: a drummer riding the hi-hat pedal would otherwise spray
// yellow cymbal hits through every song, and in Rock Band an unwanted hit is
// an overhit that breaks your streak.
#ifndef ENABLE_HIHAT_PEDAL_AS_CYMBAL
#define ENABLE_HIHAT_PEDAL_AS_CYMBAL 0
#endif

struct NoteZone { uint8_t note; uint8_t zone; };

// ---------------------------------------------------------------------------
// Roland V-Drums (TD-1 / TD-4 / TD-17 ...) - the original firmware's map,
// preserved as-is so existing builds behave identically.
// ---------------------------------------------------------------------------
static const NoteZone PROGMEM MAP_ROLAND[] = {
  {27, ZONE_RED}, {31, ZONE_RED}, {34, ZONE_RED}, {37, ZONE_RED},
  {38, ZONE_RED}, {39, ZONE_RED}, {40, ZONE_RED},
  {35, ZONE_KICK}, {36, ZONE_KICK},
  {48, ZONE_YELLOW}, {50, ZONE_YELLOW},
  {45, ZONE_BLUE},   {47, ZONE_BLUE},
  {43, ZONE_GREEN},  {41, ZONE_GREEN},
  {22, ZONE_YELLOW_CYM}, {26, ZONE_YELLOW_CYM}, {42, ZONE_YELLOW_CYM},
  {46, ZONE_YELLOW_CYM}, {54, ZONE_YELLOW_CYM}, {78, ZONE_YELLOW_CYM},
  {79, ZONE_YELLOW_CYM}, {83, ZONE_YELLOW_CYM}, {85, ZONE_YELLOW_CYM},
  {86, ZONE_YELLOW_CYM},
#if ENABLE_HIHAT_PEDAL_AS_CYMBAL
  {44, ZONE_YELLOW_CYM},
#endif
  {51, ZONE_BLUE_CYM}, {53, ZONE_BLUE_CYM}, {58, ZONE_BLUE_CYM},
  {59, ZONE_BLUE_CYM},
  {49, ZONE_GREEN_CYM}, {52, ZONE_GREEN_CYM}, {55, ZONE_GREEN_CYM},
  {57, ZONE_GREEN_CYM},
};

// ---------------------------------------------------------------------------
// Yamaha DTX502 - crash and ride are swapped relative to the Roland map.
// ---------------------------------------------------------------------------
static const NoteZone PROGMEM MAP_YAMAHA_DTX[] = {
  {27, ZONE_RED}, {31, ZONE_RED}, {34, ZONE_RED}, {37, ZONE_RED},
  {38, ZONE_RED}, {39, ZONE_RED}, {40, ZONE_RED},
  {35, ZONE_KICK}, {36, ZONE_KICK},
  {48, ZONE_YELLOW}, {50, ZONE_YELLOW},
  {45, ZONE_BLUE},   {47, ZONE_BLUE},
  {43, ZONE_GREEN},  {41, ZONE_GREEN},
  {22, ZONE_YELLOW_CYM}, {26, ZONE_YELLOW_CYM}, {42, ZONE_YELLOW_CYM},
  {46, ZONE_YELLOW_CYM}, {54, ZONE_YELLOW_CYM}, {78, ZONE_YELLOW_CYM},
  {79, ZONE_YELLOW_CYM}, {83, ZONE_YELLOW_CYM}, {85, ZONE_YELLOW_CYM},
  {86, ZONE_YELLOW_CYM},
#if ENABLE_HIHAT_PEDAL_AS_CYMBAL
  {44, ZONE_YELLOW_CYM},
#endif
  {59, ZONE_BLUE_CYM}, {49, ZONE_BLUE_CYM}, {55, ZONE_BLUE_CYM},
  {51, ZONE_GREEN_CYM}, {52, ZONE_GREEN_CYM}, {53, ZONE_GREEN_CYM},
};

// ---------------------------------------------------------------------------
// Alesis Nitro / Nitro Mesh / Nitro Max / Nitro Pro / Surge Mesh / DM7X
// (all five user guides print the identical table)
//
//   Kick 36                Ride 51            Crash 1 49
//   Snare 38  Rim 40       Crash 2 57
//   Tom 1 48  Rim 50       Hi-Hat Open 46
//   Tom 2 45  Rim 47       Hi-Hat Half-Open 23
//   Tom 3 43  Rim 58       Hi-Hat Closed 42
//   Tom 4 41  Rim 39       Hi-Hat Pedal 44
//                          Splash 21
//
// Three of these are handled wrongly by the shared Roland table:
//   58 (tom 3 rim)  -> fired the BLUE cymbal instead of the green pad
//   39 (tom 4 rim)  -> unmapped (the Roland map has it on red)
//   23, 21          -> unmapped entirely
// ---------------------------------------------------------------------------
static const NoteZone PROGMEM MAP_ALESIS[] = {
  {36, ZONE_KICK},
  {38, ZONE_RED},    {40, ZONE_RED},           // snare head, rim
  {48, ZONE_YELLOW}, {50, ZONE_YELLOW},        // tom 1 head, rim
  {45, ZONE_BLUE},   {47, ZONE_BLUE},          // tom 2 head, rim
  {43, ZONE_GREEN},  {58, ZONE_GREEN},         // tom 3 head, rim
  {41, ZONE_GREEN},  {39, ZONE_GREEN},         // tom 4 head, rim
  {46, ZONE_YELLOW_CYM},                       // hi-hat open
  {23, ZONE_YELLOW_CYM},                       // hi-hat half open
  {42, ZONE_YELLOW_CYM},                       // hi-hat closed
  {21, ZONE_YELLOW_CYM},                       // splash
#if ENABLE_HIHAT_PEDAL_AS_CYMBAL
  {44, ZONE_YELLOW_CYM},                       // hi-hat pedal chick
#endif
  {51, ZONE_BLUE_CYM},                         // ride
  {49, ZONE_GREEN_CYM}, {57, ZONE_GREEN_CYM},  // crash 1, crash 2
};

// ---------------------------------------------------------------------------
// Alesis Command / Command Mesh - Nitro table plus cymbal edges and ride bell.
// ---------------------------------------------------------------------------
static const NoteZone PROGMEM MAP_ALESIS_CMD[] = {
  {36, ZONE_KICK},
  {38, ZONE_RED},    {40, ZONE_RED},
  {48, ZONE_YELLOW}, {50, ZONE_YELLOW},
  {45, ZONE_BLUE},   {47, ZONE_BLUE},
  {43, ZONE_GREEN},  {58, ZONE_GREEN},
  {41, ZONE_GREEN},  {39, ZONE_GREEN},
  {46, ZONE_YELLOW_CYM}, {42, ZONE_YELLOW_CYM}, {21, ZONE_YELLOW_CYM},
#if ENABLE_HIHAT_PEDAL_AS_CYMBAL
  {44, ZONE_YELLOW_CYM},
#endif
  {51, ZONE_BLUE_CYM},  {59, ZONE_BLUE_CYM},  {53, ZONE_BLUE_CYM},  // ride bow/edge/bell
  {49, ZONE_GREEN_CYM}, {55, ZONE_GREEN_CYM},                       // crash 1 bow/edge
  {57, ZONE_GREEN_CYM}, {52, ZONE_GREEN_CYM},                       // crash 2 bow/edge
};

// ---------------------------------------------------------------------------
// Alesis Crimson II - genuinely different. Tom 1 is 50 (not 48), tom 2 is 47
// (not 45), the rims are up in the 73-82 range, and the hi-hat sends note 8
// for BOTH open and closed (confirmed in Alesis's own knowledge base, not a
// documentation typo).
// ---------------------------------------------------------------------------
static const NoteZone PROGMEM MAP_ALESIS_CRIMSON[] = {
  {36, ZONE_KICK},
  {38, ZONE_RED},    {40, ZONE_RED},
  {50, ZONE_YELLOW}, {82, ZONE_YELLOW},
  {47, ZONE_BLUE},   {80, ZONE_BLUE},
  {43, ZONE_GREEN},  {75, ZONE_GREEN},
  {41, ZONE_GREEN},  {73, ZONE_GREEN},
  { 8, ZONE_YELLOW_CYM},                       // hi-hat open AND closed
  {23, ZONE_YELLOW_CYM},                       // hi-hat splash
#if ENABLE_HIHAT_PEDAL_AS_CYMBAL
  {22, ZONE_YELLOW_CYM},                       // hi-hat pedal
#endif
  {51, ZONE_BLUE_CYM},  {59, ZONE_BLUE_CYM},  {53, ZONE_BLUE_CYM},
  {49, ZONE_GREEN_CYM}, {57, ZONE_GREEN_CYM},
};

// ---------------------------------------------------------------------------

#if   KIT_PROFILE == KIT_PROFILE_ROLAND
  #define ACTIVE_MAP      MAP_ROLAND
  #define ACTIVE_MAP_NAME "Roland V-Drums"
#elif KIT_PROFILE == KIT_PROFILE_YAMAHA_DTX
  #define ACTIVE_MAP      MAP_YAMAHA_DTX
  #define ACTIVE_MAP_NAME "Yamaha DTX502"
#elif KIT_PROFILE == KIT_PROFILE_ALESIS
  #define ACTIVE_MAP      MAP_ALESIS
  #define ACTIVE_MAP_NAME "Alesis Nitro/Surge/DM7X"
#elif KIT_PROFILE == KIT_PROFILE_ALESIS_CMD
  #define ACTIVE_MAP      MAP_ALESIS_CMD
  #define ACTIVE_MAP_NAME "Alesis Command"
#elif KIT_PROFILE == KIT_PROFILE_ALESIS_CRIM
  #define ACTIVE_MAP      MAP_ALESIS_CRIMSON
  #define ACTIVE_MAP_NAME "Alesis Crimson II"
#else
  #error "KIT_PROFILE in config.h is not set to a known profile"
#endif

// Flat lookup, note number -> zone. Built once in setup().
extern uint8_t noteZone[128];

inline void buildNoteMap() {
  memset(noteZone, ZONE_NONE, sizeof(noteZone));
  const size_t n = sizeof(ACTIVE_MAP) / sizeof(ACTIVE_MAP[0]);
  for (size_t i = 0; i < n; i++) {
    uint8_t note = pgm_read_byte(&ACTIVE_MAP[i].note);
    uint8_t zone = pgm_read_byte(&ACTIVE_MAP[i].zone);
    if (note < 128) noteZone[note] = zone;
  }
}
