#pragma once

// Orthogonal cable router (D6 editor baseline, D7 routing heuristics; #69). Pure functions over
// the fixed slot geometry: an edge list in, per edge an axis-aligned polyline with rounded corners,
// distinct channel lanes, spread port stubs, a gain-knob anchor and a stable colour index. No JUCE,
// no painting, no state.
#include "iupac/ui/Layout.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace iupac::ui
{
struct CableEdge
{
    std::size_t source{};      // index into slotTable (never outputSlot)
    std::size_t destination{}; // index into slotTable (never a source slot); outputSlot for the OUT bus
    double gain{};
    std::string_view sourceId;      // stable patch node IDs: colour identity only
    std::string_view destinationId; // "output" for the OUT bus
};

struct ChannelLane
{
    int channel{-1}; // inter-column channel index, -1 when unused
    int lane{-1};
};

struct Cable
{
    std::size_t edge{};                 // index into the routed edge span
    std::vector<Point> points;          // axis-aligned polyline, first point on the OUT border, last on the IN border
    double cornerRadius{};              // rounding applied at every interior corner
    std::size_t knobSegment{};          // index of the longest horizontal segment (points[i] -> points[i + 1])
    Point knobAnchor{};                 // midpoint of that segment
    double gain{};
    int colourIndex{};                  // index into cablePalette
    bool backward{};                    // routed through the return channel below the field
    ChannelLane firstChannel{};         // vertical run leaving the source column
    ChannelLane secondChannel{};        // vertical run entering the destination column (spanning/backward cables only)
};

struct Rgb
{
    std::uint8_t r{}, g{}, b{};
};

// Cable colour set: eight hues, each >= 3:1 contrast against the off-white ground (0xF4F1EA).
inline constexpr std::array<Rgb, 8> cablePalette {{
    {0xB3, 0x26, 0x1E}, {0xB8, 0x5C, 0x00}, {0x7A, 0x6A, 0x00}, {0x1E, 0x7A, 0x3C},
    {0x00, 0x7A, 0x7A}, {0x1F, 0x5F, 0xBF}, {0x6B, 0x3F, 0xBF}, {0xB0, 0x23, 0x7A},
}};
inline constexpr Rgb offWhiteGround {0xF4, 0xF1, 0xEA};

inline constexpr double maximumCornerRadius = 8.0;
inline constexpr double maximumStubSpread = 8.0; // reference units between neighbouring stubs at one port
inline constexpr double channelInset = 6.0;      // reference units kept clear at both channel edges

// The gain knob is an overlay of fixed pixel size at every window size, centred on `knobAnchor`.
inline constexpr double knobDiameterPixels = 20.0;
inline constexpr double knobHalfExtentPixels = knobDiameterPixels / 2.0 + 0.5; // + the hairline ring
// Corridor lanes above and below the field therefore keep the knob's half extent clear of the field
// border, in reference units at the smallest scale the field is ever drawn at (#88): a lane inset of
// `channelInset` put the outermost lane ~4 px from the border, so the knob was clipped by the field
// edge and the macro strip above it. The corridors are 70 and 90 units tall, so the wider inset
// still leaves a lane band.
inline constexpr double corridorInset = knobHalfExtentPixels / minimumFieldVerticalScale;

// Deterministic from the stable edge identity so a cable keeps its colour across edits.
[[nodiscard]] int cableColourIndex(std::string_view sourceId, std::string_view destinationId) noexcept;

// Routes every edge; edges referencing a slot without the required port are skipped (no cable).
[[nodiscard]] std::vector<Cable> routeCables(std::span<const CableEdge> edges, const std::array<Slot, slotCount>& slots = slotTable);

// Number of proper crossings between segments of distinct cables (diagnostic/baseline only).
[[nodiscard]] std::size_t countCrossings(std::span<const Cable> cables) noexcept;

// WCAG relative-luminance contrast ratio between two colours.
[[nodiscard]] double contrastRatio(Rgb a, Rgb b) noexcept;
}
