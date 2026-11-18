#!/bin/sh
# Puts what the app loads into web/public (not in git): the exported JSON and the
# WebAssembly build (from ../build-web: web/engine/build.sh and silt_export_web),
# the fonts and their licences, and the factory samples.
set -e
cd "$(dirname "$0")/.."
root=..
out=public
mkdir -p "$out/data" "$out/fonts" "$out/samples"
for f in params layout help presets; do
  [ -f "$root/build-web/$f.json" ] || { echo "missing build-web/$f.json: run silt_export_web build-web" >&2; exit 1; }
  cp "$root/build-web/$f.json" "$out/data/"
done
[ -f "$root/build-web/silt.wasm" ] || { echo "missing build-web/silt.wasm: run web/engine/build.sh" >&2; exit 1; }
cp "$root/build-web/silt.wasm" "$out/"
cp "$root"/resources/fonts/*.ttf "$root"/resources/fonts/OFL-*.txt "$out/fonts/"
cp "$root"/kit/fonts/GlyphGrotesque-Regular.ttf "$root"/kit/fonts/OFL-GlyphGrotesque.txt "$out/fonts/"
cp "$root"/resources/samples/*.flac "$out/samples/"
[ "$1" = "--quiet" ] || echo "assets in web/$out"
# Brood's loops, for LOAD's FROM BROOD while developing (on jackherz.com they load from Brood's
# own copy): from a Brood checkout beside this one, or BROOD_DEMOS. The build leaves them out.
brood="${BROOD_DEMOS:-$root/../Brood-Source/assets/demos}"
if [ -d "$brood" ]; then mkdir -p "$out/brood-loops" && cp "$brood"/*.flac "$out/brood-loops/"; fi
