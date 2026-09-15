#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace iupac::chemistry
{
inline constexpr int protocolVersion = 1;
inline constexpr int analysisVersion = 1;
inline constexpr std::size_t maximumResponseBytes = 256 * 1024;

struct Analysis
{
    std::string canonicalIsomericSmiles;
    std::string rdkitVersion;
    std::string opsinVersion;
    std::array<std::size_t, 10> elementCounts{}; // C,N,O,S,P,F,Cl,Br,I,other
    std::size_t heavyAtoms{};
    int formalCharge{};
    juce::var descriptors;
    juce::var detail;
};

struct AnalysisResult
{
    std::optional<Analysis> value;
    std::string error;
    explicit operator bool() const noexcept { return value.has_value(); }
};

// Decodes a successful helper response and enforces the finite Analysis v1 shape.
[[nodiscard]] AnalysisResult decodeAnalysisResponse(std::string_view json, std::string_view expectedRequestId);
}
