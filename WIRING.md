# WIRING.md — line input + MIDI hardware for the PyGamer SPX

The PyGamer itself gives you: the screen, stick, 4 buttons, stereo DAC
audio out (headphone jack + mono speaker), the microSD slot, and a
Feather header on the back. Everything in this guide plugs into that
Feather header or the JST connectors on the back. **All logic is 3.3 V.
Never put 5 V into a PyGamer pin.**

The SPX90 II was **mono in / stereo out**, and so is this: one line
input (below) feeds every effect; stereo comes out of the headphone
jack (L = A1 DAC, R = A0 DAC). The built-in speaker is mono and
follows the right side — fine for checking it's alive, but use
headphones or a line out to actually hear the stereo programs.

## The line input — the PyGamer has NO mic and NO line-in

The input is analog pin **A9**, broken out on the back as the **D3
JST-PH connector** (3 pins: GND / A9 / 3.3 V) — no FeatherWing
conflict, and you can build the input on a little JST pigtail. This is
the same circuit the PyGamer MPC build uses; the difference is that
the SPX firmware runs it as a **live stream**, not sampled takes.

The ADC can only read 0–3.3 V, so the signal must be biased at half
the supply (1.65 V). The firmware measures the actual bias point at
boot, tracks it slowly (clamped to ±200 ADC counts of the boot value)
in the audio interrupt, and a ~7 Hz DC blocker in the DSP core removes
what's left of the offset and any drift — so close is fine, and the
bias wobbling a little in use does not detune or offset the effects.

### Line-in (from a synth, mixer send/tape-out, phone — build this one)

```
                         100k
  3.3V (D3 pin) ────────/\/\/\──┬─────────────── to A9 (D3 pin)
                                │
  signal in (3.5mm tip) ── 1µF ─┤         (film or ceramic cap,
                                │          + side toward source if electrolytic)
                         100k   │
  GND ──────────────────/\/\/\──┘
  signal sleeve ──────── GND

  gain trim: put a 10k pot in series before the cap
  (in ── pot ── cap ── node) so hot sources can be turned down.
  Optional but smart: two 1N4148 diodes from the A9 node to 3.3V
  and GND as clamps, so an over-hot source can't hurt the pin.
```

### Gain staging (this is the original's −20 dB / +4 dB switch, split in two)

The SPX90 II had a rear-panel input level selector. Here the same job
is split between the **10k trim pot** (coarse, analogue, do this
first) and **UTIL → INPUT GAIN** on the device (0.10×–4.00×, fine).

- **Synth / mixer aux send / tape-out / phone headphone out** — the
  ideal sources. Line level (~1 V peak) lands nicely; watch the
  on-screen 7-LED meter and keep peaks out of the red LED.
- **Guitar / bass, plugged straight in** — works, but a raw guitar
  pickup is low-level *and* high-impedance: into this input it will
  be quiet and a bit dull (the 100k bias resistors load the pickup).
  It is genuinely usable for testing — trim up, INPUT GAIN up to
  2–4× — but for real playing put *anything* buffered in front: a
  buffered pedal (most Boss-style pedals), a DI box, a cheap preamp,
  or the MAX9814-style module below with the gain set low. That one
  box is the difference between "proof it works" and "sounds right".
- **Never feed speaker-level outputs** or anything above 3.3 V
  peak-to-peak into A9. The clamp diodes are seatbelts, not a plan.

## Mixer aux send / return — the way this box is meant to be used

The SPX90 II lived on console aux sends, and this build slots into
the same loop unchanged:

```
board AUX SEND (mono) ──► input circuit above ──► A9
board AUX RETURN L/R ◄── headphone jack (stereo) ◄── PyGamer
```

- **Run the PyGamer at (or near) 100% wet.** In an aux loop the
  board *is* the mix control — the dry signal reaches the mains
  through the channel itself. Set PARAM → BALANCE (per program, or
  MIDI CC91) fully wet for reverb/echo/ER programs; the board's send
  and return levels then do the job the front-panel BALANCE key did
  on a channel insert. Modulation/pitch programs also work fine in
  the loop run wet — what returns is the effect, the channel keeps
  the dry.
- **Aux sends run HOT — the trim pot is not optional here.** A pro
  board's nominal +4 dBu is already ≈3.5 V peak-to-peak, right at
  this ADC's full range, and sends peak far above that. Start with
  the board's aux master and the 10k trim both LOW, bring them up
  while watching the on-screen 7-LED meter, and park peaks around
  LED 5–6. Build the 1N4148 clamp diodes for this hookup — with a
  live board in front, they're real protection, not decoration.
- **Don't send the return back into the same aux.** If you return
  into a channel strip instead of the dedicated aux return, keep
  that strip's aux send knob for *this* aux at zero, or the reverb
  feeds itself and howls. (Dedicated aux returns usually can't send
  to themselves — channel strips can.)
- **Run the PyGamer on its battery in the loop.** Unplugging USB
  breaks the ground path between the board and your computer, which
  is the usual source of hum/whine on an FX return. Charge it
  between sessions, not during.

### Alligator clips — fine for the bench test, with three rules

1. **Clip the perfboard, never the PyGamer.** The D3 JST pins are
   ~2 mm apart and clip jaws are wide: a clip straight on the
   connector will short A9 to 3.3 V or GND the first time it slips.
   Clip onto the input pigtail / perfboard pads instead — signal
   clip on the source side of the 1 µF cap, ground clip on the GND
   pad. Ground clip to the board's ground *is* wanted here (audio
   needs the shared ground; that's only a MIDI rule, not this one).
2. **The output side never needs clips.** Headphone jack → aux
   return is an ordinary cable (3.5 mm stereo to dual 1/4").
3. **Secure them.** A clip that pops off a live board can land
   anywhere. Tape or heat-shrink over the jaws, clips clipped
   *before* the board is powered, and don't move the rig by the
   leads. For anything past the first test, solder the pigtail —
   it takes ten minutes and ends the short risk.

## MIDI — three routes, from zero-solder to full DIY

MIDI is **input only** on this firmware (program change, note
triggers, CC7/CC91 — see README). The PyGamer still needs the wires
below for DIN; USB MIDI needs nothing at all.

### Route 0 — USB MIDI (no wiring at all)

Plug the PyGamer's micro-USB into your computer. The firmware is a
USB MIDI device ("PyGamer SPX"): DAWs see it as a MIDI in port.
Program changes 0–89 recall programs 1–90. Use it to sanity-check
the firmware before soldering anything.

### Route 1 — Adafruit MIDI FeatherWing (no soldering)

Adafruit's **MIDI FeatherWing** (DIN-5 IN + OUT, built-in 3.3 V
optocoupler, activity LEDs) stacks straight onto the PyGamer's back
Feather headers — it is designed for exactly these RX/TX pins. We
only use its IN side. Plug it in, plug a DIN cable in, done.

### Route 2 — DIY DIN-5 MIDI IN (about $2 of parts)

The firmware listens on **Serial1 = Feather D0 (RX)** at 31,250 baud,
whether the bytes come from a FeatherWing or this circuit. (The OUT
circuit from the MPC guide also works if you ever want thru/out —
this firmware doesn't transmit, so it's optional here.)

```
DIN-5 IN pin 4 ── 220Ω ───── H11L1 (or 6N138) pin 1 (anode)
DIN-5 IN pin 5 ───────────── H11L1 pin 2 (cathode)
        1N4148 across the opto LED, reverse-parallel:*
        anode to pin 2 side, cathode to pin 1 side
H11L1 pin 6 (Vcc) ── 3.3V (Feather 3V pin)
H11L1 pin 4 (GND) ── GND
H11L1 pin 5 (output) ── Feather D0 (RX), with 470Ω pull-up to 3.3V
DIN-5 IN pin 2 ── cable shield only: leave unconnected at this end
                  (the opto IS the isolation; do not tie the two
                  devices' grounds together here)
```
\* Standard practice: the 1N4148 sits across the opto LED (reverse
protection) — anode to H11L1 pin 2 side, cathode to H11L1 pin 1 side.
H11L1 is preferred over 6N138 at 3.3 V (it has a logic output and
works reliably at the lower voltage); with a 6N138 use a 1 kΩ
pull-up and a base resistor is not needed for MIDI speeds.

TRS MIDI (Type A) instead: IN tip = pin-5 side, ring = pin-4 side,
sleeve = shield.

## Parts list (DIY route, roughly)

| Part | Qty | ~Cost |
|---|---|---|
| DIN-5 female socket (panel or PCB mount) | 1 | $1 |
| H11L1 optocoupler (+ 8-pin socket) | 1 | $1 |
| 220 Ω, 470 Ω, 100 kΩ ×2 resistors | — | pennies |
| 1N4148 diodes | 3 | pennies |
| 1 µF capacitor (film/ceramic) | 1 | pennies |
| 3.5 mm jack (line-in) + 10 kΩ pot | 1 | $1 |
| JST-PH 3-pin pigtail for D3 | 1 | $1 |
| Small perfboard | 1 | $1 |

## Sanity checks before first power

1. Continuity: no shorts between 3.3 V and GND on your board.
2. UTIL screen shows **A9 bias ≈ 2048/4095** with nothing plugged in
   (the firmware prints the boot measurement there). Way off from
   ~2048 = your bias divider is wrong; fix that before blaming audio.
3. MIDI IN: with nothing plugged in, D0/RX should sit HIGH (3.3 V).
4. First run with **UTIL → SOURCE = DEMO PLUCK** so you know the
   output side works before debugging the input side.
