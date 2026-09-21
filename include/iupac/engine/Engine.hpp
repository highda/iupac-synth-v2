#pragma once

#include "iupac/domain/Patch.hpp"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <limits>
#include <span>
#include <string>

namespace iupac::engine
{
inline constexpr std::size_t maximumModuleBlockSize = 4096;
inline constexpr unsigned internalOversamplingFactor = 2;
inline constexpr std::size_t maximumVoices = 16;
// D8 unison: a unison-capable source renders up to this many detuned copies of itself. Polyphony
// stays `maximumVoices` — unison multiplies oscillator work per voice, never the voice count — so
// the per-copy oscillator banks below are fixed-size members, sized once at compile time.
inline constexpr std::size_t maximumUnisonVoices = 7;
inline constexpr std::size_t maximumMidiEventsPerBlock = 4096;
[[nodiscard]] constexpr double internalSampleRate(double outputSampleRate) noexcept { return outputSampleRate * internalOversamplingFactor; }
[[nodiscard]] constexpr float filterCutoffCeiling(double outputSampleRate) noexcept { return static_cast<float>(outputSampleRate * 0.4); }

struct ModuleValues
{
    std::array<float, 16> amplitudes{};
    std::array<float, 16> ratios{};
    std::array<float, 16> pans{};
    std::array<float, 4> modeRatios{1.0f, 2.0f, 3.0f, 4.0f};
    std::array<float, 4> modeLevels{};
    float carrierRatio{1.0f}, modulatorRatio{1.0f}, index{};
    float burstMilliseconds{80.0f}, tuneRatio{1.0f}, combFeedback{0.4f}, modalQ{3.0f};
    float cutoff{1000.0f}, q{0.707f}, drive{1.0f}, wet{1.0f};
    float level{1.0f}, pan{}, outputLevel{1.0f};
    int mode{}, color{};
    // D8 additions. Names are shared where two types declare the same id (filter/shaper `drive`,
    // chorus/delay `feedback`, delay/reverb `damping`, reverb/width `width`): a node has one type,
    // so one field per id is the whole storage that node ever needs.
    float detuneCents{12.0f}, unisonSpread{0.5f}, phaseRandom{}, drift{};
    float harmonicityMorph{}, oddEvenBalance{}, symmetry{0.5f};
    float fine{}, keytrack{1.0f}, modInDepth{}, exciteDepth{}, envAmount{};
    float rate{0.5f}, depth{0.3f}, feedback{}, mix{0.3f};
    float timeMs{375.0f}, spread{}, damping{0.4f};
    float size{0.5f}, decaySeconds{2.0f}, preDelayMs{20.0f}, width{1.0f}, bassMonoHz{120.0f};
    int unisonVoices{1}, octave{}, coarse{}, waveform{}, curve{}, voices{2}, syncMode{}, syncDivision{};
};

class ModuleProcessor final
{
public:
    using CombDelay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;
    explicit ModuleProcessor(domain::ModuleType type = domain::ModuleType::mixer) noexcept : type_(type) {}
    void setType(domain::ModuleType type) noexcept { type_ = type; reset(); }
    void setCombDelay(CombDelay* delay) noexcept { comb_ = delay; }
    void prepare(double internalSampleRate, std::size_t maximumBlockSize);
    void reset() noexcept;
    void noteOn(int midiNote, int midiChannel, std::uint32_t patchSeed, std::uint32_t nodeHash) noexcept;
    void noteOff() noexcept {}
    void process(const ModuleValues&, float fundamentalHz, std::span<const float> inputLeft,
                 std::span<const float> inputRight, std::span<float> outputLeft,
                 std::span<float> outputRight) noexcept;

private:
    static float clampFinite(float value, float lo, float hi, float fallback) noexcept;
    float nextNoise() noexcept;
    // Recomputes the per-copy detune ratios and pan/level gains when `unisonVoices`, `detuneCents`
    // or `unisonSpread` move. D9 delegates the curve: copies sit at evenly spaced positions across
    // [-1, 1], detuned by `position * detuneCents` and panned to `position * unisonSpread`, with a
    // 1/sqrt(voices) level compensation. One voice is the exact identity — multiplier and gains are
    // literal 1, so the pre-D8 signal reproduces bit for bit.
    void updateUnison(int voices, float detuneCents, float spread) noexcept;
    domain::ModuleType type_;
    double sampleRate_{96000.0};
    using UnisonBank = std::array<std::array<float, 16>, maximumUnisonVoices>;
    static constexpr UnisonBank unitCosine() noexcept { UnisonBank b{}; for (auto& copy : b) copy.fill(1.0f); return b; }
    std::array<double, maximumUnisonVoices> phases_{}, modPhases_{};
    UnisonBank harmonicSin_{}, harmonicCos_{unitCosine()}, harmonicDeltaSin_{}, harmonicDeltaCos_{};
    // Per-copy unison state. `detuneMultiplier_` is a frequency ratio, the pan gains already carry
    // the 1/sqrt(voices) compensation, and the phase draws are taken once per note-on from the
    // patch-seeded stream below so `phaseRandom` never reaches for wall-clock entropy.
    std::array<float, maximumUnisonVoices> detuneMultiplier_{}, unisonPanLeft_{}, unisonPanRight_{}, unisonPhase_{}, unisonModPhase_{};
    float cachedDetuneCents_{std::numeric_limits<float>::quiet_NaN()}, cachedUnisonSpread_{std::numeric_limits<float>::quiet_NaN()};
    std::uint8_t cachedUnisonVoices_{};
    bool unisonDirty_{true}, pendingPhaseOffset_{};
    // `drift`: two slow deterministic wanders per voice (pitch and level), seeded per note-on.
    double driftPhase_{}, driftLevelPhase_{}, driftRate_{0.11}, driftLevelRate_{0.07};
    std::array<float, 16> cachedRatios_{{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}};
    // D8 spectral shape (#121): the ratios the recursion actually runs at and the amplitudes the
    // sum actually uses, both derived per block from the stored arrays and never written back.
    std::array<float, 16> shapedAmplitudes_{};
    float cachedHarmonicFundamental_{-1.0f};
    std::uint32_t randomState_{1};
    std::array<float, 2> pink_{};
    std::array<float, 16> panLeft_{}, panRight_{}, cachedPans_{};
    float cachedPan_{std::numeric_limits<float>::quiet_NaN()}, cachedPanLeft_{}, cachedPanRight_{};
    float cachedFundamental_{std::numeric_limits<float>::quiet_NaN()}, cachedFmRolloff_{1.0f};
    float cachedFilterCutoff_{std::numeric_limits<float>::quiet_NaN()}, cachedFilterQ_{std::numeric_limits<float>::quiet_NaN()};
    std::array<float, 4> cachedModeCutoffs_{{-1,-1,-1,-1}};
    float cachedModalQ_{std::numeric_limits<float>::quiet_NaN()};
    std::uint8_t filterControlCountdown_{};
    std::uint16_t harmonicRenormalizeCountdown_{4096};
    std::size_t burstSamplesRemaining_{};
    bool gate_{};
    // The MIDI note the current voice is playing: `keytrack` needs the note itself, not the
    // fundamental, because masterTune and bend are already folded into the frequency. It sits in
    // the padding after `gate_` so adding it moves no other member and leaves the hot harmonic
    // arrays above on the cache lines they already occupied.
    int note_{60};
    // D8 pitch block (#122). The engine calls process() once per sample, so the transposition is
    // resolved to one frequency multiplier and recomputed only when one of its four controls or the
    // note moves; `fine` is matrix-modulatable, so that can be per sample, but normally is not.
    int cachedPitchOctave_{}, cachedPitchCoarse_{}, cachedPitchNote_{60};
    float cachedPitchFine_{}, cachedPitchKeytrack_{1.0f}, cachedPitchMultiplier_{1.0f};
    CombDelay* comb_{};
    std::unique_ptr<CombDelay> ownedComb_;
    juce::dsp::StateVariableTPTFilter<float> filter_;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 4> modes_;
};

class ModulatorBank final
{
public:
    void prepare(double sampleRate) noexcept;
    void configure(const std::array<domain::Envelope, domain::envelopeCount>&, const std::array<domain::Lfo, domain::lfoCount>&) noexcept;
    void reset() noexcept;
    void noteOn() noexcept;
    void noteOff() noexcept;
    // E1..E3 then L1..L2, the leading block of ModulationSource. E4 is stored and reset with the
    // bank but is not yet a matrix source (issue #125 appends `e4` to the persisted source list).
    [[nodiscard]] std::array<float, 5> next() noexcept;
    [[nodiscard]] bool finalEnvelopeActive() const noexcept { return envelopes_[0].isActive(); }
private:
    std::array<juce::ADSR, domain::envelopeCount> envelopes_;
    std::array<domain::Lfo, domain::lfoCount> lfoSettings_{}, previousLfoSettings_{};
    std::array<double, domain::lfoCount> lfoPhases_{};
    double sampleRate_{48000.0};
    std::uint64_t smoothingSample_{}, smoothingLength_{1};
};

// Every matrix-eligible (continuous, modulatable) catalog parameter, in catalog order. The original
// fourteen keep their positions so a stored effective-value column never moves.
enum class ParameterTarget : std::uint8_t { carrierRatio, modulatorRatio, index, burstMilliseconds, tuneRatio, combFeedback, modalQ, cutoff, q, drive, wet, level, pan, outputLevel,
                                            detuneCents, unisonSpread, drift, harmonicityMorph, oddEvenBalance, symmetry, fine, keytrack, modInDepth, exciteDepth, envAmount,
                                            rate, depth, feedback, mix, timeMs, spread, damping, size, decaySeconds, preDelayMs, width, bassMonoHz };
inline constexpr std::size_t parameterTargetCount = static_cast<std::size_t>(ParameterTarget::bassMonoHz) + 1;
// Catalog range of one matrix-eligible scalar, resolved at compile time so the audio thread can normalize without catalog lookups.
struct CompiledRange { float minimum{}, maximum{1}; domain::ParameterScale scale{}; bool eligible{}; };
struct CompiledNode { domain::ModuleType type{}; ModuleValues values{}; std::uint32_t idHash{}; std::array<CompiledRange, parameterTargetCount> ranges{}; };
struct CompiledEdge { std::uint8_t source{}, destination{}; float gain{}; bool toOutput{}; domain::AudioPort port{}; };
struct CompiledRow { domain::ModulationSource source{}; std::uint8_t node{}; ParameterTarget target{}; float depth{}, minimum{}, maximum{}; domain::ParameterScale scale{}; };
struct CompiledTarget { std::uint8_t node{}; ParameterTarget target{}; float minimum{}, maximum{}; domain::ParameterScale scale{}; std::array<std::uint8_t, domain::maximumMatrixRows> rows{}; std::uint8_t rowCount{}; };
struct CompiledEdgeList { std::array<std::uint8_t, domain::maximumEdges> edges{}; std::uint8_t count{}; };
struct CompiledPatch
{
    std::array<CompiledNode, domain::maximumNodes> nodes{};
    std::array<CompiledEdge, domain::maximumEdges> edges{};
    std::array<CompiledRow, domain::maximumMatrixRows> rows{};
    std::array<CompiledTarget, domain::maximumMatrixRows> targets{};
    std::array<CompiledEdgeList, domain::maximumNodes> incomingEdges{};
    // Edges into the OUT bus split by region: the per-voice ones are gated by E1 and velocity inside
    // each voice, the tail ones are summed once per sample after the global effects tail has run.
    CompiledEdgeList outputEdges{}, tailOutputEdges{};
    std::array<domain::Envelope, domain::envelopeCount> envelopes{};
    std::array<domain::Lfo, domain::lfoCount> lfos{};
    std::uint32_t noiseSeed{};
    std::uint8_t nodeCount{}, edgeCount{}, rowCount{}, targetCount{};
    // Nodes [tailStart, nodeCount) are the effects region: prepared once in the global post-mixer
    // tail, never per voice. The compiler guarantees no per-voice node depends on them.
    std::uint8_t tailStart{};
};
// Node identity as published in EffectiveValues::nodeIds and the matrix target of a catalog parameter id (display lookups).
[[nodiscard]] std::uint32_t hashNodeId(std::string_view) noexcept;
[[nodiscard]] std::optional<ParameterTarget> parameterTarget(std::string_view) noexcept;
struct CompileResult { CompiledPatch patch{}; std::string error; explicit operator bool() const noexcept { return error.empty(); } };
[[nodiscard]] CompileResult compilePatch(const domain::Patch&);
using ModulationInputs = std::array<float, static_cast<std::size_t>(domain::ModulationSource::macro4) + 1>;
[[nodiscard]] ModuleValues applyModulation(const CompiledPatch&, std::size_t node,
                                            const ModuleValues&, const ModulationInputs&) noexcept;

// Block-rate publication of effective (post-summation, post-clamp) normalized parameter values per compiled node;
// -1 marks a target that is not a matrix-eligible parameter of that node's type.
struct EffectiveValues
{
    std::uint8_t nodeCount{};
    std::array<std::uint32_t, domain::maximumNodes> nodeIds{};
    std::array<std::array<float, parameterTargetCount>, domain::maximumNodes> values{};
};

enum class MidiEventType : std::uint8_t { noteOn, noteOff, pitchBend, controlChange };
struct MidiEvent { std::uint32_t sampleOffset{}; MidiEventType type{}; std::uint8_t channel{1}, data1{}, data2{}; std::uint16_t bend{8192}; };

class Engine final
{
public:
    Engine(); ~Engine(); Engine(Engine&&) noexcept; Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete; Engine& operator=(const Engine&) = delete;
    void prepare(double sampleRate, std::size_t maximumBlockSize) noexcept;
    void reset() noexcept; void setPatch(const CompiledPatch&); void setControls(const domain::HostControls&) noexcept;
    void updatePatchPreservingVoices(const CompiledPatch&) noexcept;
    void render(std::span<float> left, std::span<float> right, std::span<const MidiEvent> events = {}) noexcept;
    void renderSilence(std::span<float> left, std::span<float> right) noexcept;
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::size_t maximumBlockSize() const noexcept { return maximumBlockSize_; }
    [[nodiscard]] int latencySamples() const noexcept; [[nodiscard]] std::uint64_t guardHits() const noexcept;
    [[nodiscard]] std::uint64_t midiOverflowCount() const noexcept; [[nodiscard]] std::size_t activeVoiceCount() const noexcept;
    // Effective values of the newest-started active voice (D7 baseline), or the zero-modulation base when no voice is active.
    void effectiveValues(EffectiveValues&) const noexcept;
private:
    class Impl; std::unique_ptr<Impl> impl_; double sampleRate_{}; std::size_t maximumBlockSize_{};
};
}
