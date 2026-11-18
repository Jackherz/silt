#!/bin/sh
# Builds Silt's sound (core/ + SiltWeb.cpp) to WebAssembly: build-web/silt.wasm.
# Needs Emscripten (brew install emscripten). Run from anywhere.
set -e
cd "$(dirname "$0")/../.."
mkdir -p build-web
EXPORTS=$(grep -o 'SILT_EXPORT [a-z0-9_* ]* \(silt_[a-z0-9_]*\)' web/engine/SiltWeb.cpp | sed 's/.* \(silt_[a-z0-9_]*\)$/_\1/' | sort -u | paste -sd, -)
em++ -std=c++20 -O3 -DNDEBUG -Icore/include -Ikit/include core/src/*.cpp web/engine/SiltWeb.cpp \
    --no-entry -sSTANDALONE_WASM -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 \
    -sEXPORTED_FUNCTIONS="$EXPORTS" \
    -o build-web/silt.wasm
ls -l build-web/silt.wasm
