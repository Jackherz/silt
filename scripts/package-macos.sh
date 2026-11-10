#!/bin/bash
# Silt and Silt FX: one Universal (Apple Silicon + Intel) installer package for macOS,
# with each as a VST3, an Audio Unit, a CLAP and a standalone app. Run by the release
# workflow, or by hand:
#
#   bash scripts/package-macos.sh [VERSION]      # -> release/Silt-VERSION-macOS.pkg
#
# The bundles carry ad-hoc signatures only (no Apple Developer account), so the
# package isn't notarised: macOS asks once, in System Settings > Privacy & Security,
# before it will open it. Files the Installer puts in place aren't quarantined, so
# hosts load the plug-ins without further steps.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
VERSION="${1:-$(git describe --tags --always --dirty)}"
VERSION="${VERSION#v}"
BUILD="${SILT_RELEASE_BUILD:-$ROOT/build-release}"
OUT="$ROOT/release"
WORK="$OUT/work"
PKG="$OUT/Silt-$VERSION-macOS.pkg"

[ "$(uname -s)" = Darwin ] || { echo "error: macOS only" >&2; exit 1; }

echo "==> Building Silt $VERSION, Universal"
generator=()
command -v ninja >/dev/null 2>&1 && generator=(-G Ninja)
cmake -S . -B "$BUILD" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DSILT_COPY_AFTER_BUILD=OFF ${SILT_CMAKE_ARGS:-}
targets=()
for plugin in Silt SiltFX; do
  for format in VST3 AU CLAP Standalone; do targets+=("${plugin}_${format}"); done
done
cmake --build "$BUILD" --config Release --parallel --target "${targets[@]}"

rm -rf "$WORK"
P="$WORK/root/Library/Audio/Plug-Ins"
mkdir -p "$P/VST3" "$P/Components" "$P/CLAP" "$WORK/root/Applications"
for plugin in Silt SiltFX; do
  name=$([ "$plugin" = Silt ] && echo "Silt" || echo "Silt FX")
  from="$BUILD/${plugin}_artefacts/Release"
  ditto "$from/VST3/$name.vst3" "$P/VST3/$name.vst3"
  ditto "$from/AU/$name.component" "$P/Components/$name.component"
  ditto "$from/CLAP/$name.clap" "$P/CLAP/$name.clap"
  ditto "$from/Standalone/$name.app" "$WORK/root/Applications/$name.app"
done

echo "==> Signing (ad hoc) and checking every binary is Universal"
for bundle in "$P"/*/* "$WORK"/root/Applications/*.app; do
  codesign --force --sign - --timestamp=none "$bundle"
  codesign --verify --strict "$bundle"
  binary="$bundle/Contents/MacOS/$(defaults read "$bundle/Contents/Info.plist" CFBundleExecutable)"
  archs="$(lipo -archs "$binary")"
  case "$archs" in *arm64*x86_64*|*x86_64*arm64*) ;; *) echo "error: $binary is only $archs" >&2; exit 1 ;; esac
  echo "    $(basename "$bundle"): $archs, signed"
done

# Installed bundles stay where the package puts them: without this, Installer moves an
# update to wherever it finds an older copy (a developer's build folder, say).
pkgbuild --analyze --root "$WORK/root" "$WORK/components.plist" >/dev/null
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/components.plist" >/dev/null 2>&1; do
  /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$WORK/components.plist" 2>/dev/null || true
  /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$WORK/components.plist"
  i=$((i + 1))
done

pkgbuild --root "$WORK/root" --component-plist "$WORK/components.plist" \
  --identifier com.siltaudio.pkg --version "$VERSION" --install-location / "$WORK/Silt.pkg"

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Silt $VERSION</title>
    <domains enable_localSystem="true" enable_currentUserHome="true"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <license file="LICENSE"/>
    <choices-outline><line choice="silt"/></choices-outline>
    <choice id="silt" title="Silt and Silt FX"><pkg-ref id="com.siltaudio.pkg"/></choice>
    <pkg-ref id="com.siltaudio.pkg" version="$VERSION">Silt.pkg</pkg-ref>
</installer-gui-script>
XML
mkdir -p "$WORK/resources" && cp LICENSE "$WORK/resources/LICENSE"

productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" \
  --package-path "$WORK" "$PKG"
rm -rf "$WORK"
echo "==> $PKG ($(du -h "$PKG" | cut -f1))"
