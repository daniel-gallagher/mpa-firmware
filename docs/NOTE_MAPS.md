# Drum module note maps

The tables the firmware's profiles are built from, with sources and confidence
levels. If your module isn't here, see [Profiling an unknown module](#profiling-an-unknown-module).

## Why per-module tables

The original firmware used one `switch` statement whose case labels were the
union of several modules' note numbers. That works only as long as no two
modules disagree — and they do. Note 58 is the ride cymbal on a Roland and the
tom 3 rim on an Alesis. Under the shared table, hitting an Alesis floor tom rim
fired the blue cymbal.

Each profile now gets its own table, expanded once at startup into a flat
128-entry lookup. Selecting one is `KIT_PROFILE` in `config.h`.

## Alesis Nitro / Nitro Mesh / Nitro Max / Nitro Pro / Surge Mesh / DM7X

`KIT_PROFILE_ALESIS` — **high confidence**. All five user guides print this
identical table under "Pad MIDI Note Numbers".

| Trigger | Note | → zone | | Trigger | Note | → zone |
|---|---|---|---|---|---|---|
| Kick | 36 | kick | | Ride | 51 | blue cymbal |
| Snare | 38 | red | | Crash 1 | 49 | green cymbal |
| Snare Rim | 40 | red | | Crash 2 | 57 | green cymbal |
| Tom 1 | 48 | yellow | | Hi-Hat Open | 46 | yellow cymbal |
| Tom 1 Rim | 50 | yellow | | Hi-Hat Half-Open | 23 | yellow cymbal |
| Tom 2 | 45 | blue | | Hi-Hat Closed | 42 | yellow cymbal |
| Tom 2 Rim | 47 | blue | | Hi-Hat Pedal | 44 | *ignored* |
| Tom 3 | 43 | green | | Splash | 21 | yellow cymbal |
| Tom 3 Rim | 58 | green | | | | |
| Tom 4 | 41 | green | | | | |
| Tom 4 Rim | 39 | green | | | | |

Four of these were handled wrongly or not at all by the shared Roland table:
**58** fired the blue cymbal instead of the green pad, **39** landed on red,
and **23** and **21** were unmapped.

Cymbals on this class of module are single-zone — no crash edge, no ride bell.

## Alesis Command / Command Mesh

`KIT_PROFILE_ALESIS_CMD` — **high confidence**. Same toms and snare as Nitro,
plus cymbal edges and a ride bell.

Additional: Ride Edge **59**, Ride Bell **53** → blue cymbal.
Crash 1 Edge **55**, Crash 2 Edge **52** → green cymbal.
No half-open hi-hat (no note 23).

## Alesis Crimson II

`KIT_PROFILE_ALESIS_CRIM` — **high confidence**, and genuinely different from
the others. Do not reuse the Command table here.

| Trigger | Note | | Trigger | Note |
|---|---|---|---|---|
| Kick | 36 | | Ride Bow / Edge / Bell | 51 / 59 / 53 |
| Snare / Rim | 38 / 40 | | Crash 1 / Crash 2 | 49 / 57 |
| Tom 1 / Rim | **50** / 82 | | Hi-Hat Open **and** Closed | **8** |
| Tom 2 / Rim | **47** / 80 | | Hi-Hat Pedal | 22 |
| Tom 3 / Rim | 43 / 75 | | Hi-Hat Splash | 23 |
| Tom 4 / Rim | 41 / 73 | | | |

Two things that look like documentation typos but are not: the hi-hat sends
note 8 for **both** open and closed (Alesis's own knowledge base confirms it,
and documents remapping open to 9 as the workaround), and crash bow and edge
share a note.

## Roland V-Drums

`KIT_PROFILE_ROLAND` — the original firmware's map, preserved verbatim so
existing Teensy 3.6 builds behave identically. It is a broad catch-all rather
than a documented per-model table.

## Yamaha DTX502

`KIT_PROFILE_YAMAHA_DTX` — the Roland map with crash and ride swapped, which
is what the original `YAMAHA_DTX_502` define did.

## The hi-hat pedal

The hi-hat pedal "chick" (note 44 on most modules, 22 on a Crimson II) is
**deliberately unmapped**. In Rock Band an unwanted hit is an overhit that
breaks your streak, and a drummer who rides the hi-hat pedal would spray yellow
cymbal hits through every song. Set `ENABLE_HIHAT_PEDAL_AS_CYMBAL` to 1 in
`config.h` to restore the original behaviour.

Separately, the pedal's **continuous controller** stream is now blocked from
acting as a button. See the CC section of `config.h` — this was the single
worst bug in the original firmware for Alesis owners.

## Modules with no published table

Alesis **DM6**, **DM10**, **DM10 MKII Pro** and **Strike / Strike Pro SE** do
not print a default note table anywhere in their official manuals — every voice
is user-assignable. (The DM10 manual does note that hi-hat and ride notes are
fixed, without saying to what.) **DM Lite** has only a secondary-source table
whose "hi-hat closed = 44" looks wrong against every other Alesis module.

Don't guess these. Profile them.

## Profiling an unknown module

1. In `config.h`, uncomment `DEBUG_ENABLED` and `DEBUG_MIDI_MONITOR`.
2. Build with **USB Type: MPA + Serial (debug)**, or wire a USB-serial adapter
   to pins 0/1 at 115200 baud.
3. Hit each pad and zone in turn and write down the note numbers. The firmware
   also prints `unmapped note NN` for anything it doesn't recognise, so you can
   start from an existing profile and just fill the gaps.
4. Add a table to `notemap.h` and a `KIT_PROFILE_*` constant to `config.h`.

Also worth checking while you're in there: whether your module sends a real
Note Off, and whether the hi-hat streams CC#4 continuously or only on change.
Neither matters to this firmware — it's timer-driven and ignores note-offs —
but it tells you whether `CC_ALWAYS_IGNORE_1` is doing real work for you.

## Sources

Official Alesis user guides (alesis.com), which print the tables quoted above:
Nitro v1.2, Nitro Max v1.1, Nitro Pro v1.2, Surge v1.2, DM7X revB, Command
v1.2, Crimson II v1.3. Plus the Alesis knowledge base articles on
[class compliance](https://support.alesis.com/support/solutions/articles/69000822838-alesis-drums-connecting-your-kit-to-a-computer)
and the
[Crimson II hi-hat note](https://support.alesis.com/support/solutions/articles/69000827353-alesis-crimson-ii-mapping-the-crimson-ii-hi-hat-in-logic-pro-x).
