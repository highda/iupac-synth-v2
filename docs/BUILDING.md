# Building IUPAC Synth 2

The canonical build platform is Debian 12 arm64; the required packages are listed in [TOOLCHAIN.md](architecture/TOOLCHAIN.md#development-environment). Native macOS arm64 builds are described [below](#native-macos-arm64).

The initial product configuration is chemistry-independent:

```sh
cmake --preset linux-synth
cmake --build --preset linux-synth --parallel 4
ctest --preset linux-synth
./build/linux-synth/iupac-cli
```

This builds the domain and engine libraries, CLI, tests, VST3, native Standalone, and editor. `IUPAC_ENABLE_CHEMISTRY=OFF` is explicit in the preset. The configure has no chemistry fetch, lookup, link, or bundle step.

Headless production-path operations are scriptable JSON commands:

```sh
./build/linux-synth/iupac-cli inspect --stage catalog
./build/linux-synth/iupac-cli inspect --patch patch.json --stage patch
./build/linux-synth/iupac-cli inspect --snapshot sound.json --stage effective
./build/linux-synth/iupac-cli render --snapshot sound.json --midi events.json --sample-rate 48000 --block-size 128 --output out.wav
./build/linux-synth/iupac-cli verify-panel --panel data/panels/authored-synth/panel.json --output-dir out/authored
./build/linux-synth/iupac-cli benchmark --snapshot sound.json --seconds 60
python3 tools/render_distance.py first.wav second.wav --latency 4
```

The benchmark fixes 48 kHz, 128-frame blocks, 16 sustained voices and performs an internal five-second warmup before the timed interval. Use `--seconds 60` for V8. WAV and manifest outputs belong under ignored `out/`.

For the initial sanitizer smoke:

```sh
cmake --preset linux-sanitize
cmake --build --preset linux-sanitize --target iupac-tests --parallel 4
ctest --preset linux-sanitize
```

Run the coordinator/mailbox race gate separately from ASan/UBSan:

```sh
cmake --preset linux-tsan
cmake --build --preset linux-tsan --target iupac-tests --parallel 4
ctest --preset linux-tsan
```

Build the test-only validator from its recursively pinned source inputs with:

```sh
./scripts/build-pluginval.sh
```

The source revisions are recorded in `cmake/Dependencies.lock`. On Linux the script applies the checked `cmake/pluginval-linux.patch`, which disables unused LADSPA hosting because that SDK is not an IUPAC build input; VST3 hosting remains enabled. On Darwin the pinned source builds unmodified and the executable lives inside `pluginval.app`. Builds write only below `build/`; installation is always a separate explicit operation.

## Native macOS arm64

The same code builds natively on macOS arm64. The Linux presets above remain canonical for V0–V11; macOS evidence never replaces them. Tool inventory: Xcode Command Line Tools (Apple clang 21, `xcode-select -p` = `/Library/Developer/CommandLineTools`), CMake 4.0.2, Ninja 1.13, Python 3.13 with NumPy (only for the `render-distance-metric` ctest), `auval` from the OS, jq, and the repository's `macos-26` GitHub-hosted runner (`macos-15` defaults to Xcode 16.4, whose libc++ has no `std::jthread`) (Xcode, Homebrew Ninja, a `build/venv` NumPy).

```sh
cmake --preset mac-synth
cmake --build --preset mac-synth --parallel 4
ctest --preset mac-synth
cmake --preset mac-release
cmake --build --preset mac-release --parallel 4
./scripts/build-pluginval.sh                      # pinned source, unpatched on Darwin
./scripts/macos-validate.sh build/mac-synth out/macos
```

`mac-synth` (chemistry OFF) builds `iupac-cli`, `iupac-tests`, the VST3, the AU component and the Standalone app for `arm64` only with `CMAKE_OSX_DEPLOYMENT_TARGET=12.0`; `mac-release` compiles the chemistry-ON product. Presets are Darwin-conditional and pick `python3` from `PATH` (`Python3_FIND_FRAMEWORK=LAST`). Every bundle is re-sealed with an ad-hoc signature as the final post-build step because JUCE's post-link manifest/plist steps otherwise invalidate the linker's seal; no Developer ID, hardened runtime, notarization or installer is produced. `scripts/macos-validate.sh` asserts Info.plist identity (`space.highda.iupacsynth2`, `aumu`/`Iup2`/`Iups`, no `Iup1`), runs pluginval strictness 5 on the VST3 and on the AU after copying the component to `~/Library/Audio/Plug-Ins/Components`, runs `auval -v aumu Iup2 Iups`, the native editor smoke, a bounded Standalone launch, and writes the artifacts with a `shasum -a 256` manifest under `out/macos/`. `.github/workflows/macos-arm64.yml` performs the same steps on `macos-26` and retains the artifacts and reports; its exact-commit run URL is the verifiable evidence.

`auval` and AU hosting need a console login session (a live `AudioComponentRegistrar`); over SSH or from a secondary account, read the AU results from the workflow run instead. To try unsigned downloaded workflow artifacts locally, strip quarantine first: `xattr -dr com.apple.quarantine "IUPAC Synth 2.vst3"` (likewise for the `.component` and `.app`). Gatekeeper still treats ad-hoc-signed bundles as unidentified; this is expected for these builds.

Build the target-native private chemistry payload explicitly with:

```sh
./packaging/build-runtime.sh out/iupac-chemistry build/package
./packaging/stage-artifact.sh out/iupac-chemistry out/example-artifact
```

The first command creates a PyInstaller `onedir` helper, a jlink OpenJDK 17
runtime, OPSIN, the offline discovery index, notices, and a machine-readable
verification report. The output and work directories must not already exist.
The report covers source/frozen parity, private-runtime environment isolation,
deadline process-tree cleanup, damage diagnostics, and relocation to a
read-only path containing spaces. `.github/workflows/package-arm64.yml` repeats
the build on the standard arm64 runner and runs the payload with networking
disabled in a separate minimal Debian image containing no external Python,
Java, RDKit, source tree, or build tools.

On macOS arm64 the same payload is built natively — the Linux payload never
establishes it — with Homebrew `python@3.11` and `openjdk@17` (install them with
`brew install python@3.11 openjdk@17 ninja jq shellcheck`; the payload build creates
its own pinned venv under `build/package-macos/venv`):

```sh
./packaging/build-runtime-macos.sh                 # out/iupac-chemistry-macos
./tools/verify_panel_parity.py --cli build/mac-release/iupac-cli \
    --helper-root "$PWD/out/iupac-chemistry-macos" --report out/macos/parity-report.json
./packaging/stage-product-macos.sh build/mac-release out/iupac-chemistry-macos \
    "$HOME/iupac-stage/IUPAC Synth 2 macOS"
./packaging/verify-installed-macos.sh "$HOME/iupac-stage/IUPAC Synth 2 macOS" \
    out/macos/v10-report.json
```

`packaging/macos-chemistry-requirements.txt` pins the hashed arm64 RDKit wheel of
Debian's `202209.3` line and NumPy on Debian's 1.24 line; `jdeps`/`jlink` use the
same module closure as the Linux payload. The staged prefix is one flat
distribution directory (the three bundles, `iupac-cli`,
`iupac-product-doctor`, `resources/chemistry`, `share/`), each bundle carrying the
whole payload under `Contents/Resources/chemistry` and re-sealed with an ad-hoc
signature; see the TOOLCHAIN Placement bullet for why a nested-code location is
not usable. `tools/verify_panel_parity.py` is the acceptance for the wheel pin:
all 107 frozen panel records must reproduce the canonical Linux arm64 digests in
`data/panels/cross-platform-parity-v1.json`, which is measured from the tested
Linux delivery on the canonical container.
`packaging/verify-installed-macos.sh` is the macOS V10 analogue and **must be run
against a prefix outside the checkout**: its `sandbox-exec` profile
(`packaging/macos-isolation.sb`) denies networking, the SIP-protected
interpreters, Homebrew, `/usr/local`, `/Library/Java`, the source tree and the
provisioning venv, and `realpath` cannot traverse a denied ancestor. On an
ephemeral runner `packaging/macos-clean-machine.sh` additionally deletes every
removable external Python/Java first; it refuses to run anywhere else. AU-hosted
checks (`auval`, pluginval on the `.component`) need the console session's
`AudioComponentRegistrar`, so outside a console login session the report
records them as pending rather than passed. Mind that `ctest --preset mac-release`
runs the production helper: put `build/package-macos/venv/bin` on `PATH` so the
payload's pinned interpreter closure is used, not a host NumPy 2 that makes RDKit
2022.09 warn on stderr.

The chemistry-enabled shell resolves the helper relative to the staged product
payload and uses the same Analysis/projection/generator as the editor:

```sh
cmake --preset linux-chemistry
cmake --build --preset linux-chemistry --parallel 4
./build/linux-chemistry/iupac-cli analyze --mode name --text ethanol --helper-root out/iupac-chemistry
./build/linux-chemistry/iupac-cli generate --mode smiles --text CCO --helper-root out/iupac-chemistry > ethanol.iupacpatch
./build/linux-chemistry/iupac-cli inspect --stage sonic --analysis analysis.json --request-id cli-1
./build/linux-chemistry/iupac-cli inspect --stage mapping --analysis analysis.json --request-id cli-1
./build/linux-chemistry/iupac-cli discover --query ethanol --prefix 1 --limit 16 --helper-root out/iupac-chemistry
./build/linux-chemistry/iupac-cli record --record-id wikidata:Q100138042@2475457273 --helper-root out/iupac-chemistry
./build/linux-chemistry/iupac-cli cache-inspect --helper-root out/iupac-chemistry
./build/linux-chemistry/iupac-cli cache-get --key SHA256 --helper-root out/iupac-chemistry
./build/linux-chemistry/iupac-cli cache-clear --helper-root out/iupac-chemistry
```

`generate` emits an exact State v1 snapshot with base/edited Patch, retained
controls and inert chemistry provenance. Rendering or restoring that file never
starts the helper or remaps the sound.

The consolidated pre-delivery engineering gate is:

```sh
./scripts/verify-release.sh
```

It builds and tests the canonical chemistry-enabled `linux-release` preset,
reruns the complete chemistry-OFF V0 gate, runs ASan/UBSan and TSan separately,
and writes a machine-readable identity and gate report beneath
`out/verification/<full-commit>/report.json`. Set `IUPAC_PACKAGE_REPORT` to a
private-runtime `package-report.json` to integrate its checks. Installed-format
V10 remains the delivery gate. If the container kernel prevents
TSan from starting, set `IUPAC_TSAN_RUN_ID` to a successful exact-commit run of
the repository's pinned arm64 TSan workflow; the script verifies that run via
GitHub before recording it.

Discovery searches the bundled SQLite snapshot only. Ambiguous results require
an explicit record selection in the editor. Generated base sounds are cached as
ordinary State v1 `.iupacpatch` files, keyed by canonical identity and mapping
versions; reopening a cached entry loads that stored file, while Apply performs
an explicit current-mapper regeneration. Cache clear never touches the ordinary
user preset directory.

Stage a complete relocatable product only into a new explicit prefix:

```sh
./packaging/stage-product.sh build/linux-release out/iupac-chemistry 'out/IUPAC Synth 2'
'out/IUPAC Synth 2/bin/iupac-product-doctor' 'out/IUPAC Synth 2'
```

This separate install operation never runs during configure or build. The staged
prefix and compressed sibling contain exact hash manifests, CLI, Standalone,
VST3, factory sound, offline data, private runtimes, notices and the user guide.
The arm64 package workflow builds this prefix from the pinned Debian base and
runs V10 without network or external Python/Java before exporting the exact
tested archive.

## Cutting a release

`.github/workflows/prerelease.yml` publishes the exact CI-tested artifacts of both platforms as a GitHub release. Dispatch only on `main`; pre-release tags increase by one per cut, and `final=true` publishes the full release:

```sh
gh workflow run prerelease.yml --ref main -f tag=v2.0.0-preview.N        # optional: -f notes='…'
gh workflow run prerelease.yml --ref main -f tag=v2.0.0 -f final=true    # full release
gh run watch "$(gh run list --workflow prerelease.yml --limit 1 --json databaseId --jq '.[0].databaseId')"
```

`.github/workflows/prerelease.yml` refuses any ref other than `main`, a tag not matching `v2.0.0-preview.N` (or `v2.0.0` with `final`), or an existing tag/release; lints itself (pinned actionlint, shellcheck) and dry-runs `scripts/test-release-notes.sh` and `scripts/test-prerelease-assemble.sh`; then calls `package-arm64.yml` and `macos-arm64.yml` (`workflow_call`) at the dispatched commit, so the Linux delivery and the macOS bundles are rebuilt and gated for that one SHA — never downloaded from another commit. `scripts/prerelease-assemble.sh` recomputes SHA-256 of every attachment and asserts it against `delivery-report.json`/the `.sha256` sidecar (Linux, plus `v10-report.json` status `pass` and the commit) and against the macOS `manifest.sha256`/`artifacts.sha256`, re-extracts the macOS archive to prove it holds the validated bundle bytes with executable bits, then writes one `SHA256SUMS`. A lightweight tag is created at that exact SHA and `gh release create --prerelease --verify-tag` attaches: `IUPAC-Synth-2-linux-arm64.tar.gz` (byte-identical to the tested `IUPAC Synth 2.tar.gz`; GitHub rewrites spaces in asset names) with `.sha256`, `linux-delivery-report.json`, `linux-v10-report.json`, `linux-install-report.json`; `IUPAC-Synth-2-macos-arm64.tar.gz` (bsdtar, bundle structure/symlinks/modes preserved, written by `scripts/macos-validate.sh`) with `.sha256`, `macos-manifest.sha256`, `macos-artifacts.sha256`, `macos-pluginval-vst3.log`, `macos-pluginval-au.log`, `macos-auval.txt`, `macos-product.json`, `macos-runner-identity.txt`; and `SHA256SUMS`. Release notes are generated by `scripts/release-notes.sh` (commit, run URLs, per-file hashes, per-platform contents including the macOS chemistry OFF/ON state read from `macos-product.json`, install paths, quarantine instruction, user-guide link). A final `roundtrip` job (`.github/workflows/prerelease-roundtrip.yml`, also dispatchable alone with `gh workflow run prerelease-roundtrip.yml --ref main -f tag=v2.0.0-preview.N` for an existing tag) downloads every asset with `gh release download`, runs `sha256sum -c SHA256SUMS`, runs the product doctor and an offline name analysis on the Linux archive inside the `runtime-base` stage of `packaging/ProductDockerfile` with networking disabled, and re-checks the macOS archive bytes; a failing gate anywhere means no release. The `final: true` input publishes tag `v2.0.0` as a full (non-pre-) release.

To try a downloaded macOS archive locally: `tar -xzf IUPAC-Synth-2-macos-arm64.tar.gz`, then `xattr -dr com.apple.quarantine "IUPAC Synth 2.vst3" "IUPAC Synth 2.component" "IUPAC Synth 2.app" iupac-cli` before the first launch or scan (see the quarantine note above).
