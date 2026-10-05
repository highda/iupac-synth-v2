#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace iupac::engine
{
// D12 (#166). The wavetable source reads immutable tables of `wavetableFrames` single-cycle frames
// that the `position` control morphs across. Two kinds exist:
//   - eight **synthesized** tables, built here from formulas (harmonic series, pulse trains,
//     formant envelopes, windowed sync, two-operator FM);
//   - twenty-four **sampled** tables, each up to sixteen cycles of one instrument family from
//     Adventure Kid Waveforms (AKWF-FREE, CC0-1.0), vendored under `third_party/akwf` and embedded
//     by `scripts/build-wavetables.py`.
// Every frame is analysed into its harmonic series and stored once per octave band
// (`wavetableLevels`), truncated to the harmonics that band can carry, so playback never has to
// filter and never aliases into the audible range.
inline constexpr std::size_t synthesizedWavetableCount = 8;
inline constexpr std::size_t sampledWavetableCount = 24;
inline constexpr std::size_t wavetableCount = synthesizedWavetableCount + sampledWavetableCount;
inline constexpr std::size_t wavetableFrames = 16;
inline constexpr std::size_t wavetableLevels = 11;        // 1024, 512, ... 1 harmonics
inline constexpr std::size_t wavetableMaximumSize = 2048;
inline constexpr std::size_t sampledCycleSamples = 600;   // the AKWF original format
// Level k carries 1024 >> k harmonics. The upper bands hold few harmonics, so they are stored
// shorter — never below thirty-two samples per stored harmonic, which keeps the linear read's
// images far under the signal — and a table costs about 0.8 MiB instead of 1.4.
[[nodiscard]] constexpr std::size_t wavetableLevelSize(std::size_t level) noexcept { return level <= 4 ? wavetableMaximumSize : wavetableMaximumSize >> (level - 4); }
[[nodiscard]] constexpr std::size_t wavetableLevelHarmonics(std::size_t level) noexcept { return (wavetableMaximumSize / 2) >> level; }
// Offset of a level inside one frame; every level carries one guard sample for the interpolated read.
[[nodiscard]] constexpr std::size_t wavetableLevelOffset(std::size_t level) noexcept { std::size_t o = 0; for (std::size_t k = 0; k < level; ++k) o += wavetableLevelSize(k) + 1; return o; }
inline constexpr std::size_t wavetableFrameStride = wavetableLevelOffset(wavetableLevels);
// Catalog order of the `table` choice. Append only: a stored index must keep naming its table.
inline constexpr std::array<std::string_view, wavetableCount> wavetableNames{
    "analog", "harmonics", "pwm", "formant", "organ", "sync", "fm", "digital",
    "cello", "violin", "flute", "clarinet", "oboe", "altosax", "piano", "epiano",
    "eorgan", "aguitar", "eguitar", "ebass", "dbass", "voice", "fmsynth", "chip",
    "vgame", "granular", "overtone", "blended", "distorted", "handdrawn", "theremin", "clavinet"};

// One sampled table as embedded: `frames` cycles (2..16) of signed 16-bit PCM.
struct SampledWavetable
{
    std::size_t frames;
    std::array<std::array<std::int16_t, sampledCycleSamples>, wavetableFrames> cycles;
};
extern const std::array<SampledWavetable, sampledWavetableCount> sampledWavetables;

struct Wavetable
{
    std::vector<float> samples;
    [[nodiscard]] const float* frame(std::size_t frame, std::size_t level) const noexcept
    {
        return samples.data() + frame * wavetableFrameStride + wavetableLevelOffset(level);
    }
};

// Builds one table on first use (about 0.8 MiB, a few milliseconds) and returns it. Never call
// this from the audio thread: the patch compiler does, for every table a patch names, so a
// published patch always finds its tables built.
const Wavetable& wavetable(std::size_t table);
// The audio thread's accessor: the built table or null. It never builds and never allocates.
[[nodiscard]] const Wavetable* wavetableIfBuilt(std::size_t table) noexcept;
}
