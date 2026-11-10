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

Silt uses JUCE under the AGPLv3, so the plug-ins built from this repository are subject
to the AGPLv3's terms as well as the GPLv3's. JUCE's licence file lists the further
components it includes. Catch2, used only by the tests, isn't part of the plug-ins.

The four factory samples in `resources/samples/` were recorded for Silt and are covered by
the same GPLv3 licence as the code.
