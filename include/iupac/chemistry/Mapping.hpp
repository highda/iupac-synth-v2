#pragma once

#include "iupac/chemistry/Analysis.hpp"
#include "iupac/domain/Patch.hpp"

#include <array>
#include <optional>
#include <string>

namespace iupac::chemistry
{
inline constexpr int sonicIntentVersion = 1;
inline constexpr int mappingVersion = 1;

struct StructuralDetail
{
    double bondOrderMean{};
    double bondOrderSpread{};
    double heteroPlacement{};
    double motifPlacement{};
};

struct SonicIntent
{
    double density{}, brightness{}, rigidity{}, roughness{}, decay{}, harmonicity{}, motion{};
    StructuralDetail detail;
};

struct GenerationResult
{
    std::optional<SonicIntent> intent;
    std::optional<domain::Patch> patch;
    juce::var trace;
    std::string error;
    explicit operator bool() const noexcept { return patch.has_value(); }
};

[[nodiscard]] SonicIntent project(const Analysis&);
[[nodiscard]] juce::var encodeSonicIntent(const SonicIntent&);
[[nodiscard]] GenerationResult generate(const Analysis&);
}
