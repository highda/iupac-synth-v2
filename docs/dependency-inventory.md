# Development dependency inventory

Recorded for issue #2 on 2026-09-14 in the Debian 12 arm64 development container, Linux aarch64. The recorded local image ID is `sha256:842f28507613659dce63aa88c3fe22520c3ddb80b3a1225b9da0f38126b239b7`; this is an image ID, not a registry digest.

| Input | Exact observed or locked value | License / role |
| --- | --- | --- |
| JUCE | 8.0.15, `91ad83ae34a81e0833b1a2b0866f54846370ae53` | AGPL-3.0 or commercial; product framework |
| pluginval | v1.0.4, `ed19c2c16b57a6d94db391bea3ef4a80b769d5bf`; `modules/juce` at `5179f4e720d8406ebd1b5401c86aea8da6cc83c9` | GPL-3.0; test-only validator and its recursive source input |
| CMake / Ninja | 3.25.1-1 / 1.11.1-2~deb12u1 | system build tools |
| GCC / G++ | 12.2.0-3 (`gcc`/`g++` metapackages); compiler reports 12.2.0-14+deb12u1 | system C++20 compiler |
| Clang | 1:14.0-55.7~deb12u1; compiler reports 14.0.6 | sanitizer compiler option |
| ALSA / JACK dev | 1.2.8-1+b1 / 1.9.21~dfsg-3 | LGPL-2.1-or-later / GPL-2.0-or-later; Linux audio build inputs |
| X11 / Xrandr / Xinerama / Xcursor | 2:1.8.4-2+deb12u2 / 2:1.5.2-2+b1 / 2:1.1.4-3 / 1:1.2.1-1 | upstream X11-family licenses; native GUI build inputs |
| FreeType / Mesa GL | 2.12.1+dfsg-5+deb12u4 / 22.3.6-1+deb12u2 | FTL or GPL-2.0 / MIT-family; text and graphics build inputs |
| Xvfb / xauth | 2:21.1.7-3+deb12u13 / 1:1.1.2-1 | X11-family; headless GUI and validator testing |
| RDKit | Debian `python3-rdkit=202209.3-1`; runtime reports 2022.09.3 | BSD-3-Clause plus bundled notices; canonical Analysis helper |
| OPSIN | 2.8.0 release CLI JAR, source `0a09a99c121af3a0fdab404ff71a3bc0a8eee446`, SHA-256 `d25bc08f41b8f6fcd6f35e18ab83f3b8d9218cdb003d55c5f74aaefe2e0c68ab` | MIT plus transitive JAR notices; name-to-structure only |
| PyInstaller / hooks | 6.22.3 / 2026.7, wheel SHA-256 `312da84b...954e4` / `24257a04...408a` (full hashes in `packaging/pyinstaller-requirements.txt`) | GPL-2.0 with bootloader exception / Apache-2.0 and GPL components; build-only onedir freezer |
| AKWF-FREE (Adventure Kid Waveforms) | curated subset, upstream revision `8de90bf94376670947369e69de0af6b9fbd19286`, per-file SHA-256 in `third_party/akwf/manifest.json` | CC0-1.0 (licence vendored); single-cycle waveforms embedded as the sampled tables of the `wavetable` source (D12) |
| OpenJDK | Debian `openjdk-17-jdk-headless=17.0.20.1+1-1~deb12u1`; jdeps-selected runtime modules recorded per package | GPL-2.0 with Classpath Exception; private jlink runtime for OPSIN |

The authoritative selected dependency policy and distribution caveat remain in [TOOLCHAIN.md](architecture/TOOLCHAIN.md). Chemistry tools present in the development image are intentionally absent from the synth-only CMake dependency graph and are not end-user prerequisites.

pluginval's pinned CMake enables multiple host formats unconditionally. The reproducible Linux build applies the checked `cmake/pluginval-linux.patch` to disable unused LADSPA hosting while retaining the required VST3 host; this avoids adding an unrelated LADSPA SDK to the product environment.
