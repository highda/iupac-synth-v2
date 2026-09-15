#include "iupac/domain/Patch.hpp"

#include <cmath>
#include <iostream>

namespace
{
using namespace iupac::domain;

Node defaults(std::string id, std::string_view type)
{
    const auto* d = findModule(type);
    Node n {std::move(id), d->type, {}};
    for (const auto& p : d->parameters)
        n.parameters.push_back({std::string(p.id), std::vector<double>(p.arraySize == 0 ? 1 : p.arraySize, p.defaultValue)});
    if (type == "harmonic")
    {
        auto& amplitudes = n.parameters[0].values;
        amplitudes[0] = 1.0;
        for (std::size_t i = 0; i < n.parameters[1].values.size(); ++i) n.parameters[1].values[i] = static_cast<double>(i + 1);
    }
    return n;
}

Patch authoredPatch()
{
    Patch p;
    p.noiseSeed = 0x13579bdu;
    p.nodes = {defaults("osc", "harmonic"), defaults("filter", "filter"), defaults("mix", "mixer")};
    p.edges = {{"osc", "filter", 1.0}, {"filter", "mix", 0.8}, {"osc", "mix", 0.2}, {"mix", "output", 1.0}};
    p.matrix = {{"row-cutoff", true, ModulationSource::l1, "filter", "cutoff", 0.5}, {"row-level", false, ModulationSource::macro1, "mix", "level", -0.25}};
    p.macros = {{{"Brightness", 0.4}, {"Macro 2", 0.0}, {"Macro 3", 0.0}, {"Macro 4", 0.0}}};
    return p;
}

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "patch contract test failed: " << message << '\n';
    return condition;
}
}

bool runPatchTests()
{
    bool ok = true;
    const auto& catalog = moduleCatalog();
    ok &= expect(catalog.size() == 7, "seven stable module types");
    for (const auto& module : catalog)
        for (const auto& parameter : module.parameters)
        {
            ok &= expect(parameter.minimum <= parameter.defaultValue && parameter.defaultValue <= parameter.maximum, "catalog default in range");
            ok &= expect(std::abs(parameter.denormalize(parameter.normalize(parameter.defaultValue)) - parameter.defaultValue) < 1.0e-9, "normalization round trip");
            if (parameter.kind != ParameterKind::continuous) ok &= expect(!parameter.modulatable, "only scalar controls modulatable");
        }

    Patch silent;
    ok &= expect(validate(silent).empty(), "empty patch is valid silence");
    auto silentDecoded = decodePatchJson(encodePatchJson(silent));
    ok &= expect(static_cast<bool>(silentDecoded), "silent patch codec round trip");

    auto patch = authoredPatch();
    ok &= expect(applyHarmonicSpectrum(patch.nodes[0], -1.0, 0.02), "harmonic convenience edit succeeds");
    ok &= expect(patch.nodes[0].parameters[0].values[1] < patch.nodes[0].parameters[0].values[0]
        && patch.nodes[0].parameters[1].values[1] > 2.0, "convenience edit stores explicit arrays");
    ok &= expect(validate(patch).empty(), "authored serial/parallel graph valid");
    const auto encoded = encodePatchJson(patch, true);
    auto decoded = decodePatchJson(encoded);
    ok &= expect(static_cast<bool>(decoded) && encodePatchJson(*decoded.value) == encodePatchJson(patch), "canonical patch round trip");
    State state {patch, patch, {{0.1, 0.2, 0.3, 0.4}, -6.0, 0.5, 0.0, false}};
    auto stateDecoded = decodeStateJson(encodeStateJson(state));
    ok &= expect(static_cast<bool>(stateDecoded) && encodeStateJson(*stateDecoded.value) == encodeStateJson(state), "state round trip");

    auto invalid = patch; invalid.edges.push_back({"filter", "osc", 1.0});
    ok &= expect(!validate(invalid).empty(), "edge into source rejected");
    invalid = patch; invalid.edges.push_back({"mix", "filter", 1.0});
    ok &= expect(!validate(invalid).empty(), "processor cycle rejected");
    invalid = patch; invalid.edges.push_back(patch.edges.front());
    ok &= expect(!validate(invalid).empty(), "duplicate edge rejected");
    invalid = patch; invalid.edges[0].destination = "missing";
    ok &= expect(!validate(invalid).empty(), "dangling edge rejected");
    invalid = patch; invalid.nodes.push_back(defaults("osc", "fm"));
    ok &= expect(!validate(invalid).empty(), "duplicate node rejected");
    invalid = patch; invalid.matrix[0].destinationParameter = "mode";
    ok &= expect(!validate(invalid).empty(), "discrete matrix destination rejected");
    invalid = patch; invalid.matrix.push_back(invalid.matrix.front());
    ok &= expect(!validate(invalid).empty(), "duplicate row rejected");
    ok &= expect(!decodePatchJson("{}").value, "missing required fields rejected");
    ok &= expect(!decodePatchJson(std::string(maximumDocumentBytes + 1, ' ')).value, "oversized input rejected before parsing");
    std::string nested(33, '['); nested.append(33, ']');
    ok &= expect(!decodePatchJson(nested).value, "deeply nested input rejected before parsing");
    auto provenanceState = state; juce::var provenance(new juce::DynamicObject()); provenance.getDynamicObject()->setProperty("nested", juce::var(new juce::DynamicObject())); provenanceState.provenance = provenance;
    ok &= expect(decodeStateJson(encodeStateJson(provenanceState)).value.has_value(), "bounded optional provenance accepted");
    auto extra = juce::JSON::parse(encoded); extra.getDynamicObject()->setProperty("chemicalHash", "forbidden");
    ok &= expect(!decodePatchValue(extra).value, "unknown chemistry metadata rejected");
    auto nonfinite = patch; nonfinite.nodes[0].parameters[0].values[0] = INFINITY;
    ok &= expect(!validate(nonfinite).empty(), "nonfinite coefficient rejected");
    invalid = patch; while (invalid.nodes.size() <= maximumNodes) invalid.nodes.push_back(defaults("n" + std::to_string(invalid.nodes.size()), "mixer"));
    ok &= expect(!validate(invalid).empty(), "node cap rejected");
    return ok;
}
