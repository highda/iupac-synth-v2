#include "iupac/tools/Headless.hpp"

#include <cmath>
#include <fstream>
#include <iostream>

namespace
{
using namespace iupac;
bool expect(bool condition, const char* message) { if (!condition) std::cerr << "headless test failed: " << message << '\n'; return condition; }
domain::Node defaults(std::string id, std::string_view type)
{
    const auto* descriptor = domain::findModule(type); domain::Node node {std::move(id), descriptor->type, {}};
    for (const auto& parameter : descriptor->parameters) node.parameters.push_back({std::string(parameter.id), std::vector<double>(parameter.arraySize ? parameter.arraySize : 1, parameter.defaultValue)});
    if (type == "harmonic") { node.parameters[0].values[0] = 1; for (std::size_t i = 0; i < 16; ++i) node.parameters[1].values[i] = static_cast<double>(i + 1); }
    return node;
}
domain::Patch patch(std::string firstId = "tone", std::string secondId = "out")
{
    domain::Patch value; value.noiseSeed = 42; value.nodes = {defaults(firstId, "harmonic"), defaults(secondId, "mixer")};
    value.edges = {{firstId, secondId, 1}, {secondId, "output", 1}}; value.matrix = {{"row", true, domain::ModulationSource::macro1, secondId, "level", .25}}; return value;
}
}

bool runHeadlessTests()
{
    using namespace iupac; bool ok = true;
    const auto first = engine::compilePatch(patch()), renamed = engine::compilePatch(patch("arbitrary-z", "arbitrary-a"));
    ok &= expect(first && renamed, "fixture compiles");
    ok &= expect(tools::graphSignature(first.patch) == tools::graphSignature(renamed.patch), "graph signature ignores arbitrary IDs");
    domain::HostControls controls; const auto baseValue = tools::valueSignature(first.patch, controls); controls.macros[0] = .8;
    ok &= expect(baseValue != tools::valueSignature(first.patch, controls), "value signature includes effective controls");
    ok &= expect(tools::inspectCatalog().find("modulationSources") != std::string::npos, "catalog exposes modulation descriptors");
    ok &= expect(tools::inspectPatch(patch(), true).find("graphSignature") != std::string::npos, "compiled inspection uses production compiler");
    const auto midi = tools::decodeMidiJson(R"({"events":[{"sampleOffset":0,"type":"noteOn","channel":1,"data1":60,"data2":100},{"sampleOffset":1000,"type":"noteOff","channel":1,"data1":60}]})", 2048);
    ok &= expect(midi && midi.events.size() == 2, "sample-offset MIDI decodes");
    ok &= expect(!tools::decodeMidiJson(R"({"events":[{"sampleOffset":2,"type":"noteOn"},{"sampleOffset":1,"type":"noteOff"}]})", 8), "unsorted MIDI rejects");
    const auto directory = std::filesystem::temp_directory_path() / "iupac-headless-tests"; std::filesystem::create_directories(directory);
    const auto small = directory / "small.json", large = directory / "large.json"; { std::ofstream output(small); output << "{}"; } { std::ofstream output(large); std::string bytes(tools::maximumInputBytes + 1, 'x'); output << bytes; }
    ok &= expect(tools::readBoundedFile(small) && !tools::readBoundedFile(large), "file limit applies before parse");
    domain::State state {patch(), patch(), controls}; tools::RenderSettings settings {48000, 127, 2048}; const auto rendered = tools::renderSnapshot(state, midi.events, settings, directory / "render.wav");
    const auto manifest = juce::JSON::parse(rendered.manifestJson); const auto digest = manifest.getProperty("pcmSha256", {}).toString();
    ok &= expect(rendered && std::filesystem::file_size(directory / "render.wav") > 44 && digest.length() == 64, "production render writes float WAV manifest");
    std::error_code ignored; std::filesystem::remove_all(directory, ignored); return ok;
}
