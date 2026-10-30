// PyGamer SPX -- a Yamaha SPX90 II emulation on the Adafruit PyGamer.
//
// All DSP lives in spx_core.h (the same code host_test.cpp verifies
// on a PC). This file is only hardware glue: Arcada screen / buttons /
// stick, stereo DAC audio on a TC4 timer interrupt at the SPX90 II's
// real sample rate (31,250 Hz), a free-running ADC streaming the
// A9 / D3 line input, DIN MIDI on Serial1 (Feather D0/D1) + USB MIDI,
// and SD storage for the 60 user memories.
//
// The PyGamer has NO built-in mic or line-in: the input is the little
// 1.65 V bias circuit on the D3 (A9) JST connector from WIRING.md --
// the same circuit the MPC build uses, but here it runs as a LIVE
// stream: bias is measured at boot, tracked slowly (clamped) in the
// interrupt, and a ~7 Hz DC blocker in the core eats what's left.
//
// Screens (SELECT cycles, footers on every screen say what does what):
//   PROGRAM  SPX front panel: big memory number, LCD, LED meter
//   PARAM    edit the current program's parameters + balance
//   STORE    write the current program to a user slot (31-90) + SD
//   UTIL     MIDI channel, input gain, source, output level, SD save
// START = bypass everywhere. A = trigger (freeze / pan) on PROGRAM.

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Arcada.h>
#include <Adafruit_TinyUSB.h>
#include <SdFat.h>
#include "spx_core.h"

#define INPUT_PIN A9    // D3 JST connector on the back (analog in).
                        // Feather-header alternative: A3 (see WIRING.md)

Adafruit_Arcada arcada;
SdFat sd;
bool sd_ok = false;
Adafruit_USBD_MIDI usb_midi;

static uint8_t user_img[SPX_USER_SIZE];
static char status_msg[44] = "ready";
static uint32_t status_ms = 0;
static void set_status(const char *s) { strncpy(status_msg, s, sizeof(status_msg) - 1); status_msg[sizeof(status_msg) - 1] = 0; status_ms = millis(); }

// ------------------------------------------------------------ audio in
// A9 on the PyGamer is PB02 = ADC0 channel 14 (see the variant file:
// PIN_ATTR_ANALOG -> ADC0, ADC_Channel14). The Adafruit core clocks
// ADC0 from 48 MHz with CTRLA prescaler DIV32 (wiring.c), so a
// free-running 12-bit conversion completes roughly every 9-14 us --
// comfortably faster than our 32 us sample period. The ISR simply
// reads the latest RESULT; if no fresh conversion has landed it
// reuses the previous sample (inaudible at these rates).
static uint8_t adc_chan = 14;
static volatile int32_t adc_last = 2048;
static float bias_boot = 2048.0f;      // measured at boot (counts)
static float bias_track = 2048.0f;     // slow tracker (counts, clamped)

static void adc_hw_setup(void) {
  while (ADC0->SYNCBUSY.reg & ADC_SYNCBUSY_INPUTCTRL) ;
  ADC0->INPUTCTRL.bit.MUXPOS = adc_chan;
  while (ADC0->SYNCBUSY.reg & ADC_SYNCBUSY_CTRLB) ;
  ADC0->CTRLB.bit.FREERUN = 1;
  while (ADC0->SYNCBUSY.reg & ADC_SYNCBUSY_ENABLE) ;
  ADC0->CTRLA.bit.ENABLE = 1;
  while (ADC0->SYNCBUSY.reg & ADC_SYNCBUSY_ENABLE) ;
  ADC0->SWTRIG.bit.START = 1;          // free-run: one start, runs forever
}
static void adc_setup(void) {
  adc_chan = (uint8_t)g_APinDescription[INPUT_PIN].ulADCChannelNumber;
  pinMode(INPUT_PIN, INPUT);
  analogReadResolution(12);
  (void)analogRead(INPUT_PIN);         // let the core do its one-shot config
  // measure the resting bias point of the line-in circuit (~2048)
  int32_t acc = 0;
  for (int i = 0; i < 64; i++) { acc += analogRead(INPUT_PIN); delayMicroseconds(50); }
  bias_boot = bias_track = (float)(acc / 64);
  adc_last = (int32_t)bias_boot;
  adc_hw_setup();
}
static inline int32_t adc_latest(void) {
  if (ADC0->INTFLAG.bit.RESRDY) adc_last = (int32_t)ADC0->RESULT.reg;
  return adc_last;
}

// ------------------------------------------------------------ audio engine
// TC4 via Arcada's timerCallback at exactly SPX_SR (31250 Hz).
// In: free-running ADC0 on A9. Out: L -> DAC1 (A1), R -> DAC0 (A0);
// the speaker amp is mono and follows the A0 side.
static volatile uint32_t audio_frames = 0;
static void audio_isr(void) {
  int32_t raw = adc_latest();
  // slow bias tracking, clamped to +-200 counts of the boot value so
  // one-sided program material can't drag the operating point away
  bias_track += ((float)raw - bias_track) * 0.00002f;
  if (bias_track > bias_boot + 200.0f) bias_track = bias_boot + 200.0f;
  if (bias_track < bias_boot - 200.0f) bias_track = bias_boot - 200.0f;
  int32_t in = (int32_t)(((float)raw - bias_track) * 16.0f);
  if (in > 32767) in = 32767;
  if (in < -32768) in = -32768;
  int16_t l, r;
  spx_frame((int16_t)in, &l, &r);
  int32_t lo = (l >> 4) + 2048; if (lo < 0) lo = 0; if (lo > 4095) lo = 4095;
  int32_t ro = (r >> 4) + 2048; if (ro < 0) ro = 0; if (ro > 4095) ro = 4095;
  while (DAC->SYNCBUSY.bit.DATA1) ;
  DAC->DATA[1].reg = (uint16_t)lo;
  while (DAC->SYNCBUSY.bit.DATA0) ;
  DAC->DATA[0].reg = (uint16_t)ro;
  audio_frames++;
}

// ------------------------------------------------------------ SD user file
static bool save_users(void) {
  if (!sd_ok) { set_status("no SD card"); return false; }
  uint32_t n = spx_serialize_users(user_img, sizeof(user_img));
  if (!n) { set_status("serialize failed"); return false; }
  File32 f = sd.open("/SPX/USER.BIN", O_WRITE | O_CREAT | O_TRUNC);
  if (!f) { set_status("SD write failed"); return false; }
  f.write(user_img, n); f.close();
  set_status("user memories saved to SD");
  return true;
}
static bool load_users(void) {
  if (!sd_ok) return false;
  File32 f = sd.open("/SPX/USER.BIN", FILE_READ);
  if (!f) return false;
  uint32_t n = f.read(user_img, sizeof(user_img));
  f.close();
  return spx_deserialize_users(user_img, n);
}

// ------------------------------------------------------------ UI state
enum Screen { SCR_PROGRAM, SCR_PARAM, SCR_STORE, SCR_UTIL, SCR_N };
static const char *SCR_NAMES[SCR_N] = {"PROGRAM", "PARAM", "STORE", "UTIL"};
static uint8_t screen = SCR_PROGRAM;
static uint8_t param_row = 0;          // 0..count-1 params, count = BALANCE
static uint8_t store_slot = 31;
static uint8_t util_row = 0;
static bool dirty = true;

static void fmt_gain(char *buf, size_t n, float g) {   // "1.25x" without %f
  int whole = (int)g;
  int frac = (int)((g - whole) * 100.0f + 0.5f);
  if (frac >= 100) { whole++; frac -= 100; }
  snprintf(buf, n, "%d.%02dx", whole, frac);
}

static void draw_header(void) {
  auto *d = arcada.display;
  d->fillRect(0, 0, 160, 11, 0x2104);
  d->setTextSize(1); d->setTextColor(ARCADA_WHITE); d->setCursor(2, 2);
  d->print("SPX90II "); d->print(SCR_NAMES[screen]);
  d->setCursor(96, 2);
  d->print("P"); d->print(spx.cur_prog);
  if (spx.bypass) { d->setTextColor(ARCADA_RED); d->setCursor(128, 2); d->print("BYP"); }
  else { d->setTextColor(ARCADA_GREEN); d->setCursor(128, 2); d->print("FX"); }
}
static void draw_footer(const char *s) {
  auto *d = arcada.display;
  d->fillRect(0, 117, 160, 11, 0x2104);
  d->setTextSize(1); d->setTextColor(0xFFE0); d->setCursor(2, 119);
  if (millis() - status_ms < 3500 && status_msg[0]) d->print(status_msg);
  else d->print(s);
}

static void draw_program(void) {
  auto *d = arcada.display;
  // big memory number, rack-style
  d->setTextSize(4); d->setTextColor(ARCADA_YELLOW); d->setCursor(6, 16);
  d->print(spx.cur_prog);
  d->setTextSize(1); d->setTextColor(ARCADA_WHITE); d->setCursor(76, 18);
  d->print(spx.cur_prog <= SPX_ROM_PROGRAMS ? "ROM PRESET" : (spx.user_set[spx.cur_prog - 31] ? "USER MEMORY" : "USER (EMPTY=INIT)"));
  d->setCursor(76, 28); d->print("TYPE: "); d->print(ALG_NAME[spx.cur.algo]);
  d->setCursor(76, 38);
  if (spx.cur.algo == ALG_FREEZE) d->print(spx.frz_state ? "FREEZE: PLAYING" : "FREEZE: LIVE");
  else if (spx.cur.algo == ALG_PAN && spx.cur.p[0] > 0.5f) d->print("A: sweep pan");
  else d->print("31250 Hz  16ch->12bit");
  // the 16x2 LCD
  d->fillRect(4, 54, 152, 34, 0x02E0);
  d->drawRect(4, 54, 152, 34, 0x07E0);
  d->setTextColor(0xFFE0); d->setTextSize(2); d->setCursor(10, 57);
  char nm[13]; strncpy(nm, spx.cur.name, 12); nm[12] = 0;
  d->print(nm);
  d->setTextSize(1); d->setCursor(10, 78);
  if (spx_param_count() > 0) {
    const SpxParamDesc *pd = spx_param_desc(0);
    char vb[20]; spx_format_value(pd, spx.cur.p[0], vb, sizeof(vb));
    d->print(pd->name); d->print(" "); d->print(vb);
  }
  d->setCursor(104, 78);
  d->print("BAL "); d->print((int)(spx.cur.bal * 100.0f + 0.5f)); d->print("%");
  // 7-LED input meter
  int leds = spx_meter_leds();
  d->setTextColor(ARCADA_WHITE); d->setCursor(6, 96); d->print("IN");
  for (int i = 0; i < 7; i++) {
    uint16_t c = i < 4 ? ARCADA_GREEN : (i < 6 ? ARCADA_YELLOW : ARCADA_RED);
    int x = 24 + i * 19;
    if (i < leds) d->fillRect(x, 94, 16, 8, c);
    else d->drawRect(x, 94, 16, 8, 0x4208);
  }
  // source / gain line
  char gb[10]; fmt_gain(gb, sizeof(gb), spx.in_gain);
  d->setTextColor(ARCADA_WHITE); d->setCursor(6, 107);
  d->print("SRC "); d->print(SPX_SRC_NAME[spx.source]);
  d->print("  GAIN "); d->print(gb);
  d->print("  OUT "); d->print((int)(spx.out_level * 100.0f + 0.5f)); d->print("%");
  draw_footer("L/R:prog U/D:+10 A:trig START:byp");
}

static void draw_param(void) {
  auto *d = arcada.display;
  int n = spx_param_count();
  int rows = n + 1;                       // + BALANCE
  d->setTextColor(ARCADA_WHITE); d->setCursor(2, 14);
  d->print(spx.cur.name); d->print(" ("); d->print(ALG_NAME[spx.cur.algo]); d->print(")");
  int first = param_row - 6; if (first < 0) first = 0; if (first > rows - 10) first = rows - 10;
  if (first < 0) first = 0;
  for (int vi = 0; vi < 10 && first + vi < rows; vi++) {
    int i = first + vi;
    int y = 26 + vi * 9;
    if (i == param_row) d->fillRect(0, y - 1, 160, 9, 0x4208);
    d->setTextColor(i == param_row ? ARCADA_YELLOW : ARCADA_WHITE);
    d->setCursor(4, y);
    char vb[20];
    if (i < n) {
      const SpxParamDesc *pd = spx_param_desc(i);
      spx_format_value(pd, spx.cur.p[i], vb, sizeof(vb));
      d->print(pd->name);
    } else {
      snprintf(vb, sizeof(vb), "%d%%", (int)(spx.cur.bal * 100.0f + 0.5f));
      d->print("BALANCE");
    }
    d->setCursor(104, y); d->print(vb);
  }
  draw_footer("U/D:param L/R:adj A(hold):x10 B:back");
}

static void draw_store(void) {
  auto *d = arcada.display;
  d->setTextColor(ARCADA_WHITE); d->setCursor(2, 15);
  d->print("store current program:");
  d->setTextColor(ARCADA_YELLOW); d->setCursor(2, 26);
  d->print(spx.cur.name); d->print(" ("); d->print(ALG_NAME[spx.cur.algo]); d->print(")");
  d->setTextColor(ARCADA_WHITE); d->setCursor(2, 42);
  d->print("into user memory");
  d->setTextSize(3); d->setTextColor(ARCADA_YELLOW); d->setCursor(10, 54);
  d->print(store_slot);
  d->setTextSize(1); d->setTextColor(ARCADA_WHITE); d->setCursor(76, 58);
  int idx = store_slot - 31;
  if (spx.user_set[idx]) {
    d->print("holds: "); d->print(spx.user[idx].name);
    d->setCursor(76, 70); d->print("("); d->print(ALG_NAME[spx.user[idx].algo]); d->print(")");
    d->setCursor(76, 82); d->print("A overwrites it");
  } else {
    d->print("EMPTY");
  }
  d->setCursor(2, 100);
  d->print(sd_ok ? "SD ok: STORE also writes USER.BIN" : "NO SD: store is RAM-only");
  draw_footer("L/R:slot A:STORE B:back");
}

#define UTIL_NROWS 5
static void draw_util(void) {
  auto *d = arcada.display;
  char v0[10], v1[10], v3[10];
  if (spx.midi_ch == 0) snprintf(v0, sizeof(v0), "OMNI"); else snprintf(v0, sizeof(v0), "%d", spx.midi_ch);
  fmt_gain(v1, sizeof(v1), spx.in_gain);
  snprintf(v3, sizeof(v3), "%d%%", (int)(spx.out_level * 100.0f + 0.5f));
  const char *labels[UTIL_NROWS] = {"MIDI CH", "INPUT GAIN", "SOURCE", "OUT LEVEL", "SAVE USER -> SD"};
  const char *vals[UTIL_NROWS] = {v0, v1, SPX_SRC_NAME[spx.source], v3, sd_ok ? "(A to save)" : "NO SD CARD"};
  for (int i = 0; i < UTIL_NROWS; i++) {
    int y = 20 + i * 13;
    if (i == util_row) d->fillRect(0, y - 2, 160, 12, 0x4208);
    d->setTextColor(i == util_row ? ARCADA_YELLOW : ARCADA_WHITE);
    d->setCursor(6, y); d->print(labels[i]);
    d->setCursor(100, y); d->print(vals[i]);
  }
  d->setTextColor(ARCADA_WHITE); d->setCursor(6, 92);
  d->print("A9 bias: "); d->print((int)(bias_boot + 0.5f)); d->print("/4095 counts");
  d->setCursor(6, 103);
  d->print("input: D3 JST (A9), see WIRING.md");
  draw_footer("U/D:row L/R:adjust A:save B:back");
}

static void draw(void) {
  auto *d = arcada.display;
  d->fillScreen(ARCADA_BLACK);
  d->setTextSize(1);
  draw_header();
  switch (screen) {
    case SCR_PROGRAM: draw_program(); break;
    case SCR_PARAM: draw_param(); break;
    case SCR_STORE: draw_store(); break;
    case SCR_UTIL: draw_util(); break;
  }
  dirty = false;
}

// ------------------------------------------------------------ input
static uint32_t prev_btn = 0;
static uint32_t stick_last_ms = 0, stick_dir_key = 0;
static bool pressed(uint32_t now, uint32_t mask) { return (now & mask) && !(prev_btn & mask); }

static int stick_axis(int val, int axis) {   // MPC pattern: press + auto-repeat
  int dir = val > 300 ? 1 : (val < -300 ? -1 : 0);
  uint32_t key = dir == 0 ? 0 : (uint32_t)(axis * 4 + dir + 1);
  if (dir == 0) { if (stick_dir_key / 4 == (uint32_t)axis) stick_dir_key = 0; return 0; }
  uint32_t now = millis();
  if (key != stick_dir_key) { stick_dir_key = key; stick_last_ms = now; return dir; }
  if (now - stick_last_ms > 130) { stick_last_ms = now; return dir; }
  return 0;
}

static void util_adjust(int dx) {
  switch (util_row) {
    case 0: { int ch = spx.midi_ch + dx; if (ch < 0) ch = 16; if (ch > 16) ch = 0; spx.midi_ch = (uint8_t)ch; } break;
    case 1: spx.in_gain = spx_clampf(spx.in_gain + dx * 0.05f, 0.10f, 4.0f); break;
    case 2: spx.source = (uint8_t)((spx.source + dx + SPX_SRC_COUNT) % SPX_SRC_COUNT); break;
    case 3: spx.out_level = spx_clampf(spx.out_level + dx * 0.02f, 0.0f, 1.0f); break;
    default: break;
  }
}

static void handle_input(void) {
  uint32_t btn = arcada.readButtons();
  int jx = stick_axis(arcada.readJoystickX(), 0);
  int jy = stick_axis(arcada.readJoystickY(), 1);
  jy = -jy;   // PyGamer stick Y is inverted (MPC finding)

  if (pressed(btn, ARCADA_BUTTONMASK_SELECT)) { screen = (screen + 1) % SCR_N; dirty = true; }
  if (pressed(btn, ARCADA_BUTTONMASK_START)) {
    spx.bypass = !spx.bypass;
    set_status(spx.bypass ? "BYPASS: dry signal only" : "effect on");
    dirty = true;
  }
  bool a_edge = pressed(btn, ARCADA_BUTTONMASK_A);
  bool b_edge = pressed(btn, ARCADA_BUTTONMASK_B);
  bool a_held = (btn & ARCADA_BUTTONMASK_A) != 0;
  if (b_edge && screen != SCR_PROGRAM) { screen = SCR_PROGRAM; dirty = true; prev_btn = btn; return; }

  switch (screen) {
    case SCR_PROGRAM: {
      if (jx || jy) {
        int n = spx.cur_prog + jx - jy * 10;
        while (n > SPX_PROGRAMS) n -= SPX_PROGRAMS;
        while (n < 1) n += SPX_PROGRAMS;
        if (n != spx.cur_prog) { spx_load_program(n); dirty = true; }
      }
      if (a_edge) {
        spx_trigger();
        if (spx.cur.algo == ALG_FREEZE) set_status(spx.frz_state ? "FREEZE: playing loop" : "FREEZE: live");
        else set_status("triggered");
        dirty = true;
      }
      break;
    }
    case SCR_PARAM: {
      int rows = spx_param_count() + 1;
      if (jy) { param_row = (param_row + jy + rows) % rows; dirty = true; }
      if (jx) {
        int mult = a_held ? 10 : 1;
        if (param_row < spx_param_count()) {
          const SpxParamDesc *pd = spx_param_desc(param_row);
          spx_set_param(param_row, spx.cur.p[param_row] + jx * pd->step * mult);
        } else {
          spx.cur.bal = spx_clampf(spx.cur.bal + jx * 0.02f * mult, 0.0f, 1.0f);
        }
        dirty = true;
      }
      break;
    }
    case SCR_STORE: {
      if (jx) { store_slot = (uint8_t)(31 + (store_slot - 31 + jx + SPX_USER_PROGRAMS) % SPX_USER_PROGRAMS); dirty = true; }
      if (jy) { store_slot = (uint8_t)(31 + (store_slot - 31 - jy * 10 + SPX_USER_PROGRAMS * 2) % SPX_USER_PROGRAMS); dirty = true; }
      if (a_edge) {
        if (spx_store_program(store_slot)) {
          char m[44]; snprintf(m, sizeof(m), "stored -> user %d", store_slot);
          set_status(m);
          if (sd_ok) save_users();
        }
        dirty = true;
      }
      break;
    }
    case SCR_UTIL: {
      if (jy) { util_row = (util_row + jy + UTIL_NROWS) % UTIL_NROWS; dirty = true; }
      if (jx) { util_adjust(jx); dirty = true; }
      if (a_edge && util_row == 4) { save_users(); dirty = true; }
      break;
    }
  }
  prev_btn = btn;
}

// ------------------------------------------------------------ setup / loop
void setup(void) {
  usb_midi.begin();
  Serial.begin(115200);
  spx_init();

  if (!arcada.arcadaBegin()) { while (1) delay(10); }
  arcada.displayBegin();
  arcada.setBacklight(255);
  arcada.pixels.begin(); arcada.pixels.setBrightness(36); arcada.pixels.show();
  arcada.enableSpeaker(true);
  analogWriteResolution(12);
  analogWrite(A0, 2048); analogWrite(A1, 2048);   // centre both DACs
  Serial1.begin(31250);                            // DIN MIDI on Feather D1/D0

  sd_ok = sd.begin(ARCADA_SD_CS);
  if (sd_ok) {
    sd.mkdir("/SPX");
    if (load_users()) set_status("user memories loaded from SD");
    else set_status("SD ok: no user file yet");
  } else set_status("no SD card: user slots are RAM-only");

  adc_setup();                                     // bias measure + free-run ADC0
  arcada.timerCallback(SPX_SR, audio_isr);         // audio engine starts here
  draw();
}

static uint32_t last_draw_ms = 0, last_pix_ms = 0;

void loop(void) {
  // MIDI in: DIN + USB feed the same parser
  while (Serial1.available()) spx_midi_feed((uint8_t)Serial1.read());
  while (usb_midi.available()) spx_midi_feed((uint8_t)usb_midi.read());

  // self-heal: if anything reconfigured ADC0 (another analogRead on an
  // ADC0 pin would), restore the free-running stream setup
  if (ADC0->INPUTCTRL.bit.MUXPOS != adc_chan || !ADC0->CTRLB.bit.FREERUN ||
      !ADC0->CTRLA.bit.ENABLE) adc_hw_setup();

  handle_input();

  uint32_t now = millis();
  if (dirty || (screen == SCR_PROGRAM && now - last_draw_ms > 120)) {
    draw(); last_draw_ms = now;
  }

  if (now - last_pix_ms > 40) {  // NeoPixels: 5-pixel input meter bar
    last_pix_ms = now;
    int leds = spx_meter_leds();                    // 0..7
    int lit = leds * 5 / 7;
    for (int px = 0; px < 5; px++) {
      uint32_t c = 0;
      if (spx.bypass) c = (px == 0) ? arcada.pixels.Color(50, 0, 0) : 0;
      else if (px < lit) c = px < 3 ? arcada.pixels.Color(0, 60, 10)
                             : (px == 3 ? arcada.pixels.Color(60, 45, 0)
                                        : arcada.pixels.Color(70, 0, 0));
      arcada.pixels.setPixelColor(px, c);
    }
    arcada.pixels.show();
  }
  delay(2);
}
