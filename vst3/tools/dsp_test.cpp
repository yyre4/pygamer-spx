// dsp_test.cpp -- validation for the VST sample-rate adaptation.
//
// Compiled twice:
//   ref:   against the ORIGINAL firmware header (spx_core.h, fixed 31250)
//   adapt: against the adapted plugin header (SpxCore.h, runtime rate)
//
// Usage: dsp_test <rate> <prefix>
//   - renders the 6-segment demo (same recipe as host_test.cpp #17)
//     to <prefix>_demo.bin (int16 stereo raw)
//   - writes a per-preset RMS/peak table to <prefix>_presets.txt
//   - runs the rate-parameterized sanity suite (silence, all-preset
//     stability, reverb decay, echo decay, pitch accuracy, freeze,
//     bypass) and prints PASS/FAIL lines; exit code = failures>0.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#ifdef ADAPTED
#include "SpxCore.h"
#else
#include "spx_core.h"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; if (!(cond)) { failures++; printf("FAIL: %s\n", msg); } \
                              else printf("ok: %s\n", msg); } while (0)

static int RATE = 31250;
static int16_t *IN = nullptr, *OUT = nullptr;
static int MAXF = 0;

static void clear_io(void) { memset(IN, 0, MAXF * 2); memset(OUT, 0, MAXF * 4); }
static void fill_tone(int start, int n, double freq, double amp) {
  for (int i = 0; i < n && start + i < MAXF; i++)
    IN[start + i] = (int16_t)(amp * 32767.0 * sin(2 * M_PI * freq * i / RATE));
}
static void fill_noise(int start, int n, double amp, unsigned seed = 99) {
  srand(seed);
  for (int i = 0; i < n && start + i < MAXF; i++)
    IN[start + i] = (int16_t)(amp * 32767.0 * ((rand() % 20000) / 10000.0 - 1.0));
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
static double measure_freq(const int16_t *st, int a, int b, int ch = 0) {
  int cross = 0, prev = 0;
  for (int i = a; i < b && i < MAXF; i++) {
    int s = st[i * 2 + ch] > 200 ? 1 : (st[i * 2 + ch] < -200 ? -1 : 0);
    if (s != 0 && prev != 0 && s != prev) cross++;
    if (s != 0) prev = s;
  }
  return cross / 2.0 / ((double)(b - a) / RATE);
}
static int S(double sec) { return (int)(RATE * sec); }

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: dsp_test <rate> <prefix>\n"); return 2; }
  RATE = atoi(argv[1]);
  const char *prefix = argv[2];
#ifdef ADAPTED
  spx_set_sample_rate(RATE);
  printf("adapted core @ %d Hz (SPX_SR now %d, pitch win %d, comb0 %d)\n",
         RATE, (int)SPX_SR, (int)SPX_PITCH_WIN, (int)REV_COMB_LEN[0]);
#else
  if (RATE != SPX_SR) { fprintf(stderr, "ref build is fixed at %d\n", SPX_SR); return 2; }
  printf("original core @ %d Hz\n", RATE);
#endif
  MAXF = RATE * 8;
  IN = (int16_t *)calloc(MAXF, 2);
  OUT = (int16_t *)calloc(MAXF, 4);

  // ---- demo render (host_test #17 recipe) --------------------------
  {
    static const int SEG_PROG[6] = {1, 8, 11, 21, 15, 16};
    int seg = RATE * 2, total = seg * 6;
    int16_t *mix = (int16_t *)malloc(total * 2 * sizeof(int16_t));
    for (int s = 0; s < 6; s++) {
      spx_init();
      spx.source = SPX_SRC_PLUCK;
      spx_load_program(SEG_PROG[s]);
      if (s == 3) { spx_set_param(1, 12.0f); spx_set_param(2, 0.0f); }
      spx_render(mix + s * seg * 2, nullptr, seg);
    }
    char path[256]; snprintf(path, sizeof(path), "%s_demo.bin", prefix);
    FILE *f = fopen(path, "wb"); fwrite(mix, 2, total * 2, f); fclose(f);
    free(mix);
    printf("demo render -> %s (%d frames)\n", path, total);
  }

  // ---- per-preset table ---------------------------------------------
  {
    char path[256]; snprintf(path, sizeof(path), "%s_presets.txt", prefix);
    FILE *f = fopen(path, "w");
    for (int p = 1; p <= SPX_ROM_PROGRAMS; p++) {
      spx_init(); spx_load_program(p);
      clear_io();
      fill_noise(0, RATE / 2, 0.55, 1000 + p);
      fill_tone(RATE / 2, RATE / 2, 330.0, 0.4);
      spx_render(OUT, IN, RATE * 3);
      fprintf(f, "%2d %-16s rms_first=%9.2f rms_last=%9.2f peak=%9.1f nan=%d\n",
              p, spx.cur.name, win_rms_st(OUT, RATE / 10, RATE / 2),
              win_rms_st(OUT, S(2.6), RATE * 3 - 100), win_peak(OUT, 0, RATE * 3), (int)spx.nan_seen);
    }
    fclose(f);
    printf("preset table -> %s\n", path);
  }

  // ---- sanity suite ---------------------------------------------------
  // silence in -> silence out
  spx_init(); clear_io(); spx_render(OUT, IN, RATE);
  CHECK(win_peak(OUT, 0, RATE) == 0, "silence in -> silence out");
  CHECK(!spx.nan_seen, "no NaN on silence");

  // all presets stable / audible / decaying
  {
    bool ok = true, aud = true, dec = true;
    for (int p = 1; p <= SPX_ROM_PROGRAMS; p++) {
      spx_init(); spx_load_program(p); spx.nan_seen = false;
      clear_io();
      fill_noise(0, RATE / 2, 0.55, 1000 + p);
      fill_tone(RATE / 2, RATE / 2, 330.0, 0.4);
      spx_render(OUT, IN, RATE * 3);
      double rf = win_rms_st(OUT, RATE / 10, RATE / 2);
      double rl = win_rms_st(OUT, S(2.6), RATE * 3 - 100);
      if (spx.nan_seen) { ok = false; printf("  preset %d NaN\n", p); }
      if (rf < 200) { aud = false; printf("  preset %d inaudible (%.1f)\n", p, rf); }
      if (rl > rf * 1.2 + 50) { dec = false; printf("  preset %d runaway (%.1f -> %.1f)\n", p, rf, rl); }
    }
    CHECK(ok, "all 30 presets: no NaN");
    CHECK(aud, "all 30 presets: audible");
    CHECK(dec, "all 30 presets: no runaway");
  }

  // reverb tail decays
  spx_init(); spx_load_program(1);
  clear_io(); fill_noise(0, S(0.03), 0.8, 777);
  spx_render(OUT, IN, RATE * 3);
  {
    double early = win_rms_st(OUT, S(0.08), S(0.30));
    double late = win_rms_st(OUT, S(2.5), S(2.9));
    CHECK(early > 300, "reverb: impulse produces a tail");
    CHECK(late < early * 0.30, "reverb: tail decays by 2.5 s");
  }

  // stereo echo repeats decay
  spx_init(); spx_load_program(8);
  clear_io(); IN[0] = 30000;
  spx_render(OUT, IN, RATE * 2);
  {
    double r1 = win_peak(OUT, S(0.34), S(0.41));
    double r2 = win_peak(OUT, S(0.71), S(0.79));
    double r3 = win_peak(OUT, S(1.09), S(1.16));
    CHECK(r1 > 3000, "echo: first repeat at ~375 ms");
    CHECK(r2 < r1 && r3 < r2 && r3 > 200, "echo: repeats decay monotonically");
  }

  // pitch accuracy
  for (int k = 0; k < 2; k++) {
    spx_init(); spx_load_program(21);
    spx_set_param(1, k == 0 ? 12.0f : -12.0f);
    spx_set_param(2, 0.0f);
    spx.cur.bal = 1.0f;
    clear_io(); fill_tone(0, RATE * 2, 440.0, 0.5);
    spx_render(OUT, IN, RATE * 2);
    double f = measure_freq(OUT, S(1.5), RATE * 2);
    double want = k == 0 ? 880.0 : 220.0;
    char msg[90];
    snprintf(msg, sizeof(msg), "pitch %+d st @%d: %.1f Hz (want %.0f)", k == 0 ? 12 : -12, RATE, f, want);
    CHECK(fabs(f - want) < want * 0.05, msg);
  }

  // freeze captures and loops
  spx_init(); spx_load_program(25);
  clear_io(); fill_tone(0, S(1.2), 660.0, 0.55);
  spx_render(OUT, IN, S(1.2));
  spx_trigger();
  clear_io(); spx_render(OUT, IN, RATE * 2);
  {
    double lr = win_rms_st(OUT, S(1.2), S(1.9));
    double f = measure_freq(OUT, S(1.2), S(1.9));
    CHECK(lr > 800, "freeze: loop keeps playing after input stops");
    CHECK(fabs(f - 660.0) < 660.0 * 0.08, "freeze: loop is the captured 660 Hz tone");
  }

  // bypass = dry passthrough x out_level
  spx_init(); spx_load_program(1); spx.bypass = true;
  clear_io(); fill_tone(0, RATE, 220.0, 0.5);
  spx_render(OUT, IN, RATE);
  {
    double ri = 0, ro = 0;
    for (int i = RATE / 2; i < RATE; i++) { ri += (double)IN[i] * IN[i]; ro += (double)OUT[i * 2] * OUT[i * 2]; }
    double ratio = sqrt(ro) / sqrt(ri);
    char bmsg[90];
    snprintf(bmsg, sizeof(bmsg), "bypass: dry passthrough ratio %.4f (~ out_level 0.9)", ratio);
    CHECK(ratio > 0.88 && ratio < 1.0, bmsg);
  }

  printf("\n[%d Hz] %d checks, %d failures\n", RATE, checks, failures);
  free(IN); free(OUT);
  return failures ? 1 : 0;
}
