#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace iupac::domain
{
inline constexpr int patchVersion = 1;
inline constexpr int stateVersion = 1;
inline constexpr std::size_t maximumNodes = 16;
inline constexpr std::size_t maximumEdges = 48;
inline constexpr std::size_t maximumMatrixRows = 40;
inline constexpr std::size_t envelopeCount = 4;
inline constexpr std::size_t lfoCount = 2;
inline constexpr std::size_t generalSourceSlots = 3;
inline constexpr std::size_t maximumDocumentBytes = 1024 * 1024;
inline constexpr std::size_t maximumJsonDepth = 32;

// D8 appends five types; the original seven keep their order so a stored type id never moves.
enum class ModuleType { harmonic, fm, noise, resonator, filter, shaper, mixer, sub, chorus, delay, reverb, width };
inline constexpr std::size_t moduleTypeCount = 12;
// The IN ports a node declares. `in` is the ordinary summed stereo input every processor,
// effect and the OUT bus has; the two audio-rate inputs are the only typed extras (D8).
enum class AudioPort { in, modIn, exciteIn };
enum class ParameterScale { linear, logarithmic };
enum class ParameterKind { continuous, discrete, coefficientArray, convenience };

struct ParameterDescriptor
{
    std::string_view id;
    std::string_view unit;
    double minimum;
    double maximum;
    double defaultValue;
    ParameterScale scale;
    ParameterKind kind;
    bool modulatable;
    std::size_t arraySize{};
    double smoothingMilliseconds{20.0};
    std::vector<std::string_view> choices;
    // True for a parameter introduced after the original v1 catalog: a decoded node may omit it
    // and the decoder fills this default (ARCHITECTURE "Decoder compatibility for added parameters").
    bool postV1{};

    [[nodiscard]] double normalize(double value) const noexcept;
    [[nodiscard]] double denormalize(double normalized) const noexcept;
};

struct ModuleDescriptor
{
    ModuleType type;
    std::string_view id;
    bool source;             // no audio input on its `in` port
    std::size_t typeCap;
    bool sharedSourceSlot;   // counts against the three general source slots
    bool effects;            // compiled into the global post-mixer tail
    AudioPort audioRateInput; // the extra typed IN port this type declares, `in` when it has none
    std::vector<ParameterDescriptor> parameters;
};

[[nodiscard]] const std::array<ModuleDescriptor, moduleTypeCount>& moduleCatalog();
[[nodiscard]] std::string_view audioPortId(AudioPort) noexcept;
[[nodiscard]] bool declaresPort(const ModuleDescriptor&, AudioPort) noexcept;
[[nodiscard]] const ModuleDescriptor* findModule(std::string_view id) noexcept;
[[nodiscard]] const ParameterDescriptor* findParameter(const ModuleDescriptor&, std::string_view id) noexcept;

struct ParameterValue { std::string id; std::vector<double> values; };
struct Node { std::string id; ModuleType type{}; std::vector<ParameterValue> parameters; };
struct AudioEdge { std::string source; std::string destination; double gain{}; AudioPort port{AudioPort::in}; }; // destination "output" is the final bus
enum class ModulationSource { e1, e2, e3, l1, l2, velocity, keyTracking, pitchBend, cc1, macro1, macro2, macro3, macro4 };
struct MatrixRow { std::string id; bool enabled{}; ModulationSource source{}; std::string destinationNode; std::string destinationParameter; double depth{}; };
struct Envelope { double attack{0.01}, decay{0.1}, sustain{1.0}, release{0.2}; };
enum class LfoWaveform { sine, triangle };
struct Lfo { double rate{1.0}; LfoWaveform waveform{LfoWaveform::sine}; };
struct Macro { std::string label; double defaultValue{}; };

struct Patch
{
    std::uint32_t noiseSeed{};
    std::vector<Node> nodes;
    std::vector<AudioEdge> edges;
    std::array<Envelope, envelopeCount> envelopes{};
    std::array<Lfo, lfoCount> lfos{};
    std::vector<MatrixRow> matrix;
    std::array<Macro, 4> macros{};
};

[[nodiscard]] bool applyHarmonicSpectrum(Node&, double tilt, double inharmonicity) noexcept;

struct HostControls { std::array<double, 4> macros{}; double outputGain{-6.0}; double width{0.5}; double masterTune{}; bool bypass{}; };
struct State { Patch basePatch; Patch editedPatch; HostControls controls; std::optional<juce::var> provenance; };

struct DecodeResult { std::optional<Patch> value; std::string error; explicit operator bool() const noexcept { return value.has_value(); } };
struct StateDecodeResult { std::optional<State> value; std::string error; explicit operator bool() const noexcept { return value.has_value(); } };

[[nodiscard]] std::string validate(const Patch&);
[[nodiscard]] juce::var encodePatchValue(const Patch&);
[[nodiscard]] std::string encodePatchJson(const Patch&, bool pretty = false);
[[nodiscard]] DecodeResult decodePatchValue(const juce::var&);
[[nodiscard]] DecodeResult decodePatchJson(std::string_view);
[[nodiscard]] juce::var encodeStateValue(const State&);
[[nodiscard]] std::string encodeStateJson(const State&, bool pretty = false);
[[nodiscard]] StateDecodeResult decodeStateValue(const juce::var&);
[[nodiscard]] StateDecodeResult decodeStateJson(std::string_view);
}
