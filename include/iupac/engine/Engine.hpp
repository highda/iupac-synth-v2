#pragma once

#include "iupac/domain/Patch.hpp"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>

namespace iupac::engine
{
inline constexpr std::size_t maximumModuleBlockSize = 4096;
inline constexpr unsigned internalOversamplingFactor = 2;
[[nodiscard]] constexpr double internalSampleRate(double outputSampleRate) noexcept { return outputSampleRate * internalOversamplingFactor; }

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
};

class ModuleProcessor final
{
public:
    explicit ModuleProcessor(domain::ModuleType type = domain::ModuleType::mixer) noexcept : type_(type) {}
    void prepare(double internalSampleRate, std::size_t maximumBlockSize);
    void reset() noexcept;
    void noteOn(int midiNote, int midiChannel, std::uint32_t patchSeed, std::uint32_t nodeHash) noexcept;
    void noteOff() noexcept {} // E1 owns the release; source state continues until voice retirement/reset.
    void process(const ModuleValues&, float fundamentalHz, std::span<const float> inputLeft,
                 std::span<const float> inputRight, std::span<float> outputLeft,
                 std::span<float> outputRight) noexcept;

private:
    static float clampFinite(float value, float lo, float hi, float fallback) noexcept;
    float nextNoise() noexcept;
    domain::ModuleType type_;
    double sampleRate_{96000.0};
    std::array<double, 16> phases_{};
    double modPhase_{};
    std::uint32_t randomState_{1};
    std::array<float, 2> pink_{};
    std::size_t burstSamplesRemaining_{};
    bool gate_{};
    std::unique_ptr<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>> comb_;
    juce::dsp::StateVariableTPTFilter<float> filter_;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 4> modes_;
};

class ModulatorBank final
{
public:
    void prepare(double sampleRate) noexcept;
    void configure(const std::array<domain::Envelope, 3>&, const std::array<domain::Lfo, 2>&) noexcept;
    void noteOn() noexcept;
    void noteOff() noexcept;
    [[nodiscard]] std::array<float, 5> next() noexcept;
    [[nodiscard]] bool finalEnvelopeActive() const noexcept { return envelopes_[0].isActive(); }
private:
    std::array<juce::ADSR, 3> envelopes_;
    std::array<domain::Lfo, 2> lfoSettings_{};
    std::array<double, 2> lfoPhases_{};
    double sampleRate_{48000.0};
};

class Engine final
{
public:
    void prepare(double sampleRate, std::size_t maximumBlockSize) noexcept;
    void renderSilence(std::span<float> left, std::span<float> right) noexcept;

    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::size_t maximumBlockSize() const noexcept { return maximumBlockSize_; }

private:
    double sampleRate_ = 0.0;
    std::size_t maximumBlockSize_ = 0;
};
}
