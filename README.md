# PyGamer SPX — a Yamaha SPX90 II emulation for the Adafruit PyGamer

The 1986 rack effects box, in your pocket: **all 30 SPX90 II ROM
programs plus 60 user memories, running at the original's exact
31.25 kHz sample rate, mono line-in / stereo out**, on a PyGamer
(ATSAMD51J19 @ 120 MHz, 512 KB flash / 192 KB RAM). Same architecture
as the FlyBrain and MPC builds: every DSP decision lives in one
pure-C++ header (`pygamer_spx/spx_core.h`) that runs **identically**
on the handheld and in the PC host test, so the effects below are
verified even though the hardware run is still yours to do (see
"What's verified" at the bottom).

**What "emulation" means here, honestly:** a *parameter-model*
emulation. The algorithm structures (Schroeder/Moorer comb+allpass
reverb, tapped early reflections, modulated-delay chorus / flange /
symphonic, granular pitch shifting, feedback delays, gate/compressor
dynamics, biquad EQ), the 30 program names and order, and their
parameter ranges follow the SPX90 II manual. Yamaha's proprietary
DSP code was never published — this is **not bit-accurate and
contains no Yamaha code or ROM data**. It sounds like an SPX90 II
the way a good model sounds like the amp: the character and the
controls are right, the last 5% of fairy dust is not claimed.

## Flash it (no compiling)

1. (Optional) Put a FAT32 microSD card in the PyGamer if you want
   user memories to survive power-off.
2. Double-tap reset → `PYGAMERBOOT` drive appears → drag
   **`pygamer_spx.uf2`** onto it.
3. It boots into the PROGRAM screen on program 1, REV HALL. With
   nothing wired up, go to **UTIL → SOURCE → DEMO PLUCK** and it
   makes sound immediately. Wire the line input (WIRING.md) and set
   SOURCE back to **LINE A9** for the real thing.

Compiled with PlatformIO (Atmel SAM, Adafruit SAMD core, Arcada
2.5.10, TinyUSB): **Flash 122,720 / 512 KB (23.4%), RAM 146,740 /
196,608 bytes (74.6%)**. UF2 header verified (SAMD51 family
0x55114460, base 0x4000). **Not yet run on physical hardware** — see
the caveats at the bottom.

## The 30 programs (ROM 1–30, user memories 31–90)

| # | Program | Engine | # | Program | Engine |
|---|---|---|---|---|---|
| 1 | REV HALL | reverb | 16 | GATE REVERB | gated reverb |
| 2 | REV ROOM | reverb | 17 | REVERSE GATE | gated reverb |
| 3 | REV VOCAL | reverb | 18 | ADR NOISE GATE | dynamics |
| 4 | REV PLATE | reverb | 19 | COMPRESSOR | dynamics |
| 5 | EARLY REFLECTION 1 | early refl. | 20 | REVERB & GATE | gated reverb |
| 6 | EARLY REFLECTION 2 | early refl. | 21 | PITCH CHANGE A | pitch (single) |
| 7 | DELAY L,R | stereo delay | 22 | PITCH CHANGE B | pitch (dual detune) |
| 8 | STEREO ECHO | feedback echo | 23 | PITCH CHANGE C | pitch (dual harmony) |
| 9 | STEREO FLANGE A | flanger | 24 | PITCH CHANGE D | pitch (+12, feedback) |
| 10 | STEREO FLANGE B | flanger | 25 | FREEZE A | freeze (trigger) |
| 11 | CHORUS A | chorus | 26 | FREEZE B | freeze (MIDI note) |
| 12 | CHORUS B | chorus | 27 | AUTO PAN | pan |
| 13 | STEREO PHASING | phaser | 28 | TRIGGERED PAN | pan (sweep) |
| 14 | TREMOLO | tremolo | 29 | DELAY VIBRATO | vibrato (100% wet) |
| 15 | SYMPHONIC | symphonic chorus | 30 | PARAMETRIC EQ | 3-band EQ |

Every program is editable: each engine exposes its real parameters
(reverb time / high ratio / pre-delay / HPF / LPF; echo time /
feedback / damping; pitch coarse ±12 st + fine cents per voice;
freeze record time / trim / pitch; gate threshold / attack / hold /
release; EQ freq/gain/Q …) plus **BALANCE** — the wet/dry mix the
original put on its front panel. Edits are live; STORE keeps them.

**Freeze** works like the original's party trick: the input is
always being recorded into a ring; press **A** and the last
REC TIME seconds become an endless loop you can pitch from the
PARAM screen (or by MIDI note in FREEZE B). Press A again to go
back to live.

## Controls (footers on every screen repeat these)

| Control | Does |
|---|---|
| **SELECT** (tap) | next screen: PROGRAM → PARAM → STORE → UTIL |
| **START** | bypass on/off, everywhere |
| **A** | trigger: freeze capture/release, triggered-pan sweep (PROGRAM screen) |
| **B** | back to PROGRAM screen |
| Stick L/R | program ±1 (PROGRAM), adjust value (PARAM/UTIL), pick slot (STORE) |
| Stick U/D | program ±10 (PROGRAM), move row (PARAM/UTIL) |
| **A held** + stick L/R | coarse adjust ×10 (PARAM screen) |

- **PROGRAM**: the rack front panel — big memory number, a 16×2-style
  LCD with the program name and first parameter, the 7-LED input
  meter, source/gain/output readout. The 5 NeoPixels mirror the
  input meter.
- **PARAM**: parameter list for the current program + BALANCE.
- **STORE**: pick a user slot (31–90, shows what it currently holds),
  A writes it — and saves `/SPX/USER.BIN` to the SD card too.
- **UTIL**: MIDI channel (OMNI/1–16), input gain (0.10×–4.00×),
  source (LINE A9 / DEMO PLUCK / DEMO PAD / DEMO NOISE), output
  level, save-user-to-SD, and the measured A9 bias readout.

## MIDI (input only)

DIN-5 on the Feather UART (Serial1) and USB MIDI feed the same
parser. Channel filter in UTIL (OMNI by default).

| Message | Does |
|---|---|
| Program change 0–89 | recall program 1–90 (90–127 clamp to 90) |
| Note on, PITCH programs | set shift = note − 60 semitones (clamped ±12), fine reset |
| Note on, FREEZE B | start loop playback at the note's pitch (±24 st) |
| Note on, other trigger programs | trigger (pan sweep etc.) |
| CC 7 | output level |
| CC 91 | balance (wet/dry) |

## The honest limits (read these — they're the real spec)

- **Max delay / freeze time is ~1.44 s, not the II's 2.0 s.** The
  shared delay line is 45,056 samples (88 KB of the 192 KB RAM);
  delay times clamp at 1,400 ms and freeze REC TIME at 1.4 s.
  Every factory program sits far inside that.
- **12-bit DAC, ~10-effective-bit ADC.** The original was 16-bit
  both ways. The SAMD51's converters give the effects a slightly
  grainier floor — period-appropriate grit, but it is a real
  difference, stated plainly.
- **Bandwidth is emulated, not measured off a real unit:** the wet
  path runs through a ~12 kHz one-pole (the II's effect bandwidth
  was 20 Hz–12 kHz); the dry/bypass path is full-range, like the
  original's direct path.
- **Parameter-model, not bit-accurate** (see the top). Preset
  parameter values are plausible settings inside the manual's
  ranges — e.g. REV ROOM at 1.5 s / high 0.7 / 20 ms / LPF 8 kHz —
  not dumps of Yamaha's internal coefficients.
- **MIDI is input-only.** No thru, no out, no SysEx bulk dump;
  user memories move via the SD file instead.
- **Guitar straight in is quiet** (high-Z, low level into a 100k
  bias network). A buffered pedal/DI/preamp in front fixes it —
  WIRING.md has the gain-staging rundown.

## What's verified, and what isn't

Verified on the PC with the exact firmware DSP core (**46/46 host
checks**, `tests/host_test_output.txt`): all 30 presets process
without NaN/runaway and decay after the input stops; reverb tail
shape; echo repeats decay monotonically with stereo spread; pitch
shift measures 867 Hz for +12 st on a 440 Hz tone (and the −12 st
case within 5% of 220 Hz); freeze captures a 660 Hz tone and keeps
looping it, at pitch, after the input stops; the compressor
squeezes a loud/quiet dynamic range; the gate silences a −40 dB
signal and passes a loud one; the EQ's +12 dB mid boost measures
>2× band-energy ratio; gate reverb cuts its tail at the gate time;
bypass is a true dry passthrough; balance 0 kills the wet path;
MIDI program change / channel filter / CC7 / CC91 / note-pitch all
behave; user store + SD image round-trips (and a corrupt image is
rejected); all three demo sources render. `tests/demo_render.wav`
is 12 s of the demo pluck running through REV HALL, STEREO ECHO,
CHORUS A, PITCH +12, SYMPHONIC and GATE REV — rendered by the
firmware core itself, so it's an honest preview of the box.

**Not verified: the physical hardware.** The UF2 is header-checked
and the firmware compiles clean, but as with the MPC build, nobody
has run this on the actual PyGamer yet. In particular the **live
A9 input path** — ADC0 put into free-running mode by register writes
(A9 = ADC0/AIN14 per the PyGamer variant; the core's 48 MHz ÷ 32
ADC clock gives ~100 kS/s conversions against our 31.25 kHz stream,
and the loop re-arms the setup if anything reconfigures ADC0) — is
compile-verified and datasheet-checked, **not** bench-verified.
DAC output level, timer rate stability at 31,250 Hz, and SD
behaviour need your unit. If audio is silent, check in this order:
headphone jack (speaker is quiet), SOURCE = DEMO PLUCK (isolates
the output side), then UTIL's A9 bias readout (~2048 expected).

## Build from source

**PlatformIO:** `pio run` (env `adafruit_pygamer_m4`). The
`platformio.ini` carries the same `-I` fix for the SPI/TinyUSB
circular-dependency quirk the FlyBrain build documented.
**Arduino IDE:** Adafruit SAMD boards + Arcada library, open
`pygamer_spx/pygamer_spx.ino`, board = Adafruit PyGamer, USB Stack =
TinyUSB.

**Host test** (the effects proof):
`g++ -O2 -I pygamer_spx -o host_test host_test.cpp && ./host_test`
— 46 checks as listed above; also writes `tests/demo_render.wav`.

## Files

- `pygamer_spx.uf2` / `pygamer_spx.bin` — ready-to-flash firmware
- `pygamer_spx/pygamer_spx.ino` — hardware glue (UI, ADC, DAC, SD, MIDI ports)
- `pygamer_spx/spx_core.h` — the SPX brain (17 engines, 30 ROM programs, 60 user memories, demo sources, MIDI parser, SD image format)
- `host_test.cpp`, `tests/` — PC verification + output + demo render
- `WIRING.md` — A9 line-in bias circuit, gain staging, MIDI DIN circuit + FeatherWing route, parts list
- `tools/bin2uf2.py` — the bin→UF2 converter used for the shipped UF2

## Donations

If you like this and want to support more of it:

- **Ethereum:** `0x2D4B15760e621af2aC100EC8e39A10D57621E4AC`
- **Bitcoin:** `bc1qvl6sy99kn9rll3jjzu96vfkmezcfjq6yf58qd9`
