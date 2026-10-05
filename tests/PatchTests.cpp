#include "iupac/domain/Patch.hpp"
#include "MaximalPatch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    ok &= expect(catalog.size() == moduleTypeCount && moduleTypeCount == 12, "twelve stable module types");
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
    ok &= expect(validate(invalid) == "patch exceeds structural cap", "node cap rejected");
    invalid = patch; invalid.nodes.push_back(defaults("mix2", "mixer")); invalid.nodes.push_back(defaults("mix3", "mixer"));
    ok &= expect(validate(invalid) == "module type cap exceeded", "per-type cap still enforced below the node cap");

    const auto maximal = iupac::testing::maximalPatch();
    ok &= expect(maximal.nodes.size() == maximumNodes && maximal.edges.size() == maximumEdges && maximal.matrix.size() == maximumMatrixRows, "authored maximal patch fills every bound");
    ok &= expect(validate(maximal).empty(), "maximal patch with every slot active and routed is valid");
    auto maximalDecoded = decodePatchJson(encodePatchJson(maximal, true));
    ok &= expect(static_cast<bool>(maximalDecoded) && encodePatchJson(*maximalDecoded.value) == encodePatchJson(maximal), "maximal patch round-trips through the shared codec");
    State maximalState {maximal, maximal, {}};
    auto maximalStateDecoded = decodeStateJson(encodeStateJson(maximalState));
    ok &= expect(static_cast<bool>(maximalStateDecoded) && encodeStateJson(*maximalStateDecoded.value) == encodeStateJson(maximalState), "maximal state round-trips through the shared codec");
    invalid = maximal; invalid.nodes.push_back(defaults("n17", "mixer"));
    ok &= expect(validate(invalid) == "patch exceeds structural cap", "seventeenth node rejected");
    invalid = maximal; invalid.edges.push_back({"r1", "q2", 0.5});
    ok &= expect(validate(invalid) == "patch exceeds structural cap", "forty-ninth edge rejected");
    invalid = maximal; invalid.matrix.push_back({"row-40", true, ModulationSource::e1, "q1", "q", 0.1});
    ok &= expect(validate(invalid) == "patch exceeds structural cap", "forty-first row rejected");
    ok &= expect(!decodePatchJson(encodePatchJson(invalid)).value, "decoder rejects rows beyond the cap");

    // --- D8 per-type slot caps (3 general source, 1 sub, 2 each processor, 1 of each effect) ---
    for (auto [type, cap] : std::vector<std::pair<std::string_view, std::size_t>> {{"sub", 1}, {"chorus", 1}, {"delay", 1}, {"reverb", 1}, {"width", 1}})
    {
        Patch over; for (std::size_t i = 0; i <= cap; ++i) over.nodes.push_back(defaults(std::string(type) + std::to_string(i), type));
        ok &= expect(validate(over) == "module type cap exceeded", "per-type cap enforced beyond its slot count");
    }
    {
        Patch pool; pool.nodes = {defaults("g1", "harmonic"), defaults("g2", "fm"), defaults("g3", "noise")};
        ok &= expect(validate(pool).empty(), "three general sources fill the shared source pool");
        auto withSub = pool; withSub.nodes.push_back(defaults("b1", "sub"));
        ok &= expect(validate(withSub).empty(), "the sub slot is outside the general source pool");
        auto fourth = pool; fourth.nodes.push_back(defaults("g4", "harmonic"));
        ok &= expect(validate(fourth) == "module type cap exceeded", "a fourth general source is rejected");
    }

    // --- D8 effects-region router constraints ---
    {
        Patch tail; tail.nodes = {defaults("g1", "harmonic"), defaults("q1", "filter"), defaults("x1", "chorus"), defaults("x2", "delay")};
        tail.edges = {{"g1", "q1", 0.5}, {"q1", "x1", 0.5}, {"x1", "x2", 0.5}, {"x2", "output", 1.0}};
        ok &= expect(validate(tail).empty(), "a per-voice branch into an ordered effects tail is valid");
        auto backIntoVoice = tail; backIntoVoice.edges.push_back({"x2", "q1", 0.5});
        ok &= expect(validate(backIntoVoice) == "effects tail edge enters a per-voice node", "an effects edge back into a per-voice node is rejected");
        auto toOutput = tail; toOutput.edges.push_back({"x1", "output", 0.5});
        ok &= expect(validate(toOutput).empty(), "an effects node may also reach the OUT bus directly");
    }

    // --- D8 audio-rate ports ---
    {
        Patch ports; ports.nodes = {defaults("g1", "harmonic"), defaults("f1", "fm"), defaults("r1", "resonator")};
        ports.edges = {{"r1", "output", 1.0}, {"g1", "r1", 0.5}, {"g1", "f1", 0.5, AudioPort::modIn}, {"f1", "r1", 0.5, AudioPort::exciteIn}};
        ok &= expect(validate(ports).empty(), "modIn and exciteIn are declared ports and validate");
        auto duplicate = ports; duplicate.edges.push_back({"g1", "f1", 0.5, AudioPort::modIn});
        ok &= expect(validate(duplicate) == "self or duplicate audio edge", "a duplicate edge on the same port is rejected");
        auto distinct = ports; distinct.edges.push_back({"f1", "r1", 0.5});
        ok &= expect(validate(distinct).empty(), "the same pair on two different ports is not a duplicate");
        auto undeclared = ports; undeclared.edges.push_back({"g1", "r1", 0.5, AudioPort::modIn});
        ok &= expect(validate(undeclared) == "audio edge targets an undeclared port", "modIn on a resonator is rejected");
        auto intoSource = ports; intoSource.edges.push_back({"r1", "g1", 0.5});
        ok &= expect(validate(intoSource) == "audio edge enters source", "the ordinary IN port of a source stays closed");
        auto cycle = ports; cycle.edges.push_back({"r1", "f1", 0.5, AudioPort::modIn});
        ok &= expect(validate(cycle) == "audio graph contains cycle", "a cycle through modIn is rejected");
        auto inOnly = ports; inOnly.edges.resize(2);
        ok &= expect(encodePatchJson(ports).find("modIn") != std::string::npos && encodePatchJson(ports).find("exciteIn") != std::string::npos
                     && encodePatchJson(inOnly).find("port") == std::string::npos, "a non-default port is encoded and the default one is left out");
        auto portsDecoded = decodePatchJson(encodePatchJson(ports));
        ok &= expect(static_cast<bool>(portsDecoded) && portsDecoded.value->edges[2].port == AudioPort::modIn, "ports round-trip through the codec");
        // Only per-voice nodes may drive a typed input: the effects tail runs once for the whole
        // mix and has no voice to feed back into, so both targets are global-tail violations (#127).
        auto tailIntoModIn = ports; tailIntoModIn.nodes.push_back(defaults("x1", "chorus"));
        tailIntoModIn.edges.push_back({"r1", "x1", 0.5});
        auto tailIntoExcite = tailIntoModIn;
        tailIntoModIn.edges.push_back({"x1", "f1", 0.5, AudioPort::modIn});
        ok &= expect(validate(tailIntoModIn) == "effects tail edge enters a per-voice node", "an effects node into fm.modIn is rejected as a global-tail violation");
        tailIntoExcite.edges.push_back({"x1", "r1", 0.5, AudioPort::exciteIn});
        ok &= expect(validate(tailIntoExcite) == "effects tail edge enters a per-voice node", "an effects node into resonator.exciteIn is rejected as a global-tail violation");
        // A typed edge is an ordinary edge for the 48-edge cap: it is counted, never exempt. The
        // maximal patch spends its 47th and 48th edges on exactly these two ports.
        auto capped = iupac::testing::maximalPatch();
        ok &= expect(capped.edges.size() == maximumEdges && validate(capped).empty(), "the maximal patch sits at the edge cap with both typed inputs cabled");
        auto underCap = capped; underCap.edges.pop_back();
        ok &= expect(underCap.edges.size() == maximumEdges - 1 && validate(underCap).empty(), "removing the exciteIn edge leaves the patch one edge under the cap");
        auto overCap = capped; overCap.edges.push_back({"n1", "f1", 0.5, AudioPort::modIn});
        ok &= expect(validate(overCap) == "patch exceeds structural cap", "a further modIn edge past the cap is rejected like any other edge");
    }

    // --- D8 decoder default-fill ---
    {
        auto base = decodePatchJson(encodePatchJson(maximal)); ok &= expect(static_cast<bool>(base), "maximal patch decodes");
        auto document = juce::JSON::parse(juce::String(encodePatchJson(maximal)));
        auto* harmonic = document.getDynamicObject()->getProperty("nodes").getArray()->getReference(0).getDynamicObject();
        auto* parameters = harmonic->getProperty("parameters").getDynamicObject();
        parameters->removeProperty("harmonicityMorph");
        auto filled = decodePatchValue(document);
        ok &= expect(static_cast<bool>(filled), "a node omitting a post-v1 parameter decodes");
        if (filled) { const auto& filledParameters = filled.value->nodes[0].parameters;
                      const auto found = std::ranges::find(filledParameters, "harmonicityMorph", &ParameterValue::id);
                      ok &= expect(found != filledParameters.end() && found->values.size() == 1 && found->values[0] == 0.0
                                   && filledParameters.size() == maximal.nodes[0].parameters.size(), "the omitted parameter decodes at its descriptor default"); }
        parameters->removeProperty("outputLevel");
        ok &= expect(!decodePatchValue(document).value, "a missing original parameter is still rejected");
        auto unknown = juce::JSON::parse(juce::String(encodePatchJson(maximal)));
        unknown.getDynamicObject()->getProperty("nodes").getArray()->getReference(0).getDynamicObject()->getProperty("parameters").getDynamicObject()->setProperty("notAParameter", 1.0);
        ok &= expect(!decodePatchValue(unknown).value, "an unknown parameter id is still rejected");
        auto outOfRange = juce::JSON::parse(juce::String(encodePatchJson(maximal)));
        outOfRange.getDynamicObject()->getProperty("nodes").getArray()->getReference(0).getDynamicObject()->getProperty("parameters").getDynamicObject()->setProperty("harmonicityMorph", 9.0);
        ok &= expect(!decodePatchValue(outOfRange).value, "an out-of-range post-v1 value is still rejected");
        auto threeEnvelopes = juce::JSON::parse(juce::String(encodePatchJson(maximal)));
        threeEnvelopes.getDynamicObject()->getProperty("envelopes").getArray()->removeLast();
        auto legacy = decodePatchValue(threeEnvelopes);
        ok &= expect(static_cast<bool>(legacy) && legacy.value->envelopes[3].attack == Envelope{}.attack && legacy.value->envelopes[3].release == Envelope{}.release,
                     "a patch with three envelopes decodes with E4 at its construction default");
        auto twoEnvelopes = juce::JSON::parse(juce::String(encodePatchJson(maximal)));
        twoEnvelopes.getDynamicObject()->getProperty("envelopes").getArray()->removeLast();
        twoEnvelopes.getDynamicObject()->getProperty("envelopes").getArray()->removeLast();
        ok &= expect(!decodePatchValue(twoEnvelopes).value, "fewer than three envelopes is still rejected");
    }

#ifdef IUPAC_SOURCE_DIR
    {
        // The fixed V8 worst-case benchmark patch is shipped as a snapshot so the arm64 container
        // benchmarks exactly the graph VERIFICATION describes. Regenerate with
        // IUPAC_WRITE_FIXTURES=1 when the catalog moves, and say so in the leaf that moves it.
        const std::filesystem::path fixture = std::filesystem::path(IUPAC_SOURCE_DIR) / "tests" / "fixtures" / "phase4-worst-case.snapshot.json";
        const auto worstCase = iupac::testing::worstCaseBenchmarkPatch();
        ok &= expect(worstCase.nodes.size() == maximumNodes && worstCase.edges.size() == maximumEdges && worstCase.matrix.size() == maximumMatrixRows,
                     "the worst-case benchmark patch is at every phase-4 bound");
        // D10: the worst case is a per-voice source-copy budget, not unison on every source. The
        // budget is derived from the catalog and pinned to the 9 VERIFICATION V8 states, so a
        // catalog change re-derives the fixture and is caught here instead of silently moving V8.
        ok &= expect(iupac::testing::worstCaseSourceCopyBudget() == 9,
                     "the catalog still derives the V8 per-voice source-copy budget of 9");
        std::size_t copies = 0, maximumUnison = 0;
        for (const auto& node : worstCase.nodes)
            for (const auto& parameter : node.parameters)
                if (parameter.id == "unisonVoices")
                {
                    copies += static_cast<std::size_t>(parameter.values[0]);
                    if (parameter.values[0] == iupac::testing::catalogParameter("harmonic", "unisonVoices").maximum) ++maximumUnison;
                }
        ok &= expect(copies == iupac::testing::worstCaseSourceCopyBudget() && maximumUnison == 1,
                     "the worst-case benchmark patch spends the source-copy budget on exactly one general source");
        std::size_t fullSpectra = 0;
        for (const auto& node : worstCase.nodes)
            for (const auto& parameter : node.parameters)
                if (parameter.id == "partialAmplitudes" && std::ranges::none_of(parameter.values, [](double v) { return v == 0.0; })) ++fullSpectra;
        ok &= expect(fullSpectra == 2, "both worst-case additive sources render a full 16-partial spectrum");
        ok &= expect(std::ranges::any_of(worstCase.edges, [](const AudioEdge& e) { return e.port == AudioPort::modIn; })
                         && std::ranges::any_of(worstCase.edges, [](const AudioEdge& e) { return e.port == AudioPort::exciteIn; }),
                     "the worst-case benchmark patch cables both typed audio-rate inputs");
        State state {worstCase, worstCase, {}};
        const auto encoded = encodeStateJson(state, true) + "\n";
        if (const auto* write = std::getenv("IUPAC_WRITE_FIXTURES"); write != nullptr && std::string_view(write) == "1")
        {
            std::ofstream output(fixture, std::ios::binary); output << encoded;
        }
        std::ifstream input(fixture, std::ios::binary); std::ostringstream stored; stored << input.rdbuf();
        ok &= expect(stored.str() == encoded, "tests/fixtures/phase4-worst-case.snapshot.json still encodes the worst-case benchmark patch");
    }
#endif
    return ok;
}
