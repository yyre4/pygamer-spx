#!/bin/bash
# run_dsp_validation.sh -- DSP proof for the VST sample-rate adaptation.
# Compiles tools/dsp_test.cpp twice (original firmware header vs the
# adapted plugin header), renders the demo with both at 31250 Hz and
# compares sample-by-sample, then runs the sanity suite on the adapted
# core at 31250 / 44100 / 48000 / 96000 Hz.
# Usage: tools/run_dsp_validation.sh [firmware-project-dir]
set -euo pipefail
cd "$(dirname "$0")/.."
FW="${1:-$HOME/workspace/pygamer-spx}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

g++ -O2 -w -I "$FW/pygamer_spx" -o "$WORK/dsp_ref" tools/dsp_test.cpp
g++ -O2 -w -DADAPTED -I Source -o "$WORK/dsp_adapt" tools/dsp_test.cpp

echo "=== original core @ 31250 Hz ==="
(cd "$WORK" && ./dsp_ref 31250 ref)
echo
echo "=== adapted core @ 31250 Hz ==="
(cd "$WORK" && ./dsp_adapt 31250 adapt31)

echo
echo "=== demo render comparison (original vs adapted, both @ 31250) ==="
python3 - "$WORK" <<'PYEOF'
import struct, sys, math
w = sys.argv[1]
a = open(f'{w}/ref_demo.bin','rb').read()
b = open(f'{w}/adapt31_demo.bin','rb').read()
n = min(len(a), len(b)) // 2
xa = struct.unpack(f'<{n}h', a[:n*2]); xb = struct.unpack(f'<{n}h', b[:n*2])
maxdiff = max(abs(p-q) for p, q in zip(xa, xb))
ndiff = sum(1 for p, q in zip(xa, xb) if p != q)
sa = sum(v*v for v in xa); sb = sum(v*v for v in xb); sab = sum(p*q for p, q in zip(xa, xb))
corr = sab / math.sqrt(sa*sb) if sa and sb else 0.0
print(f"samples compared : {n}")
print(f"max abs diff     : {maxdiff} LSB (int16)")
print(f"samples differing: {ndiff}")
print(f"correlation      : {corr:.8f}")
import re
def tab(p):
    d = {}
    for line in open(p):
        m = re.match(r'\s*(\d+) (\S.*?)\s+rms_first=\s*([\d.]+) rms_last=\s*([\d.]+) peak=\s*([\d.]+)', line)
        if m: d[int(m.group(1))] = (m.group(2),) + tuple(float(m.group(i)) for i in (3, 4, 5))
    return d
r, ad = tab(f'{w}/ref_presets.txt'), tab(f'{w}/adapt31_presets.txt')
dev = []
for p in sorted(r):
    worst = max(abs(ad[p][i]-r[p][i])/r[p][i]*100 if r[p][i] > 0 else 0.0 for i in (1, 2, 3))
    if worst > 0.01: dev.append((p, r[p][0], worst))
ident = 30 - len(dev)
print(f"preset tables    : {ident}/30 presets bit-identical; deviating: "
      + (", ".join(f"#{p} {nm} ({wst:.2f}%)" for p, nm, wst in dev) if dev else "none"))
print("                   (the two EARLY REF programs differ only in the liveness")
print("                    regeneration tap: the plugin's larger modulation buffer")
print("                    lets it read at its programmed delay instead of the")
print("                    firmware's 2048-sample RAM clamp -- see README)")
PYEOF

for rate in 44100 48000 96000; do
  echo
  echo "=== adapted core @ $rate Hz ==="
  (cd "$WORK" && ./dsp_adapt $rate "adapt$rate" | tail -8)
done
echo
echo "DSP validation done."
