# Silt — short, copy-paste friendly entry points.
#
#   make install    build Silt and Silt FX and put them in your plug-in folders
#   make test       build, then run the unit tests
#   make web        build the browser version's engine and data (build-web, web/public)
#   make release    build the Universal installer (release/Silt-VERSION-macOS.pkg)
#   make all        install, then web
#   make clean      remove every build folder (all of them can be rebuilt)
#
# Needs: Xcode command line tools, and `brew install cmake ninja emscripten`
# (emscripten only for `make web`). The first build downloads JUCE and takes a while.

SHELL := /bin/bash
BUILD := build

.PHONY: help install build test web release all clean

help:
	@awk 'NR <= 12 && /^#/' Makefile | sed 's/^# *//'

build:
	cmake -S . -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD)

install: build
	@echo "Silt and Silt FX are in ~/Library/Audio/Plug-Ins. Restart your DAW and rescan."

test: build
	ctest --test-dir $(BUILD) --output-on-failure

web: build
	mkdir -p build-web
	$(BUILD)/silt_export_web_artefacts/Release/silt_export_web build-web
	sh web/engine/build.sh
	cd web && npm install && sh scripts/assets.sh

release:
	bash scripts/package-macos.sh

all: install web

clean:
	rm -rf build build-release build-web web/dist
	@echo "removed the build folders"
