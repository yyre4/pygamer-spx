// spx_core.h -- Yamaha SPX90 II emulation brain for the PyGamer.
//
// Pure C++ (no Arduino, no heap): the EXACT same code runs on the
// PyGamer (ATSAMD51, 120 MHz, hardware FPU) and in host_test.cpp on a
// PC, the same arrangement as the FlyBrain (flysim.h) and MPC
// (mpc_core.h) builds. The .ino only does hardware: screen, buttons,
// the free-running ADC on A9, the dual DACs, SD files, serial ports.
//
// SCOPE -- read this before believing the badge: this is a
// PARAMETER-MODEL emulation of the SPX90 II. The algorithm structures
// (Schroeder/Moorer comb+allpass reverb, tapped early reflections,
// modulated-delay chorus/flange/symphonic, granular pitch shift,
// feedback delays, gate/comp dynamics, biquad EQ), the 30 ROM program
// names/order and their parameter ranges follow the SPX90 II manual;
// Yamaha's proprietary DSP code was never published, so this is NOT
// bit-accurate and contains no Yamaha code or ROM data. It runs at
// the original's exact sample rate, 31.25 kHz, mono in / stereo out,
// and the wet path is band-limited to ~12 kHz like the real unit
// (its bypass/direct path was full-range; so is ours).
//
// Memory map (the honest constraint of a 192 KB RAM handheld):
//   main delay line  45,056 x int16 = 88 KB  (1.44 s @ 31250 Hz --
//                    the II reaches 2.0 s; RAM is the wall, see README)
//   reverb pool      4,224 x float  = ~16.5 KB (combs + allpasses)
//   modulation line  2,048 x float  = 8 KB (chorus/flange/vibrato)
//   pitch buffer     2,048 x float  = 8 KB (granular pitch shift)
//   user memories    60 programs    = ~3.6 KB
//   everything else (state, demo sources) < 3 KB

#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

#define SPX_SR            31250
#define SPX_BUF_SAMPLES   45056        // main delay line (int16)
#define SPX_REV_FLOATS    4224         // reverb pool (float)
#define SPX_MOD_SAMPLES   2048         // modulation delay line (float)
#define SPX_PITCH_SAMPLES 2048         // pitch-shift buffer (float)
#define SPX_PITCH_WIN     1024         // pitch crossfade window
#define SPX_PARAMS        8            // max editable params per program
#define SPX_ROM_PROGRAMS  30
#define SPX_USER_PROGRAMS 60
#define SPX_PROGRAMS      90           // 1..30 ROM, 31..90 user

// demo / input sources
enum { SPX_SRC_LINE = 0, SPX_SRC_PLUCK, SPX_SRC_PAD, SPX_SRC_NOISE, SPX_SRC_COUNT };
static const char *const SPX_SRC_NAME[SPX_SRC_COUNT] = {"LINE A9", "DEMO PLUCK", "DEMO PAD", "DEMO NOISE"};

// ------------------------------------------------------------------
// Algorithms. The 30 ROM programs map onto these 17 engines.
enum {
  ALG_REVERB = 0, ALG_ER, ALG_DELAY, ALG_ECHO, ALG_FLANGE, ALG_CHORUS,
  ALG_PHASE, ALG_TREMOLO, ALG_SYMPHONIC, ALG_GATEREV, ALG_NOISEGATE,
  ALG_COMP, ALG_PITCH, ALG_FREEZE, ALG_PAN, ALG_VIBRATO, ALG_EQ,
  ALG_COUNT
};
static const char *const ALG_NAME[ALG_COUNT] = {
  "REVERB", "EARLY REF", "DELAY L,R", "STEREO ECHO", "FLANGE", "CHORUS",
  "PHASING", "TREMOLO", "SYMPHONIC", "GATE REV", "NOISE GATE",
  "COMPRESSOR", "PITCH CHG", "FREEZE", "PAN", "VIBRATO", "PARAM EQ"
};

// ------------------------------------------------------------------
// Parameter descriptors: one table per algorithm so the device UI can
// edit any program generically. Units are display-only formatting.
enum { U_NONE = 0, U_SEC, U_MS, U_HZ, U_KHZ, U_PCT, U_DB, U_SEMI, U_CENT, U_MODE, U_RATIO, U_X };
struct SpxParamDesc {
  const char *name;
  float min, max, step;
  uint8_t unit;
  const char *const *modes;   // for U_MODE: value (int) indexes this
  uint8_t mode_count;
};
#define PD(n, mn, mx, st, u)        { n, mn, mx, st, u, nullptr, 0 }
#define PDM(n, arr)                 { n, 0.0f, (float)(sizeof(arr) / sizeof(arr[0]) - 1), 1.0f, U_MODE, arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])) }

static const char *const MODE_ER_TYPE[]   = {"HALL", "RANDOM", "PLATE", "REVERSE"};
static const char *const MODE_GATE[]      = {"GATE", "REVERSE", "REV+GATE"};
static const char *const MODE_PITCH[]     = {"SINGLE", "DUAL"};
static const char *const MODE_FREEZE[]    = {"A TRIG", "B NOTE"};
static const char *const MODE_PAN[]       = {"AUTO", "TRIGGERED"};
static const char *const MODE_TREM[]      = {"MONO", "STEREO"};

static const SpxParamDesc DESC_REVERB[] = {
  PD("REV TIME", 0.3f, 20.0f, 0.1f, U_SEC),
  PD("HIGH", 0.1f, 1.0f, 0.05f, U_X),
  PD("DELAY", 0.0f, 100.0f, 1.0f, U_MS),
  PD("HPF", 0.0f, 1000.0f, 10.0f, U_HZ),
  PD("LPF", 1.0f, 12.0f, 0.5f, U_KHZ),
};
static const SpxParamDesc DESC_ER[] = {
  PDM("TYPE", MODE_ER_TYPE),
  PD("ROOM SIZE", 0.5f, 10.0f, 0.1f, U_X),
  PD("LIVENESS", 0.0f, 10.0f, 0.5f, U_X),
  PD("DELAY", 0.0f, 200.0f, 1.0f, U_MS),
  PD("LPF", 1.0f, 12.0f, 0.5f, U_KHZ),
};
static const SpxParamDesc DESC_DELAY[] = {
  PD("DLY L", 0.0f, 1400.0f, 1.0f, U_MS),
  PD("DLY R", 0.0f, 1400.0f, 1.0f, U_MS),
  PD("FDBK", 0.0f, 90.0f, 1.0f, U_PCT),
  PD("LPF", 1.0f, 12.0f, 0.5f, U_KHZ),
};
static const SpxParamDesc DESC_ECHO[] = {
  PD("DLY TIME", 1.0f, 1400.0f, 1.0f, U_MS),
  PD("FDBK", 0.0f, 90.0f, 1.0f, U_PCT),
  PD("LPF", 1.0f, 12.0f, 0.5f, U_KHZ),
  PD("SPREAD", 0.0f, 100.0f, 5.0f, U_PCT),
};
static const SpxParamDesc DESC_FLANGE[] = {
  PD("MOD FREQ", 0.1f, 20.0f, 0.1f, U_HZ),
  PD("MOD DEPTH", 0.0f, 100.0f, 1.0f, U_PCT),
  PD("DELAY", 0.1f, 10.0f, 0.1f, U_MS),
  PD("FDBK", 0.0f, 95.0f, 1.0f, U_PCT),
};
static const SpxParamDesc DESC_CHORUS[] = {
  PD("MOD FREQ", 0.1f, 20.0f, 0.1f, U_HZ),
  PD("MOD DEPTH", 0.0f, 100.0f, 1.0f, U_PCT),
  PD("DELAY", 1.0f, 40.0f, 0.5f, U_MS),
  PD("FDBK", 0.0f, 60.0f, 1.0f, U_PCT),
};
static const SpxParamDesc DESC_PHASE[] = {
  PD("MOD FREQ", 0.1f, 20.0f, 0.1f, U_HZ),
  PD("MOD DEPTH", 0.0f, 100.0f, 1.0f, U_PCT),
  PD("FDBK", 0.0f, 90.0f, 1.0f, U_PCT),
};
static const SpxParamDesc DESC_TREMOLO[] = {
  PD("FREQ", 0.1f, 20.0f, 0.1f, U_HZ),
  PD("DEPTH", 0.0f, 100.0f, 1.0f, U_PCT),
  PDM("MODE", MODE_TREM),
};
static const SpxParamDesc DESC_SYMPHONIC[] = {
  PD("MOD FREQ", 0.1f, 20.0f, 0.1f, U_HZ),
  PD("MOD DEPTH", 0.0f, 100.0f, 1.0f, U_PCT),
  PD("DELAY", 1.0f, 40.0f, 0.5f, U_MS),
};
static const SpxParamDesc DESC_GATEREV[] = {
  PDM("MODE", MODE_GATE),
  PD("REV TIME", 0.3f, 10.0f, 0.1f, U_SEC),
  PD("GATE TIME", 0.05f, 2.0f, 0.05f, U_SEC),
  PD("HIGH", 0.1f, 1.0f, 0.05f, U_X),
  PD("DELAY", 0.0f, 100.0f, 1.0f, U_MS),
  PD("LPF", 1.0f, 12.0f, 0.5f, U_KHZ),
};
static const SpxParamDesc DESC_NOISEGATE[] = {
  PD("THRESH", -60.0f, 0.0f, 1.0f, U_DB),
  PD("ATTACK", 0.0f, 100.0f, 1.0f, U_MS),
  PD("HOLD", 0.0f, 1000.0f, 10.0f, U_MS),
  PD("RELEASE", 10.0f, 2000.0f, 10.0f, U_MS),
  PD("RANGE", -80.0f, 0.0f, 1.0f, U_DB),
};
static const SpxParamDesc DESC_COMP[] = {
  PD("THRESH", -40.0f, 0.0f, 1.0f, U_DB),
  PD("RATIO", 1.0f, 20.0f, 0.5f, U_RATIO),
  PD("ATTACK", 0.1f, 100.0f, 0.5f, U_MS),
  PD("RELEASE", 10.0f, 1000.0f, 10.0f, U_MS),
  PD("GAIN", 0.0f, 24.0f, 0.5f, U_DB),
};
static const SpxParamDesc DESC_PITCH[] = {
  PDM("MODE", MODE_PITCH),
  PD("PITCH A", -12.0f, 12.0f, 1.0f, U_SEMI),
  PD("FINE A", -100.0f, 100.0f, 1.0f, U_CENT),
  PD("PITCH B", -12.0f, 12.0f, 1.0f, U_SEMI),
  PD("FINE B", -100.0f, 100.0f, 1.0f, U_CENT),
  PD("FDBK", 0.0f, 90.0f, 1.0f, U_PCT),
  PD("DELAY", 0.0f, 500.0f, 5.0f, U_MS),
  PD("LEVEL B", 0.0f, 100.0f, 1.0f, U_PCT),
};
static const SpxParamDesc DESC_FREEZE[] = {
  PDM("MODE", MODE_FREEZE),
  PD("REC TIME", 0.1f, 1.4f, 0.05f, U_SEC),
  PD("START", 0.0f, 90.0f, 1.0f, U_PCT),
  PD("END", 10.0f, 100.0f, 1.0f, U_PCT),
  PD("PITCH", -12.0f, 12.0f, 1.0f, U_SEMI),
  PD("FINE", -100.0f, 100.0f, 1.0f, U_CENT),
};
static const SpxParamDesc DESC_PAN[] = {
  PDM("MODE", MODE_PAN),
  PD("FREQ", 0.1f, 20.0f, 0.1f, U_HZ),
  PD("DEPTH", 0.0f, 100.0f, 1.0f, U_PCT),
  PD("SWEEP", 0.1f, 4.0f, 0.1f, U_SEC),
};
static const SpxParamDesc DESC_VIBRATO[] = {
  PD("MOD FREQ", 0.1f, 20.0f, 0.1f, U_HZ),
  PD("MOD DEPTH", 0.0f, 100.0f, 1.0f, U_PCT),
  PD("DELAY", 1.0f, 40.0f, 0.5f, U_MS),
};
static const SpxParamDesc DESC_EQ[] = {
  PD("L FREQ", 32.0f, 2000.0f, 8.0f, U_HZ),
  PD("L GAIN", -12.0f, 12.0f, 0.5f, U_DB),
  PD("M FREQ", 200.0f, 8000.0f, 40.0f, U_HZ),
  PD("M GAIN", -12.0f, 12.0f, 0.5f, U_DB),
  PD("H FREQ", 1000.0f, 12000.0f, 200.0f, U_HZ),
  PD("H GAIN", -12.0f, 12.0f, 0.5f, U_DB),
  PD("Q", 0.5f, 4.0f, 0.1f, U_X),
};

struct SpxDescTable { const SpxParamDesc *d; uint8_t n; };
static const SpxDescTable ALG_DESC[ALG_COUNT] = {
  { DESC_REVERB, 5 }, { DESC_ER, 5 }, { DESC_DELAY, 4 }, { DESC_ECHO, 4 },
  { DESC_FLANGE, 4 }, { DESC_CHORUS, 4 }, { DESC_PHASE, 3 }, { DESC_TREMOLO, 3 },
  { DESC_SYMPHONIC, 3 }, { DESC_GATEREV, 6 }, { DESC_NOISEGATE, 5 },
  { DESC_COMP, 5 }, { DESC_PITCH, 8 }, { DESC_FREEZE, 6 }, { DESC_PAN, 4 },
  { DESC_VIBRATO, 3 }, { DESC_EQ, 7 },
};

// ------------------------------------------------------------------
// One program: an algorithm + its parameter values + wet/dry balance.
// (The original memorized BALANCE per memory too; insert-style
// programs default to 100% wet, send-style to ~50%.)
struct SpxProgram {
  uint8_t algo;
  char    name[17];
  float   p[SPX_PARAMS];
  float   bal;          // 0.0 = all dry, 1.0 = all wet
};

// The 30 ROM programs, in the SPX90 II's factory order. Parameter
// values are plausible settings inside the manual's ranges (e.g.
// REV ROOM: 1.5 s, high ratio 0.7, 20 ms pre-delay, LPF 8 kHz).
#define RP(a, nm, b, ...) { a, nm, { __VA_ARGS__ }, b }
static const SpxProgram SPX_ROM[SPX_ROM_PROGRAMS] = {
  RP(ALG_REVERB,    "REV HALL",        0.50f, 3.2f, 0.6f, 20.0f, 0.0f, 6.0f),
  RP(ALG_REVERB,    "REV ROOM",        0.50f, 1.5f, 0.7f, 20.0f, 0.0f, 8.0f),
  RP(ALG_REVERB,    "REV VOCAL",       0.50f, 2.1f, 0.8f, 25.0f, 80.0f, 9.0f),
  RP(ALG_REVERB,    "REV PLATE",       0.50f, 1.8f, 0.9f, 10.0f, 0.0f, 10.0f),
  RP(ALG_ER,        "EARLY REF 1",     0.60f, 0.0f, 4.0f, 3.0f, 0.0f, 8.0f),
  RP(ALG_ER,        "EARLY REF 2",     0.60f, 1.0f, 6.0f, 6.0f, 10.0f, 7.0f),
  RP(ALG_DELAY,     "DELAY L,R",       0.55f, 280.0f, 310.0f, 30.0f, 9.0f),
  RP(ALG_ECHO,      "STEREO ECHO",     0.55f, 375.0f, 35.0f, 8.0f, 40.0f),
  RP(ALG_FLANGE,    "ST FLANGE A",     0.70f, 0.4f, 60.0f, 1.2f, 45.0f),
  RP(ALG_FLANGE,    "ST FLANGE B",     0.70f, 0.9f, 80.0f, 2.5f, 65.0f),
  RP(ALG_CHORUS,    "CHORUS A",        0.65f, 0.8f, 45.0f, 18.0f, 0.0f),
  RP(ALG_CHORUS,    "CHORUS B",        0.65f, 1.6f, 65.0f, 22.0f, 10.0f),
  RP(ALG_PHASE,     "STEREO PHASING",  0.70f, 0.6f, 70.0f, 30.0f),
  RP(ALG_TREMOLO,   "TREMOLO",         1.00f, 4.5f, 70.0f, 1.0f),
  RP(ALG_SYMPHONIC, "SYMPHONIC",       0.70f, 0.7f, 50.0f, 20.0f),
  RP(ALG_GATEREV,   "GATE REVERB",     0.60f, 0.0f, 2.6f, 0.45f, 0.7f, 10.0f, 8.0f),
  RP(ALG_GATEREV,   "REVERSE GATE",    0.60f, 1.0f, 2.2f, 0.60f, 0.7f, 10.0f, 8.0f),
  RP(ALG_NOISEGATE, "ADR NOISE GATE",  1.00f, -30.0f, 2.0f, 120.0f, 160.0f, -60.0f),
  RP(ALG_COMP,      "COMPRESSOR",      1.00f, -18.0f, 3.0f, 5.0f, 180.0f, 4.0f),
  RP(ALG_GATEREV,   "REVERB & GATE",   0.55f, 2.0f, 3.4f, 0.90f, 0.6f, 15.0f, 7.0f),
  RP(ALG_PITCH,     "PITCH CHANGE A",  0.60f, 0.0f, 0.0f, 25.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
  RP(ALG_PITCH,     "PITCH CHANGE B",  0.60f, 1.0f, 0.0f, 8.0f, 0.0f, -8.0f, 15.0f, 10.0f, 70.0f),
  RP(ALG_PITCH,     "PITCH CHANGE C",  0.60f, 1.0f, 4.0f, 0.0f, 7.0f, 0.0f, 0.0f, 0.0f, 80.0f),
  RP(ALG_PITCH,     "PITCH CHANGE D",  0.60f, 0.0f, 12.0f, 0.0f, 0.0f, 0.0f, 45.0f, 0.0f, 0.0f),
  RP(ALG_FREEZE,    "FREEZE A",        1.00f, 0.0f, 1.0f, 0.0f, 100.0f, 0.0f, 0.0f),
  RP(ALG_FREEZE,    "FREEZE B",        1.00f, 1.0f, 1.4f, 0.0f, 100.0f, 0.0f, 0.0f),
  RP(ALG_PAN,       "AUTO PAN",        1.00f, 0.0f, 0.8f, 90.0f, 1.0f),
  RP(ALG_PAN,       "TRIGGERED PAN",   1.00f, 1.0f, 1.0f, 100.0f, 0.8f),
  RP(ALG_VIBRATO,   "DELAY VIBRATO",   1.00f, 5.5f, 55.0f, 8.0f),
  RP(ALG_EQ,        "PARAMETRIC EQ",   1.00f, 100.0f, 0.0f, 1000.0f, 0.0f, 6300.0f, 0.0f, 1.1f),
};
#undef RP

// ------------------------------------------------------------------
// Buffers + state
static int16_t spx_buf[SPX_BUF_SAMPLES];       // main delay line
static float   spx_rev[SPX_REV_FLOATS];        // reverb pool
static float   spx_mod[SPX_MOD_SAMPLES];       // modulation line
static float   spx_pbuf[SPX_PITCH_SAMPLES];    // pitch buffer

// reverb pool layout (floats): 4 combs, then allpass L x2, allpass R x2
static const uint16_t REV_COMB_LEN[4] = {619, 673, 787, 911};
static const uint16_t REV_AP_L[2] = {337, 211};
static const uint16_t REV_AP_R[2] = {293, 181};
#define REV_COMB_OFF(i) (REV_COMB_LEN[0] * (i > 0) + REV_COMB_LEN[1] * (i > 1) + \
                         REV_COMB_LEN[2] * (i > 2) + REV_COMB_LEN[3] * (i > 3))

struct SpxBiquad { float b0, b1, b2, a1, a2, x1, x2, y1, y2; };

struct SpxState {
  SpxProgram cur;                 // working copy (edited live)
  uint8_t  cur_prog;              // 1..90
  SpxProgram user[SPX_USER_PROGRAMS];
  uint8_t  user_set[SPX_USER_PROGRAMS];

  // globals
  float    in_gain;               // 0.1 .. 4.0
  float    out_level;             // 0 .. 1
  bool     bypass;
  uint8_t  midi_ch;               // 0 = omni, else 1..16
  uint8_t  source;                // SPX_SRC_*

  // delay line
  uint32_t wpos;                  // next write index in spx_buf
  uint32_t mwpos;                 // next write index in spx_mod
  uint32_t pwpos;                 // next write index in spx_pbuf

  // DC blocker (one-pole HP ~7 Hz) -- the A9 input rides on a 1.65 V
  // bias; between this and the glue's slow bias tracker, drift and
  // resistor tolerance never reach the effects.
  float    dc_x1, dc_y1;

  // meter (input, pre-effect, like the original's LED meter)
  float    meter_peak;

  // shared LFOs
  float    lfo_ph, lfo_ph2;

  // reverb derived + state
  float    rev_fb[4], rev_damp, rev_gain;
  uint32_t rev_pre;               // pre-delay, samples
  float    rev_lp_c, rev_hp_c;    // wet filter coefficients
  uint16_t rev_idx[4];
  float    rev_filt[4];           // comb damping filter states
  uint16_t ap_idx[4];             // L0,L1,R0,R1 allpass indices
  float    rev_lp_l, rev_lp_r, rev_hp_l, rev_hp_r, rev_hp_xl, rev_hp_xr;

  // gate-reverb envelope
  uint8_t  gate_state;            // 0 idle, 1 open/decaying
  uint32_t gate_t;                // samples since trigger
  uint32_t gate_len;              // gate time, samples
  bool     gate_was_below;

  // generic feedback-delay derived (DELAY / ECHO)
  uint32_t dly_l, dly_r;
  float    dly_fb, dly_lp_c, dly_lp_s;

  // modulation derived (FLANGE / CHORUS / VIBRATO / SYMPHONIC)
  float    mod_rate, mod_depth, mod_base, mod_fb;
  float    mod_lp_s;              // flange feedback damping state

  // phaser state: 4 first-order allpass stages per channel
  float    ph_x1[2][4], ph_y1[2][4], ph_fb[2];
  float    ph_rate, ph_depth, ph_fbg;

  // tremolo / pan
  float    trm_rate, trm_depth;
  float    pan_rate, pan_depth, pan_pos;
  uint32_t pan_sweep_len, pan_sweep_t;
  bool     pan_sweeping;

  // dynamics (NOISE GATE / COMPRESSOR), shared envelope follower
  float    dyn_env, dyn_gain, dyn_thresh, dyn_ratio, dyn_floor;
  float    dyn_att_c, dyn_rel_c, dyn_makeup;
  uint32_t dyn_hold, dyn_hold_t;

  // pitch shift: two granular voices over spx_pbuf
  float    pit_d[2];              // sawtooth delay phase per voice
  float    pit_ratio[2];
  float    pit_fb;
  uint32_t pit_delay;             // extra wet delay (main line), samples
  bool     pit_dual;
  float    pit_lvl_b;

  // freeze
  uint8_t  frz_state;             // 0 = live (ring recording), 1 = playing loop
  uint32_t frz_start, frz_len;    // segment in spx_buf
  float    frz_pos;               // playback position (samples into segment)
  float    frz_ratio;
  bool     frz_note_mode;

  // EQ
  SpxBiquad eq[3];

  // ER derived: 6 taps
  uint32_t er_dly[6];
  float    er_gl[6], er_gr[6], er_fb;
  float    er_lp_c, er_lp_l, er_lp_r;

  // wet-path 12 kHz bandwidth states (the II's effect bandwidth)
  float    bw_l, bw_r;

  // program-change fade (the original mutes briefly too)
  uint16_t fade;                  // samples of fade-in remaining

  // demo sources
  float    ks[320];               // Karplus-Strong string (pluck)
  uint32_t ks_idx, demo_t, demo_step_t;
  uint8_t  demo_step;
  uint32_t rng;                   // noise LFSR
  float    pad_ph[4];

  // MIDI parser
  uint8_t  mp_status, mp_data[2], mp_idx;

  bool     nan_seen;              // diagnostic: set if DSP ever NaNs
};

static SpxState spx;

// ------------------------------------------------------------------
// Small helpers
static inline float spx_clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
static inline float spx_fast_sin(float turns) {  // Bhaskara, turns 0..1
  float t = turns - floorf(turns);
  float a = t * 6.2831853f;
  bool neg = a > 3.14159265f;
  if (neg) a -= 3.14159265f;
  float y = 16.0f * a * (3.14159265f - a) / (49.348022f - 4.0f * a * (3.14159265f - a));
  return neg ? -y : y;
}
static inline float spx_clip(float x) {  // soft knee at 0.75, hard rail at 1.0
  if (x > 1.0f) return 1.0f;
  if (x < -1.0f) return -1.0f;
  float ax = x < 0 ? -x : x;
  if (ax > 0.75f) { float d = ax - 0.75f; float y = 0.75f + d - 1.333333f * d * d * d; return x < 0 ? -y : y; }
  return x;
}

// main delay line taps (delay >= 1; wpos = next write, so tap(1) is
// the most recent sample). Interpolated variant for modulation.
static inline float spx_tap(uint32_t d) {
  if (d < 1) d = 1;
  if (d > SPX_BUF_SAMPLES - 2) d = SPX_BUF_SAMPLES - 2;
  uint32_t idx = spx.wpos >= d ? spx.wpos - d : spx.wpos - d + SPX_BUF_SAMPLES;
  return spx_buf[idx] * (1.0f / 32768.0f);
}
static inline float spx_tapf(float df) {
  if (df < 1.0f) df = 1.0f;
  if (df > SPX_BUF_SAMPLES - 3) df = SPX_BUF_SAMPLES - 3;
  uint32_t d0 = (uint32_t)df;
  float fr = df - (float)d0;
  float a = spx_tap(d0), b = spx_tap(d0 + 1);
  return a + (b - a) * fr;
}
// modulation line taps (float buffer, same convention)
static inline float spx_mtap(uint32_t d) {
  if (d < 1) d = 1;
  if (d > SPX_MOD_SAMPLES - 2) d = SPX_MOD_SAMPLES - 2;
  uint32_t idx = spx.mwpos >= d ? spx.mwpos - d : spx.mwpos - d + SPX_MOD_SAMPLES;
  return spx_mod[idx];
}
static inline float spx_mtapf(float df) {
  if (df < 1.0f) df = 1.0f;
  if (df > SPX_MOD_SAMPLES - 3) df = SPX_MOD_SAMPLES - 3;
  uint32_t d0 = (uint32_t)df;
  float fr = df - (float)d0;
  float a = spx_mtap(d0), b = spx_mtap(d0 + 1);
  return a + (b - a) * fr;
}
// pitch buffer tap (delay relative to pitch write head)
static inline float spx_ptapf(float df) {
  if (df < 0.0f) df = 0.0f;
  if (df > SPX_PITCH_SAMPLES - 3) df = SPX_PITCH_SAMPLES - 3;
  uint32_t d0 = (uint32_t)df;
  float fr = df - (float)d0;
  // delay d means "d samples ago": index = pwpos - d (wrapping)
  uint32_t i0 = spx.pwpos >= d0 ? spx.pwpos - d0 : spx.pwpos - d0 + SPX_PITCH_SAMPLES;
  uint32_t d1 = d0 + 1;
  uint32_t i1 = spx.pwpos >= d1 ? spx.pwpos - d1 : spx.pwpos - d1 + SPX_PITCH_SAMPLES;
  return spx_pbuf[i0] + (spx_pbuf[i1] - spx_pbuf[i0]) * fr;
}

// ------------------------------------------------------------------
// Demo sources (generated in-core so the host test renders them too,
// and the box makes sound with nothing plugged into A9).
static const float PLUCK_NOTES[8] = {110.0f, 130.81f, 164.81f, 196.0f,
                                     164.81f, 146.83f, 130.81f, 98.0f};
static float spx_demo_sample(void) {
  SpxState &S = spx;
  S.demo_t++;
  switch (S.source) {
    case SPX_SRC_PLUCK: {
      // Karplus-Strong plucked string, a new note every 0.42 s
      if (S.demo_t - S.demo_step_t > (uint32_t)(SPX_SR * 0.42f)) {
        S.demo_step_t = S.demo_t;
        float f = PLUCK_NOTES[S.demo_step & 7]; S.demo_step++;
        uint32_t period = (uint32_t)(SPX_SR / f);
        if (period > 319) period = 319;
        for (uint32_t i = 0; i < period; i++) {
          S.rng ^= S.rng << 13; S.rng ^= S.rng >> 17; S.rng ^= S.rng << 5;
          S.ks[i] = (float)(int32_t)S.rng / 2147483648.0f;
        }
        // silence the rest of the string buffer so stale periods don't ring
        for (uint32_t i = period; i < 320; i++) S.ks[i] = 0.0f;
        S.ks_idx = 0;
        // remember period in ks[319] side channel via demo_step high bits:
        S.pad_ph[3] = (float)period;
      }
      uint32_t period = (uint32_t)S.pad_ph[3];
      if (period < 8) period = 142;
      float cur = S.ks[S.ks_idx];
      uint32_t nxt = S.ks_idx + 1 >= period ? 0 : S.ks_idx + 1;
      S.ks[S.ks_idx] = 0.996f * 0.5f * (cur + S.ks[nxt]);
      S.ks_idx = nxt;
      return cur * 0.8f;
    }
    case SPX_SRC_PAD: {
      // slow synth pad: Am - F - C - G, sine stack with gentle swell
      static const float ROOTS[4] = {110.0f, 87.31f, 130.81f, 98.0f};
      if (S.demo_t - S.demo_step_t > (uint32_t)(SPX_SR * 2.0f)) {
        S.demo_step_t = S.demo_t; S.demo_step++;
      }
      float root = ROOTS[S.demo_step & 3];
      float ratios[3] = {1.0f, 1.5f, 2.0f};
      float out = 0;
      for (int i = 0; i < 3; i++) {
        S.pad_ph[i] += root * ratios[i] * (1.0f + 0.0015f * i) / (float)SPX_SR;
        if (S.pad_ph[i] >= 1.0f) S.pad_ph[i] -= 1.0f;
        out += spx_fast_sin(S.pad_ph[i]);
      }
      float swell = 0.75f + 0.25f * spx_fast_sin((float)(S.demo_t % (SPX_SR * 4)) / (float)(SPX_SR * 4));
      return out * 0.22f * swell;
    }
    case SPX_SRC_NOISE: {
      // decaying noise burst every 0.8 s (reverb/gate demo fodder)
      uint32_t ph = S.demo_t % (uint32_t)(SPX_SR * 0.8f);
      S.rng ^= S.rng << 13; S.rng ^= S.rng >> 17; S.rng ^= S.rng << 5;
      float nz = (float)(int32_t)S.rng / 2147483648.0f;
      float env = ph < (uint32_t)(SPX_SR * 0.3f) ? expf(-(float)ph / (SPX_SR * 0.09f)) : 0.0f;
      return nz * env * 0.85f;
    }
    default: return 0.0f;
  }
}

// ------------------------------------------------------------------
// Derived parameters: recomputed whenever a program loads or a
// parameter changes, so the per-sample code stays lean.
#define REV_AP_OFF_L0 (619 + 673 + 787 + 911)
#define REV_AP_OFF_L1 (REV_AP_OFF_L0 + 337)
#define REV_AP_OFF_R0 (REV_AP_OFF_L1 + 211)
#define REV_AP_OFF_R1 (REV_AP_OFF_R0 + 293)

static float spx_db2lin(float db) { return powf(10.0f, db * 0.05f); }
static float spx_ms2samp(float ms) { return ms * (SPX_SR / 1000.0f); }
static float spx_lp_coef(float hz) {  // one-pole lowpass coefficient
  if (hz >= 20000.0f) return 1.0f;
  return 1.0f - expf(-6.2831853f * hz / (float)SPX_SR);
}
static float spx_env_coef(float ms) {  // envelope follower coefficient
  if (ms <= 0.05f) return 1.0f;
  return 1.0f - expf(-1.0f / (ms * 0.001f * SPX_SR));
}

static void spx_setup_reverb(float rtime, float high, float predelay_ms,
                             float hpf_hz, float lpf_khz) {
  SpxState &S = spx;
  for (int i = 0; i < 4; i++) {
    // per-comb feedback for an even T60 across comb lengths
    float fb = powf(10.0f, -3.0f * (float)REV_COMB_LEN[i] / (rtime * (float)SPX_SR));
    S.rev_fb[i] = spx_clampf(fb, 0.0f, 0.995f);
  }
  S.rev_damp = spx_clampf(1.05f - high, 0.05f, 0.92f);  // high ratio -> less damping
  S.rev_pre = (uint32_t)spx_ms2samp(predelay_ms);
  S.rev_lp_c = spx_lp_coef(lpf_khz * 1000.0f);
  S.rev_hp_c = hpf_hz <= 1.0f ? 0.0f : spx_lp_coef(hpf_hz);
  S.rev_gain = 0.50f;
}

static void spx_biquad_peaking(SpxBiquad *bq, float freq, float gain_db, float q) {
  freq = spx_clampf(freq, 20.0f, 14000.0f);
  float A = spx_db2lin(gain_db * 0.5f);   // sqrt of linear gain
  float w = 6.2831853f * freq / (float)SPX_SR;
  float cw = cosf(w), sw = sinf(w);
  float alpha = sw / (2.0f * q);
  float a0 = 1.0f + alpha / A;
  bq->b0 = (1.0f + alpha * A) / a0;
  bq->b1 = (-2.0f * cw) / a0;
  bq->b2 = (1.0f - alpha * A) / a0;
  bq->a1 = (-2.0f * cw) / a0;
  bq->a2 = (1.0f - alpha / A) / a0;
}
static inline float spx_biquad_run(SpxBiquad *bq, float x) {
  float y = bq->b0 * x + bq->b1 * bq->x1 + bq->b2 * bq->x2
          - bq->a1 * bq->y1 - bq->a2 * bq->y2;
  bq->x2 = bq->x1; bq->x1 = x; bq->y2 = bq->y1; bq->y1 = y;
  return y;
}

// ER tap patterns (fractions of the room-size unit) per TYPE.
static const float ER_TAPS[4][6] = {
  {0.13f, 0.27f, 0.41f, 0.58f, 0.79f, 1.00f},   // HALL
  {0.09f, 0.23f, 0.38f, 0.52f, 0.74f, 1.00f},   // RANDOM
  {0.07f, 0.17f, 0.29f, 0.44f, 0.66f, 1.00f},   // PLATE
  {0.17f, 0.34f, 0.50f, 0.66f, 0.83f, 1.00f},   // REVERSE
};

static void spx_params_changed(void) {
  SpxState &S = spx;
  const float *p = S.cur.p;
  switch (S.cur.algo) {
    case ALG_REVERB:
      spx_setup_reverb(p[0], p[1], p[2], p[3], p[4]);
      break;
    case ALG_GATEREV:
      spx_setup_reverb(p[1], p[3], p[4], 0.0f, p[5]);
      S.gate_len = (uint32_t)(p[2] * SPX_SR);
      if (S.gate_len < 64) S.gate_len = 64;
      break;
    case ALG_ER: {
      int type = (int)p[0]; if (type < 0) type = 0; if (type > 3) type = 3;
      float unit = p[1] * 22.0f;                 // ms at size 1.0 per unit fraction
      float base = spx_ms2samp(p[3]);            // initial delay
      float liv = p[2] * 0.1f;                   // 0..1
      for (int i = 0; i < 6; i++) {
        float dms = ER_TAPS[type][i] * unit + p[3];
        S.er_dly[i] = (uint32_t)spx_ms2samp(dms) + 1;
        float g;
        if (type == 3) g = 0.25f + 0.75f * ((float)i / 5.0f);        // REVERSE swells
        else g = powf(0.72f, (float)i * (1.15f - 0.75f * liv));      // liveness flattens decay
        float pan = (i & 1) ? 0.75f : -0.75f;                        // alternate taps L/R
        if (i == 0) pan = -0.3f;
        if (i == 5) pan = 0.3f;
        // simple stereo spread: panned taps favour their own side
        S.er_gl[i] = g * (1.0f - (pan > 0 ? pan * 0.45f : 0.0f));
        S.er_gr[i] = g * (1.0f - (pan < 0 ? -pan * 0.45f : 0.0f));
        (void)base;
      }
      S.er_fb = liv * 0.42f;
      S.er_lp_c = spx_lp_coef(p[4] * 1000.0f);
      break;
    }
    case ALG_DELAY:
      S.dly_l = (uint32_t)spx_ms2samp(p[0]) + 1;
      S.dly_r = (uint32_t)spx_ms2samp(p[1]) + 1;
      S.dly_fb = p[2] * 0.01f;
      S.dly_lp_c = spx_lp_coef(p[3] * 1000.0f);
      break;
    case ALG_ECHO:
      S.dly_l = (uint32_t)spx_ms2samp(p[0]) + 1;
      S.dly_r = (uint32_t)spx_ms2samp(p[0] * (1.0f - 0.055f * p[3] * 0.01f)) + 1;
      S.dly_fb = p[1] * 0.01f;
      S.dly_lp_c = spx_lp_coef(p[2] * 1000.0f);
      break;
    case ALG_FLANGE:
      S.mod_rate = p[0];
      S.mod_depth = p[1] * 0.01f * spx_ms2samp(2.2f);
      S.mod_base = spx_ms2samp(p[2]) + spx_ms2samp(1.0f);
      S.mod_fb = p[3] * 0.01f;
      break;
    case ALG_CHORUS:
      S.mod_rate = p[0];
      S.mod_depth = p[1] * 0.01f * spx_ms2samp(6.0f);
      S.mod_base = spx_ms2samp(p[2]);
      S.mod_fb = p[3] * 0.01f;
      break;
    case ALG_VIBRATO:
      S.mod_rate = p[0];
      S.mod_depth = p[1] * 0.01f * spx_ms2samp(5.0f);
      S.mod_base = spx_ms2samp(p[2]);
      S.mod_fb = 0.0f;
      break;
    case ALG_SYMPHONIC:
      S.mod_rate = p[0];
      S.mod_depth = p[1] * 0.01f * spx_ms2samp(5.0f);
      S.mod_base = spx_ms2samp(p[2]);
      S.mod_fb = 0.18f;
      break;
    case ALG_PHASE:
      S.ph_rate = p[0];
      S.ph_depth = p[1] * 0.01f;
      S.ph_fbg = p[2] * 0.01f;
      break;
    case ALG_TREMOLO:
      S.trm_rate = p[0];
      S.trm_depth = p[1] * 0.01f;
      break;
    case ALG_PAN:
      S.pan_rate = p[1];
      S.pan_depth = p[2] * 0.01f;
      S.pan_sweep_len = (uint32_t)(p[3] * SPX_SR);
      if (S.pan_sweep_len < 32) S.pan_sweep_len = 32;
      break;
    case ALG_NOISEGATE:
      S.dyn_thresh = spx_db2lin(p[0]);
      S.dyn_att_c = spx_env_coef(p[1] < 0.5f ? 0.5f : p[1]);
      S.dyn_hold = (uint32_t)spx_ms2samp(p[2]);
      S.dyn_rel_c = spx_env_coef(p[3]);
      S.dyn_floor = spx_db2lin(p[4]);
      S.dyn_ratio = 0.0f;                        // flag: gate behaviour
      break;
    case ALG_COMP:
      S.dyn_thresh = spx_db2lin(p[0]);
      S.dyn_ratio = p[1];
      S.dyn_att_c = spx_env_coef(p[2]);
      S.dyn_rel_c = spx_env_coef(p[3]);
      S.dyn_makeup = spx_db2lin(p[4]);
      break;
    case ALG_PITCH: {
      S.pit_dual = p[0] > 0.5f;
      float st0 = p[1] + p[2] * 0.01f;
      float st1 = p[3] + p[4] * 0.01f;
      S.pit_ratio[0] = powf(2.0f, st0 / 12.0f);
      S.pit_ratio[1] = powf(2.0f, st1 / 12.0f);
      S.pit_fb = p[5] * 0.01f;
      S.pit_delay = (uint32_t)spx_ms2samp(p[6]);
      S.pit_lvl_b = p[7] * 0.01f;
      break;
    }
    case ALG_FREEZE:
      S.frz_note_mode = p[0] > 0.5f;
      S.frz_ratio = powf(2.0f, (p[4] + p[5] * 0.01f) / 12.0f);
      break;
    case ALG_EQ:
      spx_biquad_peaking(&S.eq[0], p[0], p[1], p[6]);
      spx_biquad_peaking(&S.eq[1], p[2], p[3], p[6]);
      spx_biquad_peaking(&S.eq[2], p[4], p[5], p[6]);
      break;
    default: break;
  }
}

// ------------------------------------------------------------------
// Reverb network: 4 damped combs (mono sum) -> series allpass pair
// per channel (different lengths decorrelate L/R). Freeverb-shaped,
// tuned by ear-plausible constants, not Yamaha's coefficients.
static inline float spx_allpass(float *buf, uint16_t len, uint16_t *idx, float x) {
  float b = buf[*idx];
  float y = b - 0.65f * x;
  buf[*idx] = x + 0.65f * y;
  if (++(*idx) >= len) *idx = 0;
  return y;
}
static void spx_reverb_run(float x, float *ol, float *orr) {
  SpxState &S = spx;
  float in = spx_tap(S.rev_pre + 1);
  (void)x;
  float acc = 0.0f;
  for (int i = 0; i < 4; i++) {
    uint16_t len = REV_COMB_LEN[i];
    uint32_t off = (uint32_t)REV_COMB_OFF(i);
    float y = spx_rev[off + S.rev_idx[i]];
    S.rev_filt[i] = y * (1.0f - S.rev_damp) + S.rev_filt[i] * S.rev_damp;
    spx_rev[off + S.rev_idx[i]] = in + S.rev_filt[i] * S.rev_fb[i];
    if (++S.rev_idx[i] >= len) S.rev_idx[i] = 0;
    acc += y;
  }
  acc *= S.rev_gain;
  float l = spx_allpass(&spx_rev[REV_AP_OFF_L0], REV_AP_L[0], &S.ap_idx[0], acc);
  l = spx_allpass(&spx_rev[REV_AP_OFF_L1], REV_AP_L[1], &S.ap_idx[1], l);
  float r = spx_allpass(&spx_rev[REV_AP_OFF_R0], REV_AP_R[0], &S.ap_idx[2], acc);
  r = spx_allpass(&spx_rev[REV_AP_OFF_R1], REV_AP_R[1], &S.ap_idx[3], r);
  // wet HPF (one-pole difference) + LPF per channel
  S.rev_hp_xl += S.rev_hp_c * (l - S.rev_hp_xl);
  S.rev_hp_xr += S.rev_hp_c * (r - S.rev_hp_xr);
  float hl = S.rev_hp_c > 0.0f ? l - S.rev_hp_xl : l;
  float hr = S.rev_hp_c > 0.0f ? r - S.rev_hp_xr : r;
  S.rev_lp_l += S.rev_lp_c * (hl - S.rev_lp_l);
  S.rev_lp_r += S.rev_lp_c * (hr - S.rev_lp_r);
  *ol = S.rev_lp_l;
  *orr = S.rev_lp_r;
}

// One pitch voice: two crossfading taps, triangular windows, classic
// delay-line granular shift. d = sawtooth delay phase in samples.
static inline float spx_pitch_voice(int v) {
  SpxState &S = spx;
  float W = (float)SPX_PITCH_WIN;
  float d = S.pit_d[v];
  float d2 = d < W * 0.5f ? d + W * 0.5f : d - W * 0.5f;
  float w1 = 1.0f - fabsf(d - W * 0.5f) / (W * 0.5f);
  float w2 = 1.0f - fabsf(d2 - W * 0.5f) / (W * 0.5f);
  float out = spx_ptapf(d) * w1 + spx_ptapf(d2) * w2;
  d += 1.0f - S.pit_ratio[v];
  if (d >= W) d -= W;
  if (d < 0.0f) d += W;
  S.pit_d[v] = d;
  return out;
}

// ------------------------------------------------------------------
// Trigger: the A button / footswitch / MIDI note-on. Freeze captures,
// Triggered Pan sweeps. (Gated reverb and the noise gate are
// level-triggered from the input itself, like the originals.)
static void spx_trigger(void) {
  SpxState &S = spx;
  if (S.cur.algo == ALG_FREEZE) {
    if (S.frz_state == 1) { S.frz_state = 0; return; }   // stop loop, back to live
    float secs = S.cur.p[1];
    uint32_t len = (uint32_t)(secs * SPX_SR);
    if (len > SPX_BUF_SAMPLES - 2048) len = SPX_BUF_SAMPLES - 2048;
    if (len < 256) len = 256;
    S.frz_len = len;
    S.frz_start = S.wpos >= len ? S.wpos - len : S.wpos - len + SPX_BUF_SAMPLES;
    S.frz_pos = 0.0f;
    S.frz_state = 1;
  } else if (S.cur.algo == ALG_PAN && S.cur.p[0] > 0.5f) {
    S.pan_sweeping = true;
    S.pan_sweep_t = 0;
  }
}

// ------------------------------------------------------------------
// The per-sample processor. in = mono input sample (-1..1 range,
// before input gain). Writes wet/dry-mixed stereo out.
static void spx_process(float in_raw, float *out_l, float *out_r) {
  SpxState &S = spx;
  float x = (S.source == SPX_SRC_LINE) ? in_raw : spx_demo_sample();
  x *= S.in_gain;
  // DC blocker (~7 Hz one-pole highpass)
  float dc = x - S.dc_x1 + 0.99860f * S.dc_y1;
  S.dc_x1 = x; S.dc_y1 = dc; x = dc;
  // input meter (peak with slow decay)
  float ax = fabsf(x);
  if (ax > S.meter_peak) S.meter_peak = ax; else S.meter_peak *= 0.99988f;

  float line_in = x;              // what the main delay line records
  float wet_l = 0.0f, wet_r = 0.0f;
  float dry_gain = 1.0f - S.cur.bal;
  bool algo_dry = true;           // vibrato & freeze-playback kill dry

  if (S.bypass) {
    wet_l = wet_r = 0.0f;
    dry_gain = 1.0f;
  } else switch (S.cur.algo) {
    case ALG_REVERB:
      spx_reverb_run(x, &wet_l, &wet_r);
      break;

    case ALG_GATEREV: {
      spx_reverb_run(x, &wet_l, &wet_r);
      // level-triggered gate over the reverb tail (fast envelope)
      if (S.gate_state == 0) {
        if (S.dyn_env > 0.05f && S.gate_was_below) { S.gate_state = 1; S.gate_t = 0; }
        if (S.dyn_env <= 0.03f) S.gate_was_below = true;
      }
      float g = 0.0f;
      if (S.gate_state == 1) {
        float t = (float)S.gate_t / (float)S.gate_len;
        uint8_t mode = (uint8_t)S.cur.p[0];
        if (t >= 1.0f) { S.gate_state = 0; S.gate_was_below = false; g = 0.0f; }
        else if (mode == 0) g = 1.0f - t * t;                    // GATE: hold-ish then slam
        else if (mode == 1) g = t < 0.85f ? t / 0.85f : (1.0f - t) / 0.15f;  // REVERSE swell + cut
        else g = t < 0.55f ? 1.0f : expf(-(t - 0.55f) * 9.0f);   // REV+GATE: verb then gate decay
        S.gate_t++;
      }
      wet_l *= g; wet_r *= g;
      break;
    }

    case ALG_ER: {
      float acc_l = 0.0f, acc_r = 0.0f;
      for (int i = 0; i < 6; i++) {
        float t = spx_tap(S.er_dly[i]);
        acc_l += t * S.er_gl[i];
        acc_r += t * S.er_gr[i];
      }
      // liveness: one regenerated pass through the longest tap
      float fbv = spx_mtap(S.er_dly[5] > SPX_MOD_SAMPLES - 2 ? SPX_MOD_SAMPLES - 2 : S.er_dly[5]);
      acc_l += fbv * 0.35f; acc_r += fbv * 0.35f;
      S.er_lp_l += S.er_lp_c * (acc_l - S.er_lp_l);
      S.er_lp_r += S.er_lp_c * (acc_r - S.er_lp_r);
      wet_l = S.er_lp_l * 0.8f; wet_r = S.er_lp_r * 0.8f;
      // mod line records input + regenerated tail for the next pass
      spx_mod[S.mwpos] = x + fbv * S.er_fb;
      break;
    }

    case ALG_DELAY: {
      float tl = spx_tap(S.dly_l), tr = spx_tap(S.dly_r);
      S.dly_lp_s += S.dly_lp_c * (((tl + tr) * 0.5f) - S.dly_lp_s);
      line_in = x + S.dly_lp_s * S.dly_fb;
      wet_l = tl; wet_r = tr;
      break;
    }

    case ALG_ECHO: {
      float tl = spx_tap(S.dly_l), tr = spx_tap(S.dly_r);
      S.dly_lp_s += S.dly_lp_c * (tl - S.dly_lp_s);
      line_in = x + S.dly_lp_s * S.dly_fb;
      wet_l = tl; wet_r = tr;
      break;
    }

    case ALG_FLANGE: {
      float lfo = spx_fast_sin(S.lfo_ph);
      float lfo_r = spx_fast_sin(S.lfo_ph + 0.25f);   // quadrature R
      float dl = S.mod_base + S.mod_depth * lfo;
      float dr = S.mod_base + S.mod_depth * lfo_r;
      wet_l = spx_mtapf(dl);
      wet_r = spx_mtapf(dr);
      spx_mod[S.mwpos] = x + wet_l * S.mod_fb;
      break;
    }

    case ALG_CHORUS: {
      float lfo = spx_fast_sin(S.lfo_ph);
      float lfo_r = spx_fast_sin(S.lfo_ph + 0.5f);    // antiphase R
      wet_l = spx_mtapf(S.mod_base + S.mod_depth * lfo);
      wet_r = spx_mtapf(S.mod_base + S.mod_depth * lfo_r);
      spx_mod[S.mwpos] = x + wet_l * S.mod_fb;
      break;
    }

    case ALG_SYMPHONIC: {
      // Yamaha's Symphonic: three modulated voices, slow and wide
      float l1 = spx_fast_sin(S.lfo_ph);
      float l2 = spx_fast_sin(S.lfo_ph + 0.33f);
      float l3 = spx_fast_sin(S.lfo_ph + 0.66f);
      float c = spx_mtapf(S.mod_base + S.mod_depth * l2);
      wet_l = (spx_mtapf(S.mod_base * 0.82f + S.mod_depth * l1) + c) * 0.55f;
      wet_r = (spx_mtapf(S.mod_base * 1.18f + S.mod_depth * l3) + c) * 0.55f;
      spx_mod[S.mwpos] = x + c * S.mod_fb;
      break;
    }

    case ALG_VIBRATO: {
      float lfo = spx_fast_sin(S.lfo_ph);
      float v = spx_mtapf(S.mod_base + S.mod_depth * lfo);
      wet_l = wet_r = v;
      algo_dry = false;                              // 100% wet by design
      spx_mod[S.mwpos] = x;
      break;
    }

    case ALG_PHASE: {
      float lfo_l = spx_fast_sin(S.lfo_ph);
      float lfo_r = spx_fast_sin(S.lfo_ph + 0.5f);
      float coef_l = 0.15f + 0.75f * S.ph_depth * (lfo_l * 0.5f + 0.5f);
      float coef_r = 0.15f + 0.75f * S.ph_depth * (lfo_r * 0.5f + 0.5f);
      float in_l = x + S.ph_fb[0] * S.ph_fbg;
      float in_r = x + S.ph_fb[1] * S.ph_fbg;
      for (int i = 0; i < 4; i++) {
        float y = coef_l * in_l + S.ph_x1[0][i] - coef_l * S.ph_y1[0][i];
        S.ph_x1[0][i] = in_l; S.ph_y1[0][i] = y; in_l = y;
      }
      for (int i = 0; i < 4; i++) {
        float y = coef_r * in_r + S.ph_x1[1][i] - coef_r * S.ph_y1[1][i];
        S.ph_x1[1][i] = in_r; S.ph_y1[1][i] = y; in_r = y;
      }
      S.ph_fb[0] = in_l; S.ph_fb[1] = in_r;
      wet_l = in_l; wet_r = in_r;
      break;
    }

    case ALG_TREMOLO: {
      float lfo = spx_fast_sin(S.lfo_ph) * 0.5f + 0.5f;
      float g = 1.0f - S.trm_depth * (1.0f - lfo);
      float gr = g;
      if (S.cur.p[2] > 0.5f) {                       // STEREO: antiphase
        float lfo2 = spx_fast_sin(S.lfo_ph + 0.5f) * 0.5f + 0.5f;
        gr = 1.0f - S.trm_depth * (1.0f - lfo2);
      }
      wet_l = x * g; wet_r = x * gr;
      break;
    }

    case ALG_PAN: {
      float pan;
      if (S.cur.p[0] < 0.5f) {                       // AUTO
        pan = spx_fast_sin(S.lfo_ph) * S.pan_depth;
      } else {                                       // TRIGGERED sweep
        if (S.pan_sweeping) {
          float t = (float)S.pan_sweep_t / (float)S.pan_sweep_len;
          if (t >= 1.0f) { S.pan_sweeping = false; pan = S.pan_depth; }
          else pan = (-1.0f + 2.0f * t) * S.pan_depth;
          S.pan_sweep_t++;
        } else pan = S.pan_pos;
        S.pan_pos = pan;
      }
      wet_l = x * (pan < 0 ? 1.0f : 1.0f - pan);
      wet_r = x * (pan > 0 ? 1.0f : 1.0f + pan);
      break;
    }

    case ALG_NOISEGATE: {
      float env = S.dyn_env;
      float target = (env > S.dyn_thresh) ? 1.0f : S.dyn_floor;
      if (target > S.dyn_gain) {                     // open: attack
        S.dyn_gain += (target - S.dyn_gain) * S.dyn_att_c;
        S.dyn_hold_t = S.dyn_hold;
      } else if (S.dyn_hold_t > 0) {
        S.dyn_hold_t--;
      } else {                                       // close: release
        S.dyn_gain += (target - S.dyn_gain) * S.dyn_rel_c;
      }
      if (S.dyn_gain > 1.0f) S.dyn_gain = 1.0f;
      wet_l = wet_r = x * S.dyn_gain;
      break;
    }

    case ALG_COMP: {
      float env = S.dyn_env;
      float target = 1.0f;
      if (env > S.dyn_thresh && env > 1e-6f) {
        float over = S.dyn_thresh + (env - S.dyn_thresh) / S.dyn_ratio;
        target = over / env;
      }
      float c = target < S.dyn_gain ? S.dyn_att_c : S.dyn_rel_c;
      S.dyn_gain += (target - S.dyn_gain) * c;
      wet_l = wet_r = x * S.dyn_gain * S.dyn_makeup;
      break;
    }

    case ALG_PITCH: {
      float v0 = spx_pitch_voice(0);
      float v1 = S.pit_dual ? spx_pitch_voice(1) : 0.0f;
      float shifted = v0 + v1 * S.pit_lvl_b;
      spx_pbuf[S.pwpos] = x + shifted * S.pit_fb;
      if (S.pit_delay > 0) {
        line_in = shifted;                           // main line carries the shift
        wet_l = wet_r = spx_tap(S.pit_delay + 1);
      } else {
        wet_l = wet_r = shifted;
      }
      break;
    }

    case ALG_FREEZE: {
      if (S.frz_state == 1) {
        // play the captured segment on a loop, pitch by resample step
        uint32_t seg_start = S.frz_start, seg_len = S.frz_len;
        float trim0 = S.cur.p[2] * 0.01f, trim1 = S.cur.p[3] * 0.01f;
        uint32_t a = (uint32_t)(seg_len * trim0);
        uint32_t b = (uint32_t)(seg_len * trim1);
        if (b <= a + 32) b = a + 32;
        if (S.frz_pos >= (float)(b - a)) S.frz_pos -= (float)(b - a);
        uint32_t idx = seg_start + a + (uint32_t)S.frz_pos;
        if (idx >= SPX_BUF_SAMPLES) idx -= SPX_BUF_SAMPLES;
        wet_l = wet_r = spx_buf[idx] * (1.0f / 32768.0f);
        S.frz_pos += S.frz_ratio;
        line_in = 0.0f;                              // hold the frozen segment intact
        algo_dry = false;
      } else {
        wet_l = wet_r = x;                           // live monitor while armed
      }
      break;
    }

    case ALG_EQ: {
      float y = x;
      for (int i = 0; i < 3; i++) y = spx_biquad_run(&S.eq[i], y);
      wet_l = wet_r = y;
      break;
    }

    default:
      wet_l = wet_r = x;
      break;
  }

  // shared dynamics envelope follower (gate/comp read it next sample;
  // one sample of lag is inaudible and keeps the code simple)
  {
    float a = fabsf(x);
    float c = a > S.dyn_env ? 0.6f : 0.002f;
    S.dyn_env += (a - S.dyn_env) * c;
  }

  // wet-path bandwidth: the II's effects ran 20 Hz - 12 kHz
  const float bw_c = 0.905f;
  S.bw_l += bw_c * (wet_l - S.bw_l);
  S.bw_r += bw_c * (wet_r - S.bw_r);

  float ol = x * (algo_dry ? dry_gain : 0.0f) + S.bw_l * S.cur.bal;
  float orr = x * (algo_dry ? dry_gain : 0.0f) + S.bw_r * S.cur.bal;

  // program-change fade-in
  if (S.fade > 0) {
    float g = 1.0f - (float)S.fade / 96.0f;
    ol *= g; orr *= g;
    S.fade--;
  }

  ol = spx_clip(ol * S.out_level);
  orr = spx_clip(orr * S.out_level);
  if (ol != ol || orr != orr) { S.nan_seen = true; ol = orr = 0.0f; }

  // record into the main delay line; during freeze playback the
  // write head holds still so the captured segment stays intact
  if (!(S.cur.algo == ALG_FREEZE && S.frz_state == 1)) {
    float li = line_in;
    if (li > 1.2f) li = 1.2f;
    if (li < -1.2f) li = -1.2f;
    spx_buf[S.wpos] = (int16_t)(li * 32767.0f);
    if (++S.wpos >= SPX_BUF_SAMPLES) S.wpos = 0;
  }
  // modulation line defaults to clean input when an algo didn't write it
  if (S.cur.algo != ALG_FLANGE && S.cur.algo != ALG_CHORUS &&
      S.cur.algo != ALG_SYMPHONIC && S.cur.algo != ALG_VIBRATO &&
      S.cur.algo != ALG_ER)
    spx_mod[S.mwpos] = x;
  if (++S.mwpos >= SPX_MOD_SAMPLES) S.mwpos = 0;
  if (S.cur.algo != ALG_PITCH) {
    spx_pbuf[S.pwpos] = x;
  }
  if (++S.pwpos >= SPX_PITCH_SAMPLES) S.pwpos = 0;

  // advance LFOs
  float rate = 0.0f;
  switch (S.cur.algo) {
    case ALG_FLANGE: case ALG_CHORUS: case ALG_VIBRATO: case ALG_SYMPHONIC:
      rate = S.mod_rate; break;
    case ALG_PHASE: rate = S.ph_rate; break;
    case ALG_TREMOLO: rate = S.trm_rate; break;
    case ALG_PAN: rate = S.pan_rate; break;
    default: break;
  }
  S.lfo_ph += rate / (float)SPX_SR;
  if (S.lfo_ph >= 1.0f) S.lfo_ph -= 1.0f;

  *out_l = ol;
  *out_r = orr;
}

// ------------------------------------------------------------------
// int16 frame API (what the firmware ISR calls)
static void spx_frame(int16_t in, int16_t *l, int16_t *r) {
  float ol, orr;
  spx_process(in * (1.0f / 32768.0f), &ol, &orr);
  *l = (int16_t)(ol * 32767.0f);
  *r = (int16_t)(orr * 32767.0f);
}
// Block render for the host test: mono in (or NULL = zeros), stereo out.
static void spx_render(int16_t *stereo_out, const int16_t *mono_in, int frames) {
  for (int i = 0; i < frames; i++) {
    int16_t in = mono_in ? mono_in[i] : 0;
    spx_frame(in, &stereo_out[i * 2], &stereo_out[i * 2 + 1]);
  }
}

// input meter: number of lit LEDs, 0..7 (original-style thresholds)
static int spx_meter_leds(void) {
  static const float TH[7] = {0.03f, 0.06f, 0.12f, 0.22f, 0.38f, 0.60f, 0.85f};
  int n = 0;
  for (int i = 0; i < 7; i++) if (spx.meter_peak >= TH[i]) n = i + 1;
  return n;
}

// ------------------------------------------------------------------
// Program management
static void spx_reset_dsp(void) {
  SpxState &S = spx;
  memset(spx_rev, 0, sizeof(spx_rev));
  memset(spx_mod, 0, sizeof(spx_mod));
  memset(spx_pbuf, 0, sizeof(spx_pbuf));
  S.rev_filt[0] = S.rev_filt[1] = S.rev_filt[2] = S.rev_filt[3] = 0;
  S.rev_lp_l = S.rev_lp_r = S.rev_hp_l = S.rev_hp_r = S.rev_hp_xl = S.rev_hp_xr = 0;
  memset(S.ph_x1, 0, sizeof(S.ph_x1)); memset(S.ph_y1, 0, sizeof(S.ph_y1));
  S.ph_fb[0] = S.ph_fb[1] = 0;
  for (int i = 0; i < 3; i++) { S.eq[i].x1 = S.eq[i].x2 = S.eq[i].y1 = S.eq[i].y2 = 0; }
  S.dly_lp_s = 0; S.mod_lp_s = 0; S.er_lp_l = S.er_lp_r = 0;
  S.bw_l = S.bw_r = 0;
  S.dyn_env = 0; S.dyn_gain = (S.cur.algo == ALG_NOISEGATE) ? 0.0f : 1.0f;
  S.dyn_hold_t = 0;
  S.gate_state = 0; S.gate_t = 0; S.gate_was_below = true;
  S.pit_d[0] = 0; S.pit_d[1] = SPX_PITCH_WIN * 0.5f;
  S.frz_state = 0; S.frz_pos = 0;
  S.pan_sweeping = false; S.pan_pos = 0;
  S.lfo_ph = 0;
  S.fade = 96;                       // ~3 ms fade-in, kills change clicks
}

static void spx_init(void) {
  memset(&spx, 0, sizeof(spx));
  memset(spx_buf, 0, sizeof(spx_buf));
  spx.in_gain = 1.0f;
  spx.out_level = 0.9f;
  spx.midi_ch = 0;
  spx.source = SPX_SRC_LINE;
  spx.rng = 22222u;
  spx.dyn_gain = 1.0f;
  spx.cur = SPX_ROM[0];
  spx.cur_prog = 1;
  spx_params_changed();
  spx_reset_dsp();
}

// Load program n (1..90). Empty user slots load an INIT program.
static void spx_load_program(int n) {
  if (n < 1) n = 1;
  if (n > SPX_PROGRAMS) n = SPX_PROGRAMS;
  spx.cur_prog = (uint8_t)n;
  if (n <= SPX_ROM_PROGRAMS) {
    spx.cur = SPX_ROM[n - 1];
  } else if (spx.user_set[n - SPX_ROM_PROGRAMS - 1]) {
    spx.cur = spx.user[n - SPX_ROM_PROGRAMS - 1];
  } else {
    memset(&spx.cur, 0, sizeof(spx.cur));
    spx.cur.algo = ALG_REVERB;
    memcpy(spx.cur.name, "INIT", 5);
    spx.cur.p[0] = 1.2f; spx.cur.p[1] = 0.7f; spx.cur.p[2] = 15.0f;
    spx.cur.p[3] = 0.0f; spx.cur.p[4] = 8.0f;
    spx.cur.bal = 0.5f;
  }
  spx_params_changed();
  spx_reset_dsp();
}

// Store the working program into user slot n (31..90).
static bool spx_store_program(int n) {
  if (n <= SPX_ROM_PROGRAMS || n > SPX_PROGRAMS) return false;
  spx.user[n - SPX_ROM_PROGRAMS - 1] = spx.cur;
  spx.user_set[n - SPX_ROM_PROGRAMS - 1] = 1;
  return true;
}

static int spx_param_count(void) { return ALG_DESC[spx.cur.algo].n; }
static const SpxParamDesc *spx_param_desc(int i) {
  if (i < 0 || i >= spx_param_count()) return nullptr;
  return &ALG_DESC[spx.cur.algo].d[i];
}
static void spx_set_param(int i, float v) {
  const SpxParamDesc *d = spx_param_desc(i);
  if (!d) return;
  spx.cur.p[i] = spx_clampf(v, d->min, d->max);
  spx_params_changed();
}

// Format a parameter value (no printf %f in the core: manual fixed
// formatting, decimal places follow the descriptor step).
static void spx_strcpy(char *dst, const char *src, int cap) {
  int i = 0;
  while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
  dst[i] = 0;
}
static void spx_format_value(const SpxParamDesc *d, float v, char *buf, int cap) {
  if (d->unit == U_MODE) {
    int idx = (int)(v + 0.5f);
    if (idx < 0) idx = 0;
    if (idx >= d->mode_count) idx = d->mode_count - 1;
    spx_strcpy(buf, d->modes[idx], cap);
    return;
  }
  int dec = d->step >= 1.0f ? 0 : (d->step >= 0.1f ? 1 : 2);
  const char *unit = "";
  switch (d->unit) {
    case U_SEC: unit = "s"; break;
    case U_MS: unit = "ms"; break;
    case U_HZ: unit = "Hz"; break;
    case U_KHZ: unit = "kHz"; break;
    case U_PCT: unit = "%"; break;
    case U_DB: unit = "dB"; break;
    case U_SEMI: unit = "st"; break;
    case U_CENT: unit = "ct"; break;
    case U_RATIO: unit = ":1"; break;
    case U_X: unit = "x"; break;
    default: break;
  }
  // manual float -> text
  char tmp[24];
  int ti = 0;
  float av = v;
  if (av < 0) { tmp[ti++] = '-'; av = -av; }
  else if ((d->unit == U_SEMI || d->unit == U_CENT || d->unit == U_DB) && av > 0) tmp[ti++] = '+';
  long ip = (long)av;
  float frac = av - (float)ip;
  char rev[12]; int ri = 0;
  if (ip == 0) rev[ri++] = '0';
  while (ip > 0) { rev[ri++] = (char)('0' + (ip % 10)); ip /= 10; }
  while (ri > 0) tmp[ti++] = rev[--ri];
  if (dec > 0) {
    tmp[ti++] = '.';
    for (int i = 0; i < dec; i++) {
      frac *= 10.0f;
      int dig = (int)frac;
      if (dig > 9) dig = 9;
      tmp[ti++] = (char)('0' + dig);
      frac -= (float)dig;
    }
  }
  tmp[ti] = 0;
  int bi = 0;
  while (tmp[bi] && bi < cap - 1) { buf[bi] = tmp[bi]; bi++; }
  int ui = 0;
  while (unit[ui] && bi < cap - 1) buf[bi++] = unit[ui++];
  buf[bi] = 0;
}

// ------------------------------------------------------------------
// User-memory serialization for SD: fixed-size image of all 60 user
// programs + globals. Floats stored raw (same core both sides).
#define SPX_USER_MAGIC 0x55585053u   // 'SPXU'
#define SPX_USER_VER   1
#define SPX_USER_REC   56            // per-program record bytes
#define SPX_USER_SIZE  (16 + SPX_USER_PROGRAMS * SPX_USER_REC)

static uint32_t spx_serialize_users(uint8_t *buf, uint32_t cap) {
  if (cap < SPX_USER_SIZE) return 0;
  memset(buf, 0, SPX_USER_SIZE);
  uint32_t o = 0;
  memcpy(buf + o, "SPXU", 4); o += 4;
  buf[o++] = SPX_USER_VER; buf[o++] = 0;
  buf[o++] = spx.midi_ch; buf[o++] = spx.source;
  float g = spx.in_gain, l = spx.out_level;
  memcpy(buf + o, &g, 4); o += 4;
  memcpy(buf + o, &l, 4); o += 4;
  o = 16;
  for (int i = 0; i < SPX_USER_PROGRAMS; i++) {
    uint8_t *r = buf + o + i * SPX_USER_REC;
    r[0] = spx.user_set[i];
    r[1] = spx.user[i].algo;
    memcpy(r + 2, spx.user[i].name, 16);
    memcpy(r + 18, spx.user[i].p, sizeof(float) * SPX_PARAMS);
    memcpy(r + 50, &spx.user[i].bal, 4);
  }
  return SPX_USER_SIZE;
}
static bool spx_deserialize_users(const uint8_t *buf, uint32_t n) {
  if (n < SPX_USER_SIZE || memcmp(buf, "SPXU", 4) || buf[4] != SPX_USER_VER) return false;
  spx.midi_ch = buf[6];
  if (spx.midi_ch > 16) spx.midi_ch = 0;
  spx.source = buf[7];
  if (spx.source >= SPX_SRC_COUNT) spx.source = SPX_SRC_LINE;
  float g, l;
  memcpy(&g, buf + 8, 4); memcpy(&l, buf + 12, 4);
  if (g > 0.05f && g < 8.0f) spx.in_gain = g;
  if (l >= 0.0f && l <= 1.0f) spx.out_level = l;
  for (int i = 0; i < SPX_USER_PROGRAMS; i++) {
    const uint8_t *r = buf + 16 + i * SPX_USER_REC;
    spx.user_set[i] = r[0] ? 1 : 0;
    spx.user[i].algo = r[1] < ALG_COUNT ? r[1] : ALG_REVERB;
    memcpy(spx.user[i].name, r + 2, 16); spx.user[i].name[16] = 0;
    memcpy(spx.user[i].p, r + 18, sizeof(float) * SPX_PARAMS);
    memcpy(&spx.user[i].bal, r + 50, 4);
    if (spx.user[i].bal < 0.0f || spx.user[i].bal > 1.0f) spx.user[i].bal = 0.5f;
  }
  return true;
}

// ------------------------------------------------------------------
// MIDI input (byte parser; DIN + USB share it, like the MPC core).
// Map: program change -> program 1..90 (values 90..127 clamp);
// note-on: PITCH sets shift from note (note-60 semitones, clamped),
// FREEZE triggers playback at the note's pitch, PAN triggers a sweep;
// CC7 = output level, CC91 = balance (the SPX's effect-level CC).
static void spx_midi_note_on(uint8_t note) {
  SpxState &S = spx;
  if (S.cur.algo == ALG_PITCH) {
    float st = (float)((int)note - 60);
    if (st > 12.0f) st = 12.0f;
    if (st < -12.0f) st = -12.0f;
    S.cur.p[1] = st;
    S.cur.p[2] = 0.0f;
    spx_params_changed();
  } else if (S.cur.algo == ALG_FREEZE) {
    float st = (float)((int)note - 60);
    if (st > 24.0f) st = 24.0f;
    if (st < -24.0f) st = -24.0f;
    S.frz_ratio = powf(2.0f, st / 12.0f);
    if (S.frz_state == 0) spx_trigger();
  } else {
    spx_trigger();
  }
}
static void spx_midi_feed(uint8_t b) {
  SpxState &S = spx;
  if (b >= 0xF8) return;                       // realtime: ignored
  if (b & 0x80) {
    if (b >= 0xF0) { S.mp_status = 0; return; }  // system common / sysex: skip stream
    S.mp_status = b; S.mp_idx = 0;
    return;
  }
  if (!S.mp_status) return;
  S.mp_data[S.mp_idx++] = b;
  uint8_t type = S.mp_status & 0xF0;
  uint8_t need = (type == 0xC0 || type == 0xD0) ? 1 : 2;
  if (S.mp_idx < need) return;
  S.mp_idx = 0;
  uint8_t ch0 = S.mp_status & 0x0F;
  if (S.midi_ch != 0 && ch0 + 1 != S.midi_ch) return;
  uint8_t d1 = S.mp_data[0], d2 = S.mp_data[1];
  switch (type) {
    case 0x90: if (d2 > 0) spx_midi_note_on(d1); break;
    case 0xC0: spx_load_program(d1 + 1); break;
    case 0xB0:
      if (d1 == 7) S.out_level = d2 * (1.0f / 127.0f);
      else if (d1 == 91) S.cur.bal = d2 * (1.0f / 127.0f);
      break;
    default: break;
  }
}
