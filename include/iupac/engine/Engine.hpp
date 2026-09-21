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
// D8 effects tail (#123). The chorus and the delay are C3 — each is backed by one prepared stereo
// delay line, allocated once for the whole global tail and never per voice. The chorus sweep never
// reaches past `chorusBase + chorusSweep` milliseconds and the delay's declared range stops at
// 2000 ms; both capacities carry a small margin so the interpolated read never runs off the end.
inline constexpr double maximumChorusDelaySeconds = 0.025;
inline constexpr double maximumDelaySeconds = 2.05;
// D8 reverb (#124). D9 delegates the network topology; this one is a fixed Schroeder/FDN hybrid
// built entirely from the pinned `juce::dsp::DelayLine`: one stereo pre-delay, two diffusion
// allpasses and a four-line feedback delay network mixed by a normalized Hadamard matrix, with a
// one-pole damper inside each line's feedback path. It is C4 — one prepared network for the whole
// global tail, never per voice. The line lengths below are the longest each line ever reads, at
// `size` 1; `size` scales them down to `reverbSmallestSizeScale` and nothing ever reads past the
// capacity allocated from these constants, so render allocates nothing.
inline constexpr std::size_t reverbLineCount = 4;
inline constexpr double maximumReverbPreDelaySeconds = 0.205;
inline constexpr std::array<double, reverbLineCount> reverbLineSeconds{0.0297, 0.0371, 0.0411, 0.0437};
inline constexpr std::array<double, 2> reverbDiffuserSeconds{0.0051, 0.0077};
inline constexpr float reverbSmallestSizeScale = 0.3f;
inline constexpr float reverbDiffusion = 0.6f;
// The right channel reads each line slightly short of the left one, which is what decorrelates the
// two sides of the network. Strictly below 1, so it can never read past the allocated capacity.
inline constexpr float reverbRightSkew = 0.971f;
// `damping` 0 leaves the one-pole coefficient at exactly 1 — the damper is then a straight wire.
inline constexpr float reverbDampingRange = 0.92f;
// Anything smaller than this in a feedback state is flushed to zero, so a decayed tail cannot leave
// the network grinding on denormals for minutes after the last note.
inline constexpr float denormalFloor = 1e-20f;
// Tempo sync with no host playhead. ARCHITECTURE "Tempo sync": Standalone and every CLI render fall
// back to this, and the render manifest records it.
inline constexpr double fallbackTempoBpm = 120.0;
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
    int unisonVoices{1}, octave{}, coarse{}, waveform{}, curve{}, voices{2}, syncMode{}, syncDivision{4};
};

// The prepared reverb network. It is a plain value type owned by whoever prepares the tail (the
// engine for a real patch, the processor itself for a standalone module test); `process()` runs one
// sample and touches nothing but its own members.
class ReverbNetwork final
{
public:
    using Line = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;
    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset() noexcept;
    // Controls arrive in catalog units and are assumed already clamped to their declared ranges.
    void process(float inputLeft, float inputRight, float size, float decaySeconds, float damping,
                 float preDelayMilliseconds, float width, float& wetLeft, float& wetRight) noexcept;

private:
    void updateGeometry(float size, float decaySeconds) noexcept;
    double sampleRate_{96000.0};
    Line preDelay_;
    std::array<Line, 2> diffusers_;
    std::array<Line, reverbLineCount> lines_;
    std::array<float, reverbLineCount> damperLeft_{}, damperRight_{};
    std::array<float, reverbLineCount> lineSamples_{}, lineGain_{};
    std::array<float, 2> diffuserSamples_{};
    float preDelayCeiling_{1.0f};
    float cachedSize_{std::numeric_limits<float>::quiet_NaN()}, cachedDecay_{std::numeric_limits<float>::quiet_NaN()};
};

class ModuleProcessor final
{
public:
    using CombDelay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;
    explicit ModuleProcessor(domain::ModuleType type = domain::ModuleType::mixer) noexcept : type_(type) {}
    void setType(domain::ModuleType type) noexcept { type_ = type; reset(); }
    void setCombDelay(CombDelay* delay) noexcept { comb_ = delay; }
    // The prepared delay line a chorus or delay node reads. The engine's global tail owns one of
    // each and hands the pointer here at compile time, so publishing a patch allocates nothing.
    void setEffectDelay(CombDelay* delay) noexcept { effect_ = delay; }
    // The prepared reverb network a reverb node reads, handed in by the tail exactly like the line
    // above. Null for every other type.
    void setReverbNetwork(ReverbNetwork* network) noexcept { reverb_ = network; }
    // Host tempo for `syncMode` = sync. Out-of-range or absent tempo is the documented 120 BPM.
    void setTempo(double beatsPerMinute) noexcept { tempo_ = beatsPerMinute >= 20.0 && beatsPerMinute <= 999.0 ? beatsPerMinute : fallbackTempoBpm; }
    void prepare(double internalSampleRate, std::size_t maximumBlockSize);
    void reset() noexcept;
    void noteOn(int midiNote, int midiChannel, std::uint32_t patchSeed, std::uint32_t nodeHash) noexcept;
    void noteOff() noexcept {}
    // `audioRate*` is the D8 typed IN port (#127): the signal cabled into `fm.modIn` or
    // `resonator.exciteIn`. Empty spans are the uncabled port and cost nothing — no branch inside
    // the per-sample loop runs and the node renders exactly as it did before D8.
    void process(const ModuleValues&, float fundamentalHz, std::span<const float> inputLeft,
                 std::span<const float> inputRight, std::span<float> outputLeft,
                 std::span<float> outputRight, std::span<const float> audioRateLeft = {},
                 std::span<const float> audioRateRight = {}) noexcept;

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
    // D8 effects tail (#123). `effect_` is the chorus/delay line; the damping state is the one-pole
    // in the delay's feedback path. A chorus tap's LFO rides on `phases_[0]`, which no effect type
    // otherwise uses, so the effects add four floats and one pointer to a node's storage.
    CombDelay* effect_{};
    std::unique_ptr<CombDelay> ownedEffect_;
    ReverbNetwork* reverb_{};
    std::unique_ptr<ReverbNetwork> ownedReverb_;
    double tempo_{fallbackTempoBpm};
    float effectDampLeft_{}, effectDampRight_{};
    juce::dsp::StateVariableTPTFilter<float> filter_;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 4> modes_;
    // D8/D9 filter additions (#126). `ladder24` is a four-pole TPT cascade with a saturated global
    // feedback path, so it needs four one-pole states per channel and nothing else; the JUCE TPT
    // filter above still serves the three 12 dB modes and the notch. The two cached coefficients and
    // the drive normalizer are resolved in the same throttled control block the cutoff uses, because
    // process() runs once per sample.
    std::array<float, 8> ladderState_{};
    std::array<float, 2> ladderLast_{};
    float ladderG_{}, ladderFeedback_{}, filterR2_{1.414f}, driveNormalizer_{1.0f};
    float cachedFilterDrive_{std::numeric_limits<float>::quiet_NaN()};
    // One sample of the 24 dB ladder for one channel: input saturation folds the resonant feedback
    // in, so the output can never leave [-1, 1] however high `q` and `drive` are driven.
    float ladderSample(std::size_t channel, float input) noexcept;
};

// Per-voice modulator sources. D8 (#125) adds the stage curves, the four new LFO shapes, the LFO
// fade-in and LFO tempo sync; every one of them is the literal identity at its default, so a
// pre-D8 patch produces bit-identical modulation.
class ModulatorBank final
{
public:
    void prepare(double sampleRate) noexcept;
    // `patchSeed` is the patch's own noise seed: the two random LFO shapes draw from it and never
    // from wall-clock entropy, which is what keeps a repeat render bit-identical (V2).
    void configure(const std::array<domain::Envelope, domain::envelopeCount>&, const std::array<domain::Lfo, domain::lfoCount>&, std::uint32_t patchSeed = 0) noexcept;
    // Host tempo for an LFO in `sync`; out-of-range or absent tempo is the documented 120 BPM.
    void setTempo(double beatsPerMinute) noexcept;
    void reset() noexcept;
    void noteOn() noexcept;
    void noteOff() noexcept;
    // E1..E3 then L1..L2 then E4, matching the persisted source order: the leading block keeps its
    // positions and `e4` appends, exactly as the source id list does.
    [[nodiscard]] std::array<float, 6> next() noexcept;
    [[nodiscard]] bool finalEnvelopeActive() const noexcept { return envelopes_[0].isActive(); }
private:
    // One stage-curve shaper per envelope. It rides on the JUCE ADSR rather than replacing it: the
    // ADSR still owns the timing and the raw linear value, and the shaper bends that value inside
    // the stage it is in. A curve of exactly 0 returns the ADSR sample untouched, so the default
    // path is the previous code's arithmetic bit for bit.
    struct StageShaper
    {
        float attackWarp{}, decayWarp{}, releaseWarp{};   // exp() bend strengths, 0 = straight
        float attackNorm{1.0f}, decayNorm{1.0f}, releaseNorm{1.0f};
        float sustain{1.0f};
        float lastValue{}, lastShaped{}, releaseFrom{}, releaseShaped{};
        bool attacking{}, releasing{};
    };
    [[nodiscard]] float shapeEnvelope(std::size_t index, float value) noexcept;
    [[nodiscard]] float lfoRate(const domain::Lfo&) const noexcept;
    // Everything the per-sample loop would otherwise recompute from settings that only change when
    // a patch is published or the tempo moves: the sync-resolved rates and the fade length. The
    // modulator loop runs once per sample per voice, so none of it belongs in `next()`.
    void updateDerived() noexcept;
    std::array<juce::ADSR, domain::envelopeCount> envelopes_;
    std::array<StageShaper, domain::envelopeCount> shapers_{};
    std::array<domain::Lfo, domain::lfoCount> lfoSettings_{}, previousLfoSettings_{};
    std::array<double, domain::lfoCount> lfoPhases_{};
    // Deterministic per-LFO random stream for `sampleHold` and `randomSmooth`: one held value, the
    // one before it (the ramp `randomSmooth` interpolates across) and the xorshift state itself.
    std::array<float, domain::lfoCount> lfoHold_{}, lfoPreviousHold_{};
    std::array<float, domain::lfoCount> rateHz_{1.0f, 1.0f}, previousRateHz_{1.0f, 1.0f};
    std::array<double, domain::lfoCount> fadeSamples_{};
    // True when any envelope declares a non-zero stage curve. False is the pre-D8 path: the JUCE
    // ADSR sample is returned untouched and the shaper does no bookkeeping at all.
    bool curvesActive_{};
    std::array<std::uint32_t, domain::lfoCount> lfoRandom_{1, 1};
    std::uint32_t patchSeed_{};
    double sampleRate_{48000.0}, tempo_{fallbackTempoBpm};
    std::uint64_t smoothingSample_{}, smoothingLength_{1}, sampleSinceNoteOn_{};
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
// `port` is the destination's IN port: `in` for the ordinary summed input, `modIn`/`exciteIn` for
// the D8 typed audio-rate inputs (#127). It is an ordinary audio edge either way — it counts
// against the 48-edge cap, takes part in the same cycle check and carries no enable flag.
struct CompiledEdge { std::uint8_t source{}, destination{}; float gain{}; bool toOutput{}; domain::AudioPort port{}; };
struct CompiledRow { domain::ModulationSource source{}; std::uint8_t node{}; ParameterTarget target{}; float depth{}, minimum{}, maximum{}; domain::ParameterScale scale{}; };
// Compiled rows are the patch's 40 explicit matrix rows plus the implicit filter panel shortcuts
// (#126): `keytrack` and `envAmount` on each of the two filter slots compile into this same array as
// ordinary keyTracking->cutoff and E2->cutoff rows, so they are summed and clamped by the one
// existing pass rather than by a second modulation path. The Patch cap itself is unchanged at 40.
inline constexpr std::size_t implicitFilterRows = 4;
inline constexpr std::size_t compiledRowCapacity = domain::maximumMatrixRows + implicitFilterRows;
struct CompiledTarget { std::uint8_t node{}; ParameterTarget target{}; float minimum{}, maximum{}; domain::ParameterScale scale{}; std::array<std::uint8_t, compiledRowCapacity> rows{}; std::uint8_t rowCount{}; };
struct CompiledEdgeList { std::array<std::uint8_t, domain::maximumEdges> edges{}; std::uint8_t count{}; };
struct CompiledPatch
{
    std::array<CompiledNode, domain::maximumNodes> nodes{};
    std::array<CompiledEdge, domain::maximumEdges> edges{};
    std::array<CompiledRow, compiledRowCapacity> rows{};
    std::array<CompiledTarget, compiledRowCapacity> targets{};
    std::array<CompiledEdgeList, domain::maximumNodes> incomingEdges{};
    // Edges into the OUT bus split by region: the per-voice ones are gated by E1 and velocity inside
    // each voice, the tail ones are summed once per sample after the global effects tail has run.
    CompiledEdgeList outputEdges{}, tailOutputEdges{};
    std::array<domain::Envelope, domain::envelopeCount> envelopes{};
    std::array<domain::Lfo, domain::lfoCount> lfos{};
    std::uint32_t noiseSeed{};
    std::uint8_t nodeCount{}, edgeCount{}, rowCount{}, targetCount{};
    // Matrix targets are partitioned so the per-voice ones come first: [0, voiceTargetCount) is the
    // pass each voice runs and [voiceTargetCount, targetCount) is the global tail's. Neither loop
    // pays a test for the other's rows, which matters because the voice pass runs per sample.
    std::uint8_t voiceTargetCount{};
    // Nodes [tailStart, nodeCount) are the effects region: prepared once in the global post-mixer
    // tail, never per voice. The compiler guarantees no per-voice node depends on them.
    std::uint8_t tailStart{};
};
// Node identity as published in EffectiveValues::nodeIds and the matrix target of a catalog parameter id (display lookups).
[[nodiscard]] std::uint32_t hashNodeId(std::string_view) noexcept;
[[nodiscard]] std::optional<ParameterTarget> parameterTarget(std::string_view) noexcept;
struct CompileResult { CompiledPatch patch{}; std::string error; explicit operator bool() const noexcept { return error.empty(); } };
[[nodiscard]] CompileResult compilePatch(const domain::Patch&);
using ModulationInputs = std::array<float, domain::modulationSourceCount>;
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
    // Host tempo for the tail's tempo-synced delay. Safe from the audio thread: it stores one double
    // per tail node and allocates nothing.
    void setTempo(double beatsPerMinute) noexcept;
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
