// #69: fixed slot geometry, port anchors and the orthogonal cable router as pure data/functions.
#include "iupac/ui/CableRouter.hpp"
#include "iupac/ui/Layout.hpp"

#include "MaximalPatch.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
using namespace iupac::ui;

bool ok = true;

bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "layout test failed: " << message << '\n';
        ok = false;
    }
    return condition;
}

bool near(double a, double b, double tolerance = 1.0e-9) { return std::abs(a - b) <= tolerance; }

bool onBorder(const Slot& slot, Point p)
{
    const auto& f = slot.frame;
    const bool onVertical = (near(p.x, f.x) || near(p.x, f.right())) && p.y >= f.y && p.y <= f.bottom();
    const bool onHorizontal = (near(p.y, f.y) || near(p.y, f.bottom())) && p.x >= f.x && p.x <= f.right();
    return onVertical || onHorizontal;
}

std::vector<CableEdge> eligibleEdges()
{
    std::vector<CableEdge> edges;
    for (std::size_t s = 0; s < slotCount; ++s)
        for (std::size_t d = 0; d < slotCount; ++d)
            if (s != d && slotTable[s].hasOutput() && slotTable[d].hasInput()) edges.push_back({s, d, 0.5, "src", "dst"});
    return edges;
}

// Slot assignment for a Patch: kind by module type, instance in patch order (the editor's #70 mapping in miniature).
SlotKind kindOf(iupac::domain::ModuleType type)
{
    using T = iupac::domain::ModuleType;
    switch (type)
    {
        case T::harmonic: case T::fm: case T::noise: return SlotKind::source;
        case T::resonator: return SlotKind::resonator;
        case T::filter: return SlotKind::filter;
        case T::shaper: return SlotKind::shaper;
        case T::mixer: return SlotKind::mixer;
    }
    return SlotKind::source;
}

std::vector<CableEdge> edgesOf(const iupac::domain::Patch& patch)
{
    std::map<std::string, std::size_t> slotOf {{"output", outputSlot}};
    std::map<SlotKind, int> seen;
    for (const auto& node : patch.nodes)
    {
        const auto kind = kindOf(node.type);
        const auto slot = findSlot(kind, seen[kind]++);
        expect(slot.has_value(), "every maximal-patch node has a slot");
        slotOf[node.id] = slot.value_or(0);
    }
    std::vector<CableEdge> edges;
    for (const auto& edge : patch.edges) edges.push_back({slotOf.at(edge.source), slotOf.at(edge.destination), edge.gain, edge.source, edge.destination});
    return edges;
}

void checkCableGeometry(const Cable& cable, const CableEdge& edge)
{
    expect(cable.points.size() >= 2 && (cable.points.size() == 2 || cable.points.size() >= 4), "cable is a straight stub pair or has at least four points");
    for (std::size_t i = 0; i + 1 < cable.points.size(); ++i)
    {
        const auto& a = cable.points[i];
        const auto& b = cable.points[i + 1];
        expect(a.x == b.x || a.y == b.y, "polyline is axis-aligned");
        expect(!(a.x == b.x && a.y == b.y), "no zero-length segment");
        if (i > 0) expect((cable.points[i - 1].x == a.x) != (a.x == b.x), "consecutive segments alternate direction");
    }
    expect(onBorder(slotTable[edge.source], cable.points.front()), "cable starts on the source slot border");
    expect(near(cable.points.front().x, slotTable[edge.source].outputAnchor().x), "cable starts at the OUT port side");
    expect(onBorder(slotTable[edge.destination], cable.points.back()), "cable ends on the destination slot border");
    expect(near(cable.points.back().x, slotTable[edge.destination].inputAnchor().x), "cable ends at the IN port side");
    expect(cable.cornerRadius >= 0.0 && cable.cornerRadius <= maximumCornerRadius && (cable.points.size() == 2 || cable.cornerRadius > 0.0), "corner radius bounded");
    const auto& k0 = cable.points[cable.knobSegment];
    const auto& k1 = cable.points[cable.knobSegment + 1];
    expect(k0.y == k1.y && near(cable.knobAnchor.y, k0.y) && near(cable.knobAnchor.x, (k0.x + k1.x) / 2.0), "knob anchor is the midpoint of a horizontal segment");
    for (std::size_t i = 0; i + 1 < cable.points.size(); ++i)
        if (cable.points[i].y == cable.points[i + 1].y)
            expect(std::abs(cable.points[i + 1].x - cable.points[i].x) <= std::abs(k1.x - k0.x), "knob sits on the longest horizontal segment");
    // #88: the gain knob is an overlay of fixed pixel size centred on the anchor. At the smallest scale
    // the live field is ever drawn at its rectangle must still lie inside the cable layer, which takes the
    // field's local bounds; the field sits entirely below the macro strip, so "inside the layer" is also
    // "clear of the strip and the status line".
    const double halfY = knobHalfExtentPixels / minimumFieldVerticalScale;
    const double halfX = knobHalfExtentPixels; // the field is never narrower than the reference width
    expect(cable.knobAnchor.y - halfY >= 0.0 && cable.knobAnchor.y + halfY <= referenceHeight, "knob rectangle stays inside the field's top and bottom edges at the minimum window size");
    expect(cable.knobAnchor.x - halfX >= 0.0 && cable.knobAnchor.x + halfX <= referenceWidth, "knob rectangle stays inside the field's left and right edges at the minimum window size");
    expect(cable.colourIndex >= 0 && cable.colourIndex < static_cast<int>(cablePalette.size()), "colour index in palette");
    expect(cable.colourIndex == cableColourIndex(edge.sourceId, edge.destinationId), "colour is the stable edge identity colour");
    const int columnGap = slotTable[edge.destination].column - slotTable[edge.source].column;
    expect(cable.backward == (columnGap <= 0), "backward flag follows column order");
    if (cable.backward) expect(std::any_of(cable.points.begin(), cable.points.end(), [](Point p) { return p.y > fieldBottom(); }), "backward cable uses the return channel below the field");
    if (columnGap > 1) expect(std::any_of(cable.points.begin(), cable.points.end(), [](Point p) { return p.y < fieldTop(); }), "spanning cable uses the corridor above the field");
    if (columnGap == 1) expect(cable.points.size() <= 4, "adjacent-column cable is stub, channel run, stub");
    // every vertical run lies inside an inter-column channel, never over a slot
    for (std::size_t i = 0; i + 1 < cable.points.size(); ++i)
        if (cable.points[i].x == cable.points[i + 1].x)
        {
            bool inChannel = false;
            for (int c = 0; c + 1 < columnCount; ++c)
            {
                const auto extent = channelExtent(c);
                inChannel |= cable.points[i].x > extent.left && cable.points[i].x < extent.right;
            }
            expect(inChannel, "vertical run lies in an inter-column channel");
        }
}

void checkNoSharedLanes(const std::vector<Cable>& cables)
{
    // no two cables share a vertical x (channel offset) and no two stubs coincide at one port
    std::set<double> verticalX;
    std::set<std::pair<double, double>> stubs;
    for (const auto& cable : cables)
    {
        for (std::size_t i = 0; i + 1 < cable.points.size(); ++i)
            if (cable.points[i].x == cable.points[i + 1].x) expect(verticalX.insert(cable.points[i].x).second, "no two cables share a channel offset");
        expect(stubs.insert({cable.points.front().x, cable.points.front().y}).second, "fan-out stubs never coincide");
        expect(stubs.insert({cable.points.back().x, cable.points.back().y}).second, "fan-in stubs never coincide");
    }
    std::set<double> corridorY;
    for (const auto& cable : cables)
        if (cable.points.size() == 6) expect(corridorY.insert(cable.points[2].y).second, "no two corridor runs share a height");
}
}

int main()
{
    // slot table
    std::map<SlotKind, int> counts;
    for (std::size_t i = 0; i < slotCount; ++i)
    {
        const auto& slot = slotTable[i];
        ++counts[slot.kind];
        expect(slot.frame.x >= 0.0 && slot.frame.right() <= referenceWidth && slot.frame.y >= 0.0 && slot.frame.bottom() <= referenceHeight, "slot inside the reference frame");
        expect(findSlot(slot.kind, slot.instance) == i, "slot lookup by kind/instance");
        if (slot.hasInput()) expect(onBorder(slot, slot.inputAnchor()) && near(slot.inputAnchor().x, slot.frame.x), "IN anchor on the left border");
        if (slot.hasOutput()) expect(onBorder(slot, slot.outputAnchor()) && near(slot.outputAnchor().x, slot.frame.right()), "OUT anchor on the right border");
        for (std::size_t j = i + 1; j < slotCount; ++j)
        {
            const auto& other = slotTable[j];
            expect(slot.column <= other.column, "table is column-major left to right");
            const bool overlap = slot.frame.x < other.frame.right() && other.frame.x < slot.frame.right() && slot.frame.y < other.frame.bottom() && other.frame.y < slot.frame.bottom();
            expect(!overlap, "slots never overlap");
            if (slot.column < other.column) expect(slot.frame.right() < other.frame.x, "columns are separated by a channel");
        }
    }
    expect(counts[SlotKind::source] == 3 && counts[SlotKind::resonator] == 2 && counts[SlotKind::filter] == 2 && counts[SlotKind::shaper] == 2 && counts[SlotKind::mixer] == 2 && counts[SlotKind::output] == 1, "SRC(3) | RES(2) | FILT(2) | SHAPE(2) | MIX(2) | OUT");
    expect(slotTable[outputSlot].kind == SlotKind::output && slotTable[outputSlot].column == columnCount - 1, "OUT bus is the last column");
    expect(fieldTop() > 0.0 && fieldBottom() < referenceHeight, "corridors exist above and below the field");
    for (int c = 0; c + 1 < columnCount; ++c) expect(channelExtent(c).right - channelExtent(c).left > 2.0 * channelInset, "channel wide enough for lanes");
    expect(fieldTop() > 2.0 * corridorInset && referenceHeight - fieldBottom() > 2.0 * corridorInset, "both corridors leave a lane band after reserving the knob's half extent");

    // scaling is a pure function of window size
    const auto same = scaleToWindow(slotTable[0].frame, referenceWidth, referenceHeight);
    expect(near(same.x, slotTable[0].frame.x) && near(same.width, slotTable[0].frame.width), "reference-size scaling is identity");
    const auto scaled = scaleToWindow(slotTable[3].frame, 1800.0, 1200.0);
    expect(near(scaled.x, slotTable[3].frame.x * 1.8) && near(scaled.y, slotTable[3].frame.y * 1200.0 / 700.0) && near(scaled.height, slotTable[3].frame.height * 1200.0 / 700.0), "scaling is proportional");
    const auto point = scaleToWindow(Point {500.0, 350.0}, 1200.0, 800.0);
    expect(near(point.x, 600.0) && near(point.y, 400.0), "point scaling");

    // palette
    for (const auto& colour : cablePalette) expect(contrastRatio(colour, offWhiteGround) >= 3.0, "cable colour contrast >= 3:1 against off-white");
    expect(cablePalette.size() >= 8, "palette has at least eight hues");
    expect(cableColourIndex("h1", "r1") == cableColourIndex("h1", "r1"), "colour index deterministic");
    {
        std::set<int> used;
        for (const auto& edge : iupac::testing::maximalPatch().edges) used.insert(cableColourIndex(edge.source, edge.destination));
        expect(used.size() >= 4, "maximal patch uses several colours");
    }

    // every eligible edge routes, alone and all together
    const auto eligible = eligibleEdges();
    expect(eligible.size() == 3 * 9 + 8 * 8, "eligible edge count");
    for (const auto& edge : eligible)
    {
        const auto cables = routeCables(std::span<const CableEdge>(&edge, 1));
        if (expect(cables.size() == 1, "single edge routes")) checkCableGeometry(cables[0], edge);
    }
    {
        const auto cables = routeCables(eligible);
        expect(cables.size() == eligible.size(), "all eligible edges route together");
        for (const auto& cable : cables) checkCableGeometry(cable, eligible[cable.edge]);
        checkNoSharedLanes(cables);
        std::cout << "layout: " << eligible.size() << " eligible edges route with " << countCrossings(cables) << " crossings\n";
    }

    // illegal endpoints produce no cable and never crash
    {
        const CableEdge illegal[] {{outputSlot, 3, 1.0, "output", "r1"}, {0, 1, 1.0, "h1", "h2"}, {4, 4, 1.0, "r2", "r2"}, {99, 3, 1.0, "x", "r1"}};
        expect(routeCables(illegal).empty(), "edges without the required ports are skipped");
    }

    // maximal authored patch (#67): routes, unique lanes, recorded crossing baseline and timing
    const auto patch = iupac::testing::maximalPatch();
    const auto edges = edgesOf(patch);
    expect(edges.size() == iupac::domain::maximumEdges, "maximal patch has the edge cap");
    auto cables = routeCables(edges);
    expect(cables.size() == edges.size(), "maximal patch routes every edge");
    for (const auto& cable : cables) checkCableGeometry(cable, edges[cable.edge]);
    checkNoSharedLanes(cables);
    for (const auto& cable : cables) expect(near(cable.gain, edges[cable.edge].gain), "gain carried through");
    const auto crossings = countCrossings(cables);
    std::cout << "layout: maximal patch routes " << cables.size() << " cables with " << crossings << " crossings (baseline)\n";

    // #89: text is authored in reference-frame heights and scaled with the window, with an absolute floor.
    expect(near(textScale(referenceWidth, referenceHeight), 1.0), "the reference frame is scale 1");
    expect(near(textScale(2.0 * referenceWidth, 2.0 * referenceHeight), 2.0), "twice the reference frame is scale 2");
    expect(near(textScale(2.0 * referenceWidth, referenceHeight), 1.0), "a window stretched on one axis takes the smaller scale");
    expect(near(fontHeight(9.0, 1.0), 9.0), "a 9 px caption is 9 px at scale 1");
    expect(near(fontHeight(9.0, 2.0), 18.0), "a 9 px caption is 18 px at scale 2");
    expect(near(fontHeight(9.0, 1.5), 13.5), "caption height is linear in the scale");
    expect(near(fontHeight(7.0, 1.0), minimumFontHeight), "the floor lifts sub-8 px reference heights at scale 1");
    expect(near(fontHeight(9.0, 0.5), minimumFontHeight), "the floor holds below the reference frame");
    expect(fontHeight(7.0, 2.0) > minimumFontHeight, "the floor does not flatten scaled text");

    constexpr int iterations = 500;
    std::vector<double> samples;
    for (int i = 0; i < iterations; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        cables = routeCables(edges);
        samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2], worst = samples.back();
    double budget = 1.0;
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    budget = 20.0;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(undefined_behavior_sanitizer)
    budget = 20.0;
#endif
#endif
    std::cout << "layout: 32-edge route median " << median << " ms, worst " << worst << " ms over " << iterations << " runs (budget " << budget << " ms)\n";
    expect(median < budget, "fully populated 32-edge patch routes within budget");

    std::cout << (ok ? "layout tests passed\n" : "layout tests FAILED\n");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
