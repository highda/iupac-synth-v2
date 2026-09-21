#include "iupac/ui/CableRouter.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace iupac::ui
{
namespace
{
struct Plan
{
    std::size_t edge{};
    const Slot* from{};
    const Slot* to{};
    bool modulationInput{};
    bool backward{};
    bool spanning{};      // forward cable across more than one column: goes through the top corridor
    int channelA{};       // vertical run leaving the source column
    // Vertical run entering the destination column, used only when `usesChannelB` is set. Channel
    // -1 is the left margin, which is the channel a cable entering column 0 arrives through: the D8
    // typed inputs (#127) put eligible destinations in the source column for the first time, so a
    // backward cable can now end there. `channelExtent(-1)` already describes that band.
    int channelB{};
    bool usesChannelB{};
    double yOut{};        // stub height at the OUT port
    double yIn{};         // stub height at the IN port
    double xA{}, xB{};    // lane x in channels A/B
    double corridorY{};   // lane y in the top/bottom corridor
    int laneA{-1}, laneB{-1};
};

double spreadOffset(std::size_t index, std::size_t count, double height) noexcept
{
    if (count < 2) return 0.0;
    const double spacing = std::min(maximumStubSpread, (height - 16.0) / static_cast<double>(count - 1));
    return (static_cast<double>(index) - static_cast<double>(count - 1) / 2.0) * spacing;
}

double lanePosition(int lane, int count, double low, double high, double inset) noexcept
{
    const double usableLow = low + inset, usableHigh = high - inset;
    if (count < 2) return (usableLow + usableHigh) / 2.0;
    return usableLow + (usableHigh - usableLow) * static_cast<double>(lane) / static_cast<double>(count - 1);
}

// Vertical run of plan `p` in channel `channel`: entry/exit heights, used for lane ordering.
struct Run
{
    Plan* plan{};
    bool first{};
    double yStart{}, yEnd{};
};

std::uint32_t fnv1a(std::string_view text, std::uint32_t hash) noexcept
{
    for (const char c : text) hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    return hash;
}
}

int cableColourIndex(std::string_view sourceId, std::string_view destinationId) noexcept
{
    std::uint32_t hash = fnv1a(sourceId, 2166136261u);
    hash = (hash ^ 0x1fu) * 16777619u; // separator so ("ab","c") != ("a","bc")
    hash = fnv1a(destinationId, hash);
    return static_cast<int>(hash % static_cast<std::uint32_t>(cablePalette.size()));
}

std::vector<Cable> routeCables(std::span<const CableEdge> edges, const std::array<Slot, slotCount>& slots)
{
    std::vector<Plan> plans;
    plans.reserve(edges.size());
    for (std::size_t i = 0; i < edges.size(); ++i)
    {
        const auto& edge = edges[i];
        if (edge.source >= slots.size() || edge.destination >= slots.size()) continue;
        const Slot& from = slots[edge.source];
        const Slot& to = slots[edge.destination];
        if (!from.hasOutput() || edge.source == edge.destination) continue;
        if (!(edge.modulationInput ? to.hasModulationInput() : to.hasInput())) continue;
        Plan plan {i, &from, &to, edge.modulationInput};
        plan.backward = to.column <= from.column;
        plan.spanning = !plan.backward && to.column > from.column + 1;
        plan.channelA = from.column;
        plan.channelB = to.column - 1;
        plan.usesChannelB = plan.backward || plan.spanning;
        plans.push_back(plan);
    }

    // Fan-out / fan-in stubs: spread cables at each port ordered by the far end's height so stubs never coincide.
    for (std::size_t slot = 0; slot < slots.size(); ++slot)
    {
        std::vector<Plan*> outgoing, incoming, modulationIncoming;
        for (auto& plan : plans)
        {
            if (plan.from == &slots[slot]) outgoing.push_back(&plan);
            // The two IN anchors spread independently: a cable on the typed port never shifts the
            // stubs of the ordinary port, so adding one does not move an existing cable.
            if (plan.to == &slots[slot]) (plan.modulationInput ? modulationIncoming : incoming).push_back(&plan);
        }
        const auto byFarEnd = [](const Plan* a, const Plan* b, bool out)
        {
            const Slot* farA = out ? a->to : a->from;
            const Slot* farB = out ? b->to : b->from;
            const double ya = out ? (a->modulationInput ? farA->modulationInputAnchor().y : farA->inputAnchor().y) : farA->outputAnchor().y;
            const double yb = out ? (b->modulationInput ? farB->modulationInputAnchor().y : farB->inputAnchor().y) : farB->outputAnchor().y;
            if (ya != yb) return ya < yb;
            return a->edge < b->edge;
        };
        std::stable_sort(outgoing.begin(), outgoing.end(), [&](auto* a, auto* b) { return byFarEnd(a, b, true); });
        std::stable_sort(incoming.begin(), incoming.end(), [&](auto* a, auto* b) { return byFarEnd(a, b, false); });
        std::stable_sort(modulationIncoming.begin(), modulationIncoming.end(), [&](auto* a, auto* b) { return byFarEnd(a, b, false); });
        for (std::size_t i = 0; i < outgoing.size(); ++i)
            outgoing[i]->yOut = slots[slot].outputAnchor().y + spreadOffset(i, outgoing.size(), slots[slot].frame.height);
        // A slot with two IN anchors splits its left border between them: each anchor keeps the
        // symmetric band that reaches halfway to the other, so however wide a fan-in is on one port
        // its stubs can never coincide with the other port's (#127). A slot with only the ordinary
        // anchor keeps the full-height band it always had, so no pre-D8 cable moves.
        const double inputBand = slots[slot].hasModulationInput()
                                     ? slots[slot].modulationInputAnchor().y - slots[slot].inputAnchor().y
                                     : slots[slot].frame.height;
        for (std::size_t i = 0; i < incoming.size(); ++i)
            incoming[i]->yIn = slots[slot].inputAnchor().y + spreadOffset(i, incoming.size(), inputBand);
        for (std::size_t i = 0; i < modulationIncoming.size(); ++i)
            modulationIncoming[i]->yIn = slots[slot].modulationInputAnchor().y + spreadOffset(i, modulationIncoming.size(), inputBand);
    }

    // Corridor lanes: forward spanning cables above the field, backward cables in the return channel below it.
    const auto assignCorridor = [&](bool backward, double low, double high)
    {
        std::vector<Plan*> users;
        for (auto& plan : plans)
            if (plan.backward == backward && plan.usesChannelB) users.push_back(&plan);
        std::stable_sort(users.begin(), users.end(), [](const Plan* a, const Plan* b)
        {
            if (a->channelB != b->channelB) return a->channelB < b->channelB;
            if (a->channelA != b->channelA) return a->channelA < b->channelA;
            return a->edge < b->edge;
        });
        for (std::size_t i = 0; i < users.size(); ++i)
            users[i]->corridorY = lanePosition(static_cast<int>(i), static_cast<int>(users.size()), low, high, corridorInset);
    };
    assignCorridor(false, 0.0, fieldTop());
    assignCorridor(true, fieldBottom(), referenceHeight);

    // Channel lanes: every vertical run in a channel gets a distinct x; runs are ordered by exit then entry height.
    // Channel -1 is the left margin before column 0; every other channel sits between two columns.
    for (int channel = -1; channel + 1 < columnCount; ++channel)
    {
        std::vector<Run> runs;
        for (auto& plan : plans)
        {
            if (plan.channelA == channel)
                runs.push_back({&plan, true, plan.yOut, plan.usesChannelB ? plan.corridorY : plan.yIn});
            if (plan.usesChannelB && plan.channelB == channel)
                runs.push_back({&plan, false, plan.corridorY, plan.yIn});
        }
        std::stable_sort(runs.begin(), runs.end(), [](const Run& a, const Run& b)
        {
            if (a.yEnd != b.yEnd) return a.yEnd < b.yEnd;
            if (a.yStart != b.yStart) return a.yStart < b.yStart;
            return a.plan->edge < b.plan->edge;
        });
        const Channel extent = channelExtent(channel);
        for (std::size_t i = 0; i < runs.size(); ++i)
        {
            const double x = lanePosition(static_cast<int>(i), static_cast<int>(runs.size()), extent.left, extent.right, channelInset);
            if (runs[i].first) { runs[i].plan->xA = x; runs[i].plan->laneA = static_cast<int>(i); }
            else { runs[i].plan->xB = x; runs[i].plan->laneB = static_cast<int>(i); }
        }
    }

    std::vector<Cable> cables;
    cables.reserve(plans.size());
    for (const auto& plan : plans)
    {
        Cable cable;
        cable.edge = plan.edge;
        cable.gain = edges[plan.edge].gain;
        cable.backward = plan.backward;
        cable.colourIndex = cableColourIndex(edges[plan.edge].sourceId, edges[plan.edge].destinationId);
        cable.firstChannel = {plan.channelA, plan.laneA};
        cable.secondChannel = {plan.usesChannelB ? plan.channelB : -1, plan.laneB};
        // Both anchors sit on the same left border, so the destination column is entered at the
        // same x either way and the corridor/channel rules are literally unchanged.
        const double xOut = plan.from->outputAnchor().x, xIn = plan.to->inputAnchor().x;
        if (!plan.usesChannelB)
            cable.points = {{xOut, plan.yOut}, {plan.xA, plan.yOut}, {plan.xA, plan.yIn}, {xIn, plan.yIn}};
        else
            cable.points = {{xOut, plan.yOut}, {plan.xA, plan.yOut}, {plan.xA, plan.corridorY}, {plan.xB, plan.corridorY}, {plan.xB, plan.yIn}, {xIn, plan.yIn}};
        // Ports at equal height leave a zero-length vertical run: drop it and merge the collinear stubs.
        auto& points = cable.points;
        points.erase(std::unique(points.begin(), points.end(), [](Point a, Point b) { return a.x == b.x && a.y == b.y; }), points.end());
        for (std::size_t i = 1; i + 1 < points.size();)
        {
            const bool collinear = (points[i - 1].x == points[i].x && points[i].x == points[i + 1].x) || (points[i - 1].y == points[i].y && points[i].y == points[i + 1].y);
            if (collinear) points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
            else ++i;
        }

        double shortest = referenceWidth, longestHorizontal = -1.0;
        for (std::size_t i = 0; i + 1 < cable.points.size(); ++i)
        {
            const auto& a = cable.points[i];
            const auto& b = cable.points[i + 1];
            const double length = std::abs(b.x - a.x) + std::abs(b.y - a.y);
            shortest = std::min(shortest, length);
            if (a.y == b.y && length > longestHorizontal)
            {
                longestHorizontal = length;
                cable.knobSegment = i;
                cable.knobAnchor = {(a.x + b.x) / 2.0, a.y};
            }
        }
        cable.cornerRadius = std::max(0.0, std::min(maximumCornerRadius, shortest / 2.0));
        cables.push_back(std::move(cable));
    }
    return cables;
}

std::size_t countCrossings(std::span<const Cable> cables) noexcept
{
    std::size_t crossings = 0;
    for (std::size_t i = 0; i < cables.size(); ++i)
        for (std::size_t j = i + 1; j < cables.size(); ++j)
            for (std::size_t s = 0; s + 1 < cables[i].points.size(); ++s)
                for (std::size_t t = 0; t + 1 < cables[j].points.size(); ++t)
                {
                    const Point a0 = cables[i].points[s], a1 = cables[i].points[s + 1];
                    const Point b0 = cables[j].points[t], b1 = cables[j].points[t + 1];
                    const bool aHorizontal = a0.y == a1.y, bHorizontal = b0.y == b1.y;
                    if (aHorizontal == bHorizontal) continue;
                    const Point& h0 = aHorizontal ? a0 : b0;
                    const Point& h1 = aHorizontal ? a1 : b1;
                    const Point& v0 = aHorizontal ? b0 : a0;
                    const Point& v1 = aHorizontal ? b1 : a1;
                    const double hx0 = std::min(h0.x, h1.x), hx1 = std::max(h0.x, h1.x);
                    const double vy0 = std::min(v0.y, v1.y), vy1 = std::max(v0.y, v1.y);
                    if (v0.x > hx0 && v0.x < hx1 && h0.y > vy0 && h0.y < vy1) ++crossings;
                }
    return crossings;
}

double contrastRatio(Rgb a, Rgb b) noexcept
{
    const auto luminance = [](Rgb c)
    {
        const auto channel = [](std::uint8_t v)
        {
            const double s = v / 255.0;
            return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
    };
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
}
