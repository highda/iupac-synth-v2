#pragma once

// Authored maximal patch shared by the Patch, engine and realtime tests: every fixed editor slot
// active (3 general sources, 1 sub, 2 resonators, 2 filters, 2 shapers, 2 mixers and the four-slot
// effects tail), exactly maximumEdges audio edges — including one edge into each of the two declared
// audio-rate ports — and maximumMatrixRows enabled rows, all through the production catalog.
#include "iupac/domain/Patch.hpp"

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace iupac::testing
{
inline domain::Node defaultNode(std::string id, std::string_view type)
{
    const auto* descriptor = domain::findModule(type);
    domain::Node node {std::move(id), descriptor->type, {}};
    for (const auto& parameter : descriptor->parameters)
        node.parameters.push_back({std::string(parameter.id), std::vector<double>(parameter.arraySize == 0 ? 1 : parameter.arraySize, parameter.defaultValue)});
    if (type == "harmonic")
    {
        node.parameters[0].values[0] = 1.0;
        for (std::size_t i = 0; i < node.parameters[1].values.size(); ++i) node.parameters[1].values[i] = static_cast<double>(i + 1);
    }
    return node;
}

// The catalog is the source of the worst case's numbers: the fixture re-derives from it, and
// `PatchTests` asserts the derived source-copy budget still equals the 9 VERIFICATION V8 states,
// so widening `unisonVoices` fails loudly instead of silently moving the benchmark.
inline const domain::ParameterDescriptor& catalogParameter(std::string_view type, std::string_view parameter)
{
    const auto* descriptor = domain::findModule(type);
    for (const auto& candidate : descriptor->parameters)
        if (candidate.id == parameter) return candidate;
    std::abort();
}

// The per-voice source-copy budget VERIFICATION V8 fixes for the phase-4 worst case (D10):
// `unisonVoices` at its catalog maximum on exactly one general source, the default on the other two.
inline std::size_t worstCaseSourceCopyBudget()
{
    return static_cast<std::size_t>(catalogParameter("harmonic", "unisonVoices").maximum)
         + (domain::generalSourceSlots - 1) * static_cast<std::size_t>(catalogParameter("harmonic", "unisonVoices").defaultValue);
}

inline domain::Patch maximalPatch()
{
    domain::Patch patch;
    patch.noiseSeed = 0x2468aceu;
    patch.nodes = {defaultNode("h1", "harmonic"), defaultNode("f1", "fm"), defaultNode("n1", "noise"), defaultNode("b1", "sub"),
                   defaultNode("r1", "resonator"), defaultNode("r2", "resonator"), defaultNode("q1", "filter"), defaultNode("q2", "filter"),
                   defaultNode("s1", "shaper"), defaultNode("s2", "shaper"), defaultNode("m1", "mixer"), defaultNode("m2", "mixer"),
                   defaultNode("x1", "chorus"), defaultNode("x2", "delay"), defaultNode("x3", "reverb"), defaultNode("x4", "width")};
    patch.nodes[5].parameters[0].values[0] = 1.0; // second resonator in modal mode
    const std::vector<std::string> sources {"h1", "f1", "n1", "b1"}, processors {"r1", "r2", "q1", "q2", "s1", "s2", "m1", "m2"};
    for (const auto& source : sources)
        for (const auto& processor : processors) patch.edges.push_back({source, processor, 0.1}); // 32
    for (auto [from, to] : std::vector<std::pair<std::string, std::string>> {{"r1", "q1"}, {"r2", "q2"}, {"q1", "s1"}, {"q2", "s2"}, {"s1", "m1"}, {"s2", "m2"},
                                                                            {"m1", "x1"}, {"m2", "x1"}, {"x1", "x2"}, {"x2", "x3"}, {"x3", "x4"},
                                                                            {"x4", "output"}, {"m1", "output"}, {"m2", "output"}})
        patch.edges.push_back({from, to, from[0] == 'm' || from[0] == 'x' ? 1.0 : 0.5}); // 46
    // The two typed audio-rate inputs: ordinary edges in the DAG, distinct from the `in` edges above.
    patch.edges.push_back({"h1", "f1", 0.5, domain::AudioPort::modIn});
    patch.edges.push_back({"n1", "r1", 0.5, domain::AudioPort::exciteIn}); // 48
    const std::vector<std::pair<std::string, std::string>> destinations {{"r1", "tuneRatio"}, {"r2", "modalQ"}, {"q1", "cutoff"}, {"q2", "q"}, {"s1", "drive"}, {"s2", "wet"},
                                                                          {"m1", "level"}, {"m2", "pan"}, {"h1", "outputLevel"}, {"f1", "index"}, {"n1", "burstMs"}, {"r1", "combFeedback"},
                                                                          {"b1", "outputLevel"}, {"h1", "harmonicityMorph"}, {"f1", "modInDepth"}, {"r2", "exciteDepth"}, {"q1", "drive"}, {"q2", "envAmount"},
                                                                          {"x1", "mix"}, {"x2", "feedback"}, {"x3", "size"}, {"x4", "width"}};
    for (std::size_t i = 0; i < domain::maximumMatrixRows; ++i)
    {
        const auto& [node, parameter] = destinations[i % destinations.size()];
        patch.matrix.push_back({"row-" + std::to_string(i), true, static_cast<domain::ModulationSource>(i % (static_cast<std::size_t>(domain::ModulationSource::macro4) + 1)), node, parameter, i % 2 ? -0.3 : 0.3});
    }
    patch.macros = {{{"Macro 1", 0.5}, {"Macro 2", 0.25}, {"Macro 3", 0.0}, {"Macro 4", 1.0}}};
    return patch;
}

// The fixed phase-4 worst-case benchmark patch (VERIFICATION V8, D8, restated by D10). It is
// `maximalPatch()` with all three general source slots unison-capable and spending the per-voice
// source-copy budget of 9 — `unisonVoices` at its catalog maximum on one of them and the catalog
// default of 1 on the other two — both typed audio-rate depths open so the `modIn`/`exciteIn`
// branches actually run, and the whole chorus -> delay -> reverb -> width tail driven away from its
// identity settings. Voice count is still 16 everywhere: unison multiplies oscillator work, never
// polyphony (D3).
inline domain::Patch worstCaseBenchmarkPatch()
{
    auto patch = maximalPatch();
    // `noise` is the one general source type without a unison bank, so the worst case spends that
    // slot on a second harmonic instead. Keeping the id keeps every edge and row pointing at it.
    patch.nodes[2] = defaultNode("n1", "harmonic");
    for (auto& row : patch.matrix)
        if (row.destinationNode == "n1" && row.destinationParameter == "burstMs") row.destinationParameter = "detuneCents";
    const auto set = [&patch](std::string_view id, std::string_view parameter, double value) {
        for (auto& node : patch.nodes)
            if (node.id == id)
                for (auto& value_ : node.parameters)
                    if (value_.id == parameter) value_.values[0] = value;
    };
    // Unison is the per-voice source-copy budget of 9 (VERIFICATION V8, D10): `maximumUnisonVoices`
    // on exactly one general source and the catalog default of 1 on the other two. Unison 7 on all
    // three measured renderRatio 1.08 on the canonical container (#144/#146) — the worst
    // constructible case, which no current hardware can play, rather than the worst plausible one.
    // The detune, spread and drift settings stay on all three slots: they are free at one copy and
    // keep the two single-copy sources away from their identity settings.
    for (const auto* id : {"h1", "f1", "n1"}) { set(id, "detuneCents", 24); set(id, "unisonSpread", 1.0); set(id, "drift", 0.5); }
    set("h1", "unisonVoices", catalogParameter("harmonic", "unisonVoices").maximum);
    // Both additive sources render a full 16-partial spectrum (#144). `defaultNode` leaves a
    // harmonic slot at the catalog default of one partial, which is not a worst case for an
    // oscillator whose cost is one recursion per partial per unison copy: the authored `lead`
    // panels and every chemistry-derived spectrum fill all sixteen. The amplitudes fall as 1/n so
    // the patch is a plausible bright tone rather than sixteen unit partials into the output
    // guard; the render cost is the same either way, because the cost is per partial, not per
    // level. Leaving the sparse default here would have let a later zero-amplitude skip pass V8
    // on a patch it had itself made cheap.
    for (const auto* id : {"h1", "n1"})
        for (auto& node : patch.nodes)
            if (node.id == id)
                for (auto& value : node.parameters)
                    if (value.id == "partialAmplitudes")
                        for (std::size_t n = 0; n < value.values.size(); ++n) value.values[n] = 1.0 / static_cast<double>(n + 1);
    set("f1", "index", 4.0); set("f1", "modInDepth", 1.0); set("r1", "exciteDepth", 1.0);
    set("q1", "mode", 3); set("q1", "drive", 8.0); set("q2", "drive", 4.0); // ladder24 is the most expensive filter mode
    set("x1", "mix", 0.5); set("x2", "mix", 0.5); set("x3", "mix", 0.5); set("x4", "width", 1.4);
    return patch;
}
}
