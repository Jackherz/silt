#!/bin/sh
# Renders the M1 listening set: three sources (sine, drum loop, speech)
# through the engine with the recipes from research doc 01 §4.
#   tools/render/make-listening-set.sh        # after building silt_render
# Output: build/listen/*.wav (sources in build/listen/in/)
set -e
cd "$(dirname "$0")/../.."
RENDER=build/silt_render_artefacts/Release/silt_render
OUT=build/listen
mkdir -p "$OUT/in"

# Sources: a 220 Hz sine and a synthesised 120 BPM drum loop (stdlib only),
# plus speech from macOS's text-to-speech.
python3 -I - "$OUT/in" <<'EOF'
import math, random, struct, sys, wave
out, sr = sys.argv[1], 48000

def write(name, samples):
    with wave.open(f"{out}/{name}", "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s)) * 32767)) for s in samples))

write("sine.wav", [0.5 * math.sin(2 * math.pi * 220 * i / sr) for i in range(sr * 3)])

random.seed(3)
beat = sr // 2                                   # 120 BPM
loop = [0.0] * (beat * 8)                        # two bars
def add(at, sound):
    for i, s in enumerate(sound):
        if at + i < len(loop): loop[at + i] += s
kick = [0.9 * math.sin(2 * math.pi * (50 + 120 * math.exp(-i / 900)) * i / sr) * math.exp(-i / 6000) for i in range(sr // 3)]
snare = [0.5 * random.uniform(-1, 1) * math.exp(-i / 2500) + 0.3 * math.sin(2 * math.pi * 190 * i / sr) * math.exp(-i / 1500) for i in range(sr // 4)]
hat = [0.25 * random.uniform(-1, 1) * math.exp(-i / 400) for i in range(sr // 20)]
for b in range(8):
    add(b * beat, kick if b % 2 == 0 else snare)
    add(b * beat + beat // 2, hat)
    add(b * beat, hat)
write("drums.wav", loop)
EOF
say -v Samantha -o "$OUT/in/speech.wav" --file-format=WAVE --data-format=LEI16@48000 \
    "Grains of sound, scattered across time, settle into something new."

r() { name=$1; shift; "$RENDER" --out "$OUT/$name.wav" "$@"; }

r sine-plain        --in "$OUT/in/sine.wav"   --hold 3
r sine-octave-up    --in "$OUT/in/sine.wav"   --hold 3 --note 72 --set size=60 --set rate=40
r sine-spray        --in "$OUT/in/sine.wav"   --hold 3 --set spray=0.4 --set reverse=0.5
r speech-stretch    --in "$OUT/in/speech.wav" --hold 12 --set scan=0.3 --set size=120 --set rate=30 --set spray=0.01
r speech-pad        --in "$OUT/in/speech.wav" --hold 8 --set start=0.3 --set scan=0.05 --set size=250 --set rate=24 --set spray=0.25
r speech-down-fifth --in "$OUT/in/speech.wav" --hold 5 --note 53
r drums-plain       --in "$OUT/in/drums.wav"  --hold 4
r drums-glitch      --in "$OUT/in/drums.wav"  --hold 4 --set size=20 --set rate=12 --set spray=0.4 --set reverse=0.3
r drums-chops       --in "$OUT/in/drums.wav"  --hold 4 --set size=125 --set rate=8
r drums-reverse     --in "$OUT/in/drums.wav"  --hold 4 --set size=250 --set rate=8 --set reverse=1
r drums-oneshot     --in "$OUT/in/drums.wav"  --hold 6 --set start=0.5 --set end=stop
