# Third-party notices

Silt's own code is licensed under the GNU General Public License, version 3
([LICENSE](LICENSE)). It builds with the following, each under its own licence:

| Component | Licence | Text |
|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) 8.0.14 | AGPLv3 (or a commercial JUCE licence) | [JUCE-LICENSE.md](licenses/JUCE-LICENSE.md), [JUCE-AGPL-3.0.txt](licenses/JUCE-AGPL-3.0.txt) |
| VST3 SDK, inside JUCE | MIT | [VST3-SDK-LICENSE.txt](licenses/VST3-SDK-LICENSE.txt) |
| [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) | MIT | [clap-juce-extensions-LICENSE.md](licenses/clap-juce-extensions-LICENSE.md) |
| [CLAP](https://github.com/free-audio/clap), inside clap-juce-extensions | MIT | [CLAP-LICENSE.txt](licenses/CLAP-LICENSE.txt) |
| VT323 and Glyph Grotesque fonts, in `kit/fonts/` | SIL Open Font License 1.1 | `kit/fonts/OFL-*.txt` |
| B612 Mono, Overpass Medium and Chivo Mono fonts, in `resources/fonts/` | SIL Open Font License 1.1 | `resources/fonts/OFL-*.txt` |

Silt uses JUCE under the AGPLv3, so the plug-ins built from this repository are subject
to the AGPLv3's terms as well as the GPLv3's. JUCE's licence file lists the further
components it includes. Catch2, used only by the tests, isn't part of the plug-ins.

The four factory samples in `resources/samples/` were recorded for Silt and are covered by
the same GPLv3 licence as the code.

## The browser version (`web/`)

The browser version is Silt's own C++ (`core/`) compiled to WebAssembly with
[Emscripten](https://emscripten.org) 7.0, with a React page round it. It does not use JUCE.
The built page contains, each under its own licence:

| Component | Licence | Text |
|---|---|---|
| Emscripten's runtime and system libraries, compiled into `silt.wasm` | MIT or University of Illinois/NCSA | [Emscripten-LICENSE.txt](licenses/Emscripten-LICENSE.txt) |
| musl libc, as Emscripten builds it | MIT | [musl-COPYRIGHT.txt](licenses/musl-COPYRIGHT.txt) |
| LLVM libc++, libc++abi and compiler-rt, as Emscripten builds them | Apache 2.0 with LLVM exceptions | [libcxx-LICENSE.txt](licenses/libcxx-LICENSE.txt), [libcxxabi-LICENSE.txt](licenses/libcxxabi-LICENSE.txt), [compiler-rt-LICENSE.txt](licenses/compiler-rt-LICENSE.txt) |
| [React](https://react.dev) 18.3.1 and React DOM (with its scheduler) | MIT | [React-LICENSE.txt](licenses/React-LICENSE.txt) |
| Glyph Grotesque, B612 Mono, Overpass Medium and Chivo Mono fonts | SIL Open Font License 1.1 | `kit/fonts/OFL-GlyphGrotesque.txt`, `resources/fonts/OFL-*.txt` |

The four factory samples ship with the browser version under the GPLv3, as with the plug-ins.
Vite and the build tools aren't part of the built page.
