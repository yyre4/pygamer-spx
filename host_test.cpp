// host_test.cpp -- PC verification of the exact spx_core.h the firmware runs.
// Build: g++ -O2 -Wall -I pygamer_spx -o host_test host_test.cpp && ./host_test
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "spx_core.h"

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; if (!(cond)) { failures++; printf("FAIL: %s\n", msg); } \
                              else printf("ok: %s\n", msg); } while (0)

static int16_t *IN = nullptr;    // mono input scratch (8 s)
static int16_t *OUT = nullptr;   // stereo output scratch
#define MAXF (SPX_SR * 8)

static void clear_io(void) { memset(IN, 0, MAXF * 2); memset(OUT, 0, MAXF * 4); }
static void fill_tone(int start, int n, double freq, double amp, double phase = 0.0) {
  for (int i = 0; i < n && start + i < MAXF; i++)
    IN[start + i] = (int16_t)(amp * 32767.0 * sin(2 * M_PI * freq * i / SPX_SR + phase));
}
static void fill_noise(int start, int n, double amp, unsigned seed = 99) {
  srand(seed);
  for (int i = 0; i < n && start + i < MAXF; i++)
    IN[start + i] = (int16_t)(amp * 32767.0 * ((rand() % 20000) / 10000.0 - 1.0));
}
static double win_rms(const int16_t *st, int a, int b, int ch = 0) {  // stereo window RMS, one channel
  double s = 0; int n = 0;
  for (int i = a; i < b && i < MAXF; i++) { double v = st[i * 2 + ch]; s += v * v; n++; }
  return n ? sqrt(s / n) : 0;
}
static double win_rms_st(const int16_t *st, int a, int b) {
  double s = 0; int n = 0;
  for (int i = a; i < b && i < MAXF; i++) { double v = st[i * 2]; s += v * v; n++; }
  return n ? sqrt(s / n) : 0;
}
static double win_peak(const int16_t *st, int a, int b, int ch = 0) {
  double pk = 0;
  for (int i = a; i < b && i < MAXF; i++) { double v = fabs((double)st[i * 2 + ch]); if (v > pk) pk = v; }
  return pk;
}
static double measure_freq(const int16_t *st, int a, int b, int ch = 0) {  // zero-crossing estimate
  int cross = 0; int prev = 0;
  for (int i = a; i < b && i < MAXF; i++) {
    int s = st[i * 2 + ch] > 200 ? 1 : (st[i * 2 + ch] < -200 ? -1 : 0);
    if (s != 0 && prev != 0 && s != prev) cross++;
    if (s != 0) prev = s;
  }
  double secs = (double)(b - a) / SPX_SR;
  return cross / 2.0 / secs;
}
static double goertzel(const int16_t *st, int a, int n, double freq, int ch = 0) {
  double w = 2 * M_PI * freq / SPX_SR, cw = cos(w), coef = 2 * cw;
  double s0 = 0, s1 = 0, s2 = 0;
  for (int i = 0; i < n && a + i < MAXF; i++) {
    s0 = st[(a + i) * 2 + ch] + coef * s1 - s2;
    s2 = s1; s1 = s0;
  }
  return sqrt(s1 * s1 + s2 * s2 - coef * s1 * s2) / n;
}
static void render(int n) { spx_render(OUT, IN, n); }

int main(void) {
  IN = (int16_t *)calloc(MAXF, 2);
  OUT = (int16_t *)calloc(MAXF, 4);

  // ---- 1. silence in -> silence out --------------------------------
  spx_init();
  clear_io();
  render(SPX_SR);
  CHECK(win_peak(OUT, 0, SPX_SR) == 0, "silence in -> silence out (LINE source)");
  CHECK(!spx.nan_seen, "no NaN on silence");

  // ---- 2. all 30 ROM presets: finite, audible, no runaway ----------
  {
    bool all_ok = true, all_audible = true, all_decay = true;
    for (int p = 1; p <= SPX_ROM_PROGRAMS; p++) {
      spx_init();
      spx_load_program(p);
      spx.nan_seen = false;
      clear_io();
      fill_noise(0, SPX_SR / 2, 0.55, 1000 + p);                    // 0.5 s excitation
      fill_tone(SPX_SR / 2, SPX_SR / 2, 330.0, 0.4);                // + 0.5 s tone
      render(SPX_SR * 3);                                           // + 2 s of silence
      if (spx.nan_seen) { all_ok = false; printf("  preset %d (%s): NaN!\n", p, spx.cur.name); }
      double r_first = win_rms_st(OUT, SPX_SR / 10, SPX_SR / 2);
      double r_last = win_rms_st(OUT, (int)(SPX_SR * 2.6), SPX_SR * 3 - 100);
      if (r_first < 200) { all_audible = false; printf("  preset %d (%s): inaudible (rms %.1f)\n", p, spx.cur.name, r_first); }
      if (r_last > r_first * 1.2 + 50) { all_decay = false; printf("  preset %d (%s): runaway? first %.1f last %.1f\n", p, spx.cur.name, r_first, r_last); }
    }
    CHECK(all_ok, "all 30 presets: no NaN");
    CHECK(all_audible, "all 30 presets: audible output from test signal");
    CHECK(all_decay, "all 30 presets: no runaway feedback after input stops");
  }

  // ---- 3. reverb tail decays ---------------------------------------
  spx_init(); spx_load_program(1);                    // REV HALL
  clear_io(); fill_noise(0, SPX_SR * 3 / 100, 0.8, 777);   // 30 ms burst excites the tank
  render(SPX_SR * 3);
  {
    double early = win_rms_st(OUT, (int)(SPX_SR * 0.08), (int)(SPX_SR * 0.30));
    double late = win_rms_st(OUT, (int)(SPX_SR * 2.5), (int)(SPX_SR * 2.9));
    CHECK(early > 300, "reverb: impulse produces a tail");
    CHECK(late < early * 0.30, "reverb: tail decays by 2.5 s");
  }

  // ---- 4. stereo echo repeats decay --------------------------------
  spx_init(); spx_load_program(8);                    // STEREO ECHO 375 ms
  clear_io(); IN[0] = 30000;
  render(SPX_SR * 2);
  {
    double r1 = win_peak(OUT, (int)(SPX_SR * 0.34), (int)(SPX_SR * 0.41));
    double r2 = win_peak(OUT, (int)(SPX_SR * 0.71), (int)(SPX_SR * 0.79));
    double r3 = win_peak(OUT, (int)(SPX_SR * 1.09), (int)(SPX_SR * 1.16));
    CHECK(r1 > 3000, "echo: first repeat present at ~375 ms");
    CHECK(r2 < r1 && r3 < r2 && r3 > 200, "echo: repeats decay monotonically");
    double diff = 0;                                   // stereo: L and R differ (spread)
    for (int i = (int)(SPX_SR * 0.34); i < (int)(SPX_SR * 0.41); i++) diff += fabs((double)OUT[i * 2] - OUT[i * 2 + 1]);
    CHECK(diff > 1000, "echo: stereo spread decorrelates L/R");
  }

  // ---- 5. pitch shift accuracy (+12 / -12 semitones) ---------------
  for (int k = 0; k < 2; k++) {
    spx_init(); spx_load_program(21);                 // PITCH CHANGE A
    spx_set_param(1, k == 0 ? 12.0f : -12.0f);        // PITCH A
    spx_set_param(2, 0.0f);                           // FINE A
    spx.cur.bal = 1.0f;                               // 100% wet: measure the shift, not the dry
    clear_io(); fill_tone(0, SPX_SR * 2, 440.0, 0.5);
    render(SPX_SR * 2);
    double f = measure_freq(OUT, (int)(SPX_SR * 1.5), SPX_SR * 2);
    double want = k == 0 ? 880.0 : 220.0;
    char msg[80];
    snprintf(msg, sizeof(msg), "pitch %+d st: measured %.1f Hz (want %.0f)", k == 0 ? 12 : -12, f, want);
    CHECK(fabs(f - want) < want * 0.05, msg);
  }

  // ---- 6. freeze captures and loops after input stops --------------
  spx_init(); spx_load_program(25);                   // FREEZE A
  clear_io();
  fill_tone(0, (int)(SPX_SR * 1.2), 660.0, 0.55);     // prime the ring with > REC TIME of tone
  render((int)(SPX_SR * 1.2));
  spx_trigger();                                      // capture the last 1.0 s (all tone)
  clear_io();                                         // input now silent
  render(SPX_SR * 2);
  {
    double loop_rms = win_rms_st(OUT, (int)(SPX_SR * 1.2), (int)(SPX_SR * 1.9));
    double f = measure_freq(OUT, (int)(SPX_SR * 1.2), (int)(SPX_SR * 1.9));
    CHECK(loop_rms > 800, "freeze: loop keeps playing after input stops");
    CHECK(fabs(f - 660.0) < 660.0 * 0.08, "freeze: loop is the captured 660 Hz tone");
    spx_trigger();                                      // back to live
    clear_io(); render(SPX_SR / 2);
    CHECK(win_peak(OUT, 100, SPX_SR / 2) < 500, "freeze: second trigger returns to live (silent in, silent out)");
  }

  // ---- 7. compressor squeezes dynamic range ------------------------
  {
    auto dyn_range = [&](bool comp_on, double *bed_out, double *loud_out) {
      spx_init();
      spx_load_program(19);                           // COMPRESSOR -18 dB, 3:1, +4 dB makeup
      if (!comp_on) spx.bypass = true;
      clear_io();
      fill_tone(0, SPX_SR * 2, 220.0, 0.18);          // quiet bed
      fill_tone(SPX_SR, (int)(SPX_SR * 0.4), 220.0, 0.95, 2 * M_PI * 220.0);  // loud section 1.0-1.4 s
      render(SPX_SR * 2);
      *bed_out = win_rms_st(OUT, SPX_SR / 5, (int)(SPX_SR * 0.8));
      *loud_out = win_rms_st(OUT, (int)(SPX_SR * 1.1), (int)(SPX_SR * 1.35));
      return *loud_out / (*bed_out + 1e-9);
    };
    double bb, bl, cb2, cl;
    double rb = dyn_range(false, &bb, &bl);
    double rc = dyn_range(true, &cb2, &cl);
    char msg[100];
    snprintf(msg, sizeof(msg), "compressor: loud/bed ratio %.2f bypass -> %.2f comp (loud %.0f -> %.0f)", rb, rc, bl, cl);
    CHECK(rc < rb * 0.65, msg);
    CHECK(cl < bl * 0.80, "compressor: loud section pulled down");
  }

  // ---- 8. noise gate silences low signal, passes loud --------------
  spx_init(); spx_load_program(18);                   // ADR NOISE GATE (thresh -30 dB)
  clear_io(); fill_tone(0, SPX_SR, 440.0, 0.01);      // -40 dB: below threshold
  render(SPX_SR);
  double quiet = win_rms_st(OUT, SPX_SR / 2, SPX_SR);
  clear_io(); fill_tone(0, SPX_SR, 440.0, 0.5);
  spx_load_program(18);
  render(SPX_SR);
  double loud = win_rms_st(OUT, SPX_SR / 2, SPX_SR);
  CHECK(quiet < 40, "gate: -40 dB signal is silenced");
  CHECK(loud > 3000, "gate: loud signal passes");

  // ---- 9. parametric EQ boosts its band ----------------------------
  {
    auto band_ratio = [&](bool boost) {
      spx_init(); spx_load_program(30);
      if (boost) spx_set_param(3, 12.0f);             // M GAIN +12 dB @ 1 kHz
      clear_io(); fill_noise(0, SPX_SR * 2, 0.5, 4242);
      render(SPX_SR * 2);
      double g1 = goertzel(OUT, SPX_SR / 2, SPX_SR, 1000.0);
      double g2 = goertzel(OUT, SPX_SR / 2, SPX_SR, 250.0);
      return g1 / (g2 + 1e-9);
    };
    double flat = band_ratio(false), boosted = band_ratio(true);
    char msg[90];
    snprintf(msg, sizeof(msg), "EQ: 1kHz/250Hz ratio %.2f flat -> %.2f with +12 dB mid", flat, boosted);
    CHECK(boosted > flat * 2.0, msg);
  }

  // ---- 10. gate reverb: tail is cut when the gate closes -----------
  spx_init(); spx_load_program(16);                   // GATE REVERB, gate time 0.45 s
  clear_io(); fill_tone(0, SPX_SR / 5, 330.0, 0.7);   // 0.2 s burst, then silence
  render(SPX_SR * 2);
  {
    double open = win_rms_st(OUT, (int)(SPX_SR * 0.25), (int)(SPX_SR * 0.42));
    double shut = win_rms_st(OUT, (int)(SPX_SR * 1.1), (int)(SPX_SR * 1.5));
    CHECK(open > 300, "gate reverb: tail audible while gate is open");
    CHECK(shut < open * 0.10, "gate reverb: tail cut after gate time");
  }

  // ---- 11. bypass passes dry ---------------------------------------
  spx_init(); spx_load_program(1); spx.bypass = true;
  clear_io(); fill_tone(0, SPX_SR, 220.0, 0.5);
  render(SPX_SR);
  {
    double ri = 0, ro = 0;
    for (int i = SPX_SR / 2; i < SPX_SR; i++) { ri += (double)IN[i] * IN[i]; ro += (double)OUT[i * 2] * OUT[i * 2]; }
    double ratio = sqrt(ro) / sqrt(ri);
    CHECK(ratio > 0.90 && ratio < 1.0, "bypass: output RMS matches input (dry passthrough, x out_level 0.9)");
  }

  // ---- 12. balance: 0 = dry only, no echo --------------------------
  spx_init(); spx_load_program(7);                    // DELAY L,R
  spx.cur.bal = 0.0f;
  clear_io(); IN[0] = 30000;
  render(SPX_SR);
  CHECK(win_peak(OUT, (int)(SPX_SR * 0.2), (int)(SPX_SR * 0.5)) == 0, "balance 0: no wet echo, dry only");
  spx_init(); spx_load_program(7);
  spx.cur.bal = 1.0f;
  clear_io(); IN[0] = 30000;
  render(SPX_SR);
  CHECK(win_peak(OUT, (int)(SPX_SR * 0.24), (int)(SPX_SR * 0.32)) > 3000, "balance 1: echo present");

  // ---- 13. MIDI: program change, channel filter, CCs, note pitch ---
  spx_init();
  spx_midi_feed(0xC0); spx_midi_feed(7);
  CHECK(spx.cur_prog == 8, "MIDI program change 7 -> program 8");
  spx_midi_feed(0xC0); spx_midi_feed(100);
  CHECK(spx.cur_prog == 90, "MIDI program change clamps at 90");
  spx.midi_ch = 2;
  spx_midi_feed(0xC0); spx_midi_feed(3);
  CHECK(spx.cur_prog == 90, "MIDI: wrong channel ignored when midi_ch set");
  spx_midi_feed(0xC1); spx_midi_feed(0);
  CHECK(spx.cur_prog == 1, "MIDI: set channel works");
  spx.midi_ch = 0;
  spx_midi_feed(0xB0); spx_midi_feed(7); spx_midi_feed(64);
  CHECK(fabs(spx.out_level - 64.0 / 127.0) < 0.01, "MIDI CC7 -> output level");
  spx_load_program(21);
  spx_midi_feed(0xB0); spx_midi_feed(91); spx_midi_feed(127);
  CHECK(spx.cur.bal == 1.0f, "MIDI CC91 -> balance");
  spx_midi_feed(0x90); spx_midi_feed(72); spx_midi_feed(100);
  CHECK(spx.cur.p[1] == 12.0f, "MIDI note 72 on PITCH sets +12 semitones");
  spx_midi_feed(0x90); spx_midi_feed(36); spx_midi_feed(100);
  CHECK(spx.cur.p[1] == -12.0f, "MIDI note 36 on PITCH clamps to -12 semitones");

  // ---- 14. user store + serialize round trip ------------------------
  {
    spx_init(); spx_load_program(11);                 // CHORUS A
    spx_set_param(0, 2.5f);                           // MOD FREQ
    memcpy(spx.cur.name, "MY CHORUS", 10);
    CHECK(spx_store_program(36), "store into user slot 36");
    CHECK(!spx_store_program(12), "cannot store over ROM slots");
    static uint8_t img[SPX_USER_SIZE];
    uint32_t n = spx_serialize_users(img, sizeof(img));
    CHECK(n == SPX_USER_SIZE, "user image serializes to fixed size");
    spx_init();
    CHECK(spx.cur_prog == 1 && !spx.user_set[5], "init clears user slots");
    CHECK(spx_deserialize_users(img, n), "user image deserializes");
    spx_load_program(36);
    CHECK(strcmp(spx.cur.name, "MY CHORUS") == 0 && fabs(spx.cur.p[0] - 2.5f) < 0.001f &&
          spx.cur.algo == ALG_CHORUS, "user slot 36 round-trips name/params/algo");
    img[0] = 0;
    CHECK(!spx_deserialize_users(img, n), "corrupt image rejected");
    spx_load_program(77);                             // never stored
    CHECK(strcmp(spx.cur.name, "INIT") == 0, "empty user slot loads INIT program");
  }

  // ---- 15. demo sources make sound on their own ---------------------
  for (int s = SPX_SRC_PLUCK; s <= SPX_SRC_NOISE; s++) {
    spx_init(); spx_load_program(1);
    spx.source = (uint8_t)s;
    clear_io();
    render(SPX_SR * 3);
    char msg[70];
    snprintf(msg, sizeof(msg), "demo source %s renders audio", SPX_SRC_NAME[s]);
    CHECK(win_rms_st(OUT, SPX_SR, SPX_SR * 3) > 250, msg);
  }

  // ---- 16. meter responds to input ----------------------------------
  spx_init(); spx_load_program(1);
  clear_io(); fill_tone(0, SPX_SR / 2, 440.0, 0.7);
  render(SPX_SR / 2);
  CHECK(spx_meter_leds() >= 5, "meter: hot input lights >= 5 LEDs");

  // ---- 17. demo render WAV (what the firmware DSP actually sounds like)
  {
    static const int SEG_PROG[6] = {1, 8, 11, 21, 15, 16};
    static const char *SEG_NAME[6] = {"REV HALL", "STEREO ECHO", "CHORUS A", "PITCH +12", "SYMPHONIC", "GATE REV"};
    FILE *f = fopen("tests/demo_render.wav", "wb");
    if (f) {
      int seg = SPX_SR * 2, total = seg * 6;
      // placeholder header, patched at the end
      uint8_t wh[44] = {0};
      fwrite(wh, 1, 44, f);
      int16_t *mix = (int16_t *)malloc(total * 2 * sizeof(int16_t));
      for (int s = 0; s < 6; s++) {
        spx_init();
        spx.source = SPX_SRC_PLUCK;
        spx_load_program(SEG_PROG[s]);
        if (s == 3) { spx_set_param(1, 12.0f); spx_set_param(2, 0.0f); }  // PITCH +12
        int16_t *segbuf = mix + s * seg * 2;
        spx_render(segbuf, nullptr, seg);
        printf("demo segment %d: %s\n", s + 1, SEG_NAME[s]);
      }
      uint32_t data_bytes = total * 2 * 2, chunk = 36 + data_bytes;
      memcpy(wh, "RIFF", 4); memcpy(wh + 8, "WAVEfmt ", 8);
      wh[4] = chunk; wh[5] = chunk >> 8; wh[6] = chunk >> 16; wh[7] = chunk >> 24;
      wh[16] = 16; wh[20] = 1; wh[22] = 2;
      wh[24] = SPX_SR & 0xFF; wh[25] = (SPX_SR >> 8) & 0xFF; wh[26] = (SPX_SR >> 16) & 0xFF; wh[27] = (SPX_SR >> 24) & 0xFF;
      uint32_t br = SPX_SR * 4;
      wh[28] = br & 0xFF; wh[29] = (br >> 8) & 0xFF; wh[30] = (br >> 16) & 0xFF; wh[31] = (br >> 24) & 0xFF;
      wh[32] = 4; wh[34] = 16;
      memcpy(wh + 36, "data", 4);
      wh[40] = data_bytes & 0xFF; wh[41] = (data_bytes >> 8) & 0xFF; wh[42] = (data_bytes >> 16) & 0xFF; wh[43] = (data_bytes >> 24) & 0xFF;
      fseek(f, 0, SEEK_SET);
      fwrite(wh, 1, 44, f);
      fseek(f, 44, SEEK_SET);
      fwrite(mix, 2, total * 2, f);
      fclose(f); free(mix);
      printf("demo render written: tests/demo_render.wav (12 s, 31250 Hz stereo)\n");
      CHECK(true, "demo render written");
    } else {
      CHECK(false, "demo render file opens");
    }
  }

  printf("\n%d checks, %d failures\n", checks, failures);
  free(IN); free(OUT);
  return failures ? 1 : 0;
}
