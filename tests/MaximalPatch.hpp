#pragma once

// Authored maximal patch shared by the Patch, engine and realtime tests: every fixed editor
// slot active (3 sources, 2 resonators, 2 filters, 2 shapers, 2 mixers), exactly maximumEdges
// audio edges and maximumMatrixRows enabled rows, all through the production catalog.
#include "iupac/domain/Patch.hpp"

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

inline domain::Patch maximalPatch()
{
    domain::Patch patch;
    patch.noiseSeed = 0x2468aceu;
    patch.nodes = {defaultNode("h1", "harmonic"), defaultNode("f1", "fm"), defaultNode("n1", "noise"),
                   defaultNode("r1", "resonator"), defaultNode("r2", "resonator"), defaultNode("q1", "filter"), defaultNode("q2", "filter"),
                   defaultNode("s1", "shaper"), defaultNode("s2", "shaper"), defaultNode("m1", "mixer"), defaultNode("m2", "mixer")};
    patch.nodes[4].parameters[0].values[0] = 1.0; // second resonator in modal mode
    const std::vector<std::string> sources {"h1", "f1", "n1"}, processors {"r1", "r2", "q1", "q2", "s1", "s2", "m1", "m2"};
    for (const auto& source : sources)
        for (const auto& processor : processors) patch.edges.push_back({source, processor, 0.1});
    for (auto [from, to] : std::vector<std::pair<std::string, std::string>> {{"r1", "q1"}, {"r2", "q2"}, {"q1", "s1"}, {"q2", "s2"}, {"s1", "m1"}, {"s2", "m2"}, {"m1", "output"}, {"m2", "output"}})
        patch.edges.push_back({from, to, from[0] == 'm' ? 1.0 : 0.5});
    const std::vector<std::pair<std::string, std::string>> destinations {{"r1", "tuneRatio"}, {"r2", "modalQ"}, {"q1", "cutoff"}, {"q2", "q"}, {"s1", "drive"}, {"s2", "wet"},
                                                                          {"m1", "level"}, {"m2", "pan"}, {"h1", "outputLevel"}, {"f1", "index"}, {"n1", "burstMs"}, {"r1", "combFeedback"}};
    for (std::size_t i = 0; i < domain::maximumMatrixRows; ++i)
    {
        const auto& [node, parameter] = destinations[i % destinations.size()];
        patch.matrix.push_back({"row-" + std::to_string(i), true, static_cast<domain::ModulationSource>(i % (static_cast<std::size_t>(domain::ModulationSource::macro4) + 1)), node, parameter, i % 2 ? -0.3 : 0.3});
    }
    patch.macros = {{{"Macro 1", 0.5}, {"Macro 2", 0.25}, {"Macro 3", 0.0}, {"Macro 4", 1.0}}};
    return patch;
}
}
