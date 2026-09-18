#include "iupac/tools/Headless.hpp"
#include "iupac/engine/PatchCoordinator.hpp"
#include "iupac/domain/ProductInfo.hpp"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#if defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif
#include <iomanip>
#include <numeric>
#include <sstream>

namespace iupac::tools
{
namespace
{
juce::var object() { return juce::var(new juce::DynamicObject); }
void put(juce::var& value, const char* key, juce::var item) { value.getDynamicObject()->setProperty(key, std::move(item)); }
std::string json(const juce::var& value) { return juce::JSON::toString(value, false).toStdString(); }
std::string hash(std::string_view value) { return juce::SHA256(value.data(), value.size()).toHexString().toStdString(); }
std::string moduleName(domain::ModuleType type) { return std::string(domain::moduleCatalog().at(static_cast<std::size_t>(type)).id); }
std::string sourceName(domain::ModulationSource source)
{
    constexpr std::array names {"e1", "e2", "e3", "l1", "l2", "velocity", "keyTracking", "pitchBend", "cc1", "macro1", "macro2", "macro3", "macro4"};
    return names.at(static_cast<std::size_t>(source));
}
std::string targetName(engine::ParameterTarget target)
{
    constexpr std::array names {"carrierRatio", "modulatorRatio", "index", "burstMs", "tuneRatio", "combFeedback", "modalQ", "cutoff", "q", "drive", "wet", "level", "pan", "outputLevel"};
    return names.at(static_cast<std::size_t>(target));
}
std::string kindName(domain::ParameterKind kind)
{
    constexpr std::array names {"continuous", "discrete", "coefficientArray", "convenience"};
    return names.at(static_cast<std::size_t>(kind));
}
std::string scaleName(domain::ParameterScale scale) { return scale == domain::ParameterScale::linear ? "linear" : "logarithmic"; }

// Canonical labelling prevents user IDs or compiler tie-breaking from manufacturing graph
// variety. The lexicographic minimum over all node relabellings always lists module types in
// sorted name order, so only permutations inside each same-type group can change the result;
// with the per-type slot caps that is at most 3!*2!^4 candidates at the 11-node cap, and the
// minimum is identical to the exhaustive search over every permutation.
std::string canonicalGraph(const engine::CompiledPatch& patch)
{
    std::vector<std::size_t> order(patch.nodeCount); std::iota(order.begin(), order.end(), 0);
    std::ranges::stable_sort(order, {}, [&](std::size_t node) { return moduleName(patch.nodes[node].type); });
    std::vector<std::pair<std::size_t, std::size_t>> groups;
    for (std::size_t i = 0; i < order.size();)
    {
        std::size_t j = i; while (j < order.size() && patch.nodes[order[j]].type == patch.nodes[order[i]].type) ++j;
        groups.emplace_back(i, j); i = j;
    }
    const auto candidateFor = [&]
    {
        std::array<std::size_t, domain::maximumNodes> label{};
        for (std::size_t i = 0; i < order.size(); ++i) label[order[i]] = i;
        std::ostringstream stream;
        for (auto old : order) stream << moduleName(patch.nodes[old].type) << ';';
        std::vector<std::string> edges;
        for (std::size_t i = 0; i < patch.edgeCount; ++i)
        {
            const auto& edge = patch.edges[i];
            edges.push_back(std::to_string(label[edge.source]) + ">" + (edge.toOutput ? "out" : std::to_string(label[edge.destination])));
        }
        std::ranges::sort(edges); stream << '|'; for (const auto& edge : edges) stream << edge << ';';
        std::vector<std::string> rows;
        for (std::size_t i = 0; i < patch.rowCount; ++i)
        {
            const auto& row = patch.rows[i];
            rows.push_back(sourceName(row.source) + ">" + std::to_string(label[row.node]) + ':' + targetName(row.target));
        }
        std::ranges::sort(rows); stream << '|'; for (const auto& row : rows) stream << row << ';';
        return stream.str();
    };
    // Odometer over the per-group permutations: advance the first group with a next permutation;
    // std::next_permutation has already reset every exhausted group before it.
    std::string best;
    for (;;)
    {
        const auto candidate = candidateFor(); if (best.empty() || candidate < best) best = candidate;
        std::size_t group = 0;
        while (group < groups.size() && !std::next_permutation(order.begin() + static_cast<std::ptrdiff_t>(groups[group].first), order.begin() + static_cast<std::ptrdiff_t>(groups[group].second))) ++group;
        if (group == groups.size()) break;
    }
    return best;
}

juce::var compiledValue(const engine::CompiledPatch& patch, const domain::HostControls& controls)
{
    auto root = object(); put(root, "productVersion", juce::String(domain::productVersion().data())); put(root, "architecture", juce::String(domain::architectureVersion().data())); put(root, "patchVersion", domain::patchVersion);
    juce::Array<juce::var> nodes;
    for (std::size_t i = 0; i < patch.nodeCount; ++i)
    {
        auto node = object(); put(node, "slot", static_cast<int>(i)); put(node, "type", juce::String(moduleName(patch.nodes[i].type))); nodes.add(node);
    }
    juce::Array<juce::var> edges;
    for (std::size_t i = 0; i < patch.edgeCount; ++i)
    {
        const auto& source = patch.edges[i]; auto edge = object(); put(edge, "source", source.source);
        put(edge, "destination", source.toOutput ? juce::var("output") : juce::var(source.destination)); put(edge, "gain", source.gain); edges.add(edge);
    }
    juce::Array<juce::var> rows;
    for (std::size_t i = 0; i < patch.rowCount; ++i)
    {
        const auto& source = patch.rows[i]; auto row = object(); put(row, "source", juce::String(sourceName(source.source)));
        put(row, "destinationSlot", source.node); put(row, "destinationParameter", juce::String(targetName(source.target))); put(row, "depth", source.depth); rows.add(row);
    }
    put(root, "nodes", nodes); put(root, "edges", edges); put(root, "matrix", rows);
    put(root, "oversamplingFactor", static_cast<int>(engine::internalOversamplingFactor));
    put(root, "graphSignature", juce::String(graphSignature(patch))); put(root, "valueSignature", juce::String(valueSignature(patch, controls)));
    return root;
}
}

ReadResult readBoundedFile(const std::filesystem::path& path)
{
    std::error_code ec; const auto size = std::filesystem::file_size(path, ec);
    if (ec) return {{}, "cannot read file: " + path.string()};
    if (size > maximumInputBytes) return {{}, "input exceeds 1 MiB limit: " + path.string()};
    std::ifstream input(path, std::ios::binary); if (!input) return {{}, "cannot open file: " + path.string()};
    std::string value(static_cast<std::size_t>(size), '\0');
    if (size != 0 && !input.read(value.data(), static_cast<std::streamsize>(size))) return {{}, "failed reading file: " + path.string()};
    return {std::move(value), {}};
}

MidiResult decodeMidiJson(std::string_view text, std::size_t maximumSamples)
{
    MidiResult result; const auto root = juce::JSON::parse(juce::String::fromUTF8(text.data(), static_cast<int>(text.size())));
    const auto* objectValue = root.getDynamicObject(); const auto* events = objectValue ? objectValue->getProperty("events").getArray() : nullptr;
    if (!objectValue || !events || objectValue->getProperties().size() != 1 || events->size() > static_cast<int>(engine::maximumMidiEventsPerBlock)) { result.error = "invalid or oversized MIDI event stream"; return result; }
    std::uint32_t previous{};
    for (const auto& item : *events)
    {
        const auto* eventObject = item.getDynamicObject();
        if (!eventObject) { result.error = "invalid MIDI event"; return result; }
        const auto offsetValue = eventObject->getProperty("sampleOffset");
        if (!(offsetValue.isInt() || offsetValue.isInt64())) { result.error = "invalid MIDI offset"; return result; }
        const auto offset = static_cast<std::int64_t>(static_cast<juce::int64>(offsetValue));
        if (offset < 0 || static_cast<std::size_t>(offset) >= maximumSamples || (!result.events.empty() && offset < previous)) { result.error = "MIDI offsets must be sorted and within render"; return result; }
        const auto integer = [&](const char* key, int fallback) { const auto value = eventObject->getProperty(key); return value.isVoid() ? fallback : static_cast<int>(value); };
        const int channel = integer("channel", 1), data1 = integer("data1", 0), data2 = integer("data2", 0), bend = integer("bend", 8192);
        if (channel < 1 || channel > 16 || data1 < 0 || data1 > 127 || data2 < 0 || data2 > 127 || bend < 0 || bend > 16383) { result.error = "MIDI value outside bounds"; return result; }
        engine::MidiEvent event {static_cast<std::uint32_t>(offset), {}, static_cast<std::uint8_t>(channel), static_cast<std::uint8_t>(data1), static_cast<std::uint8_t>(data2), static_cast<std::uint16_t>(bend)};
        const auto type = eventObject->getProperty("type").toString();
        if (type == "noteOn") event.type = engine::MidiEventType::noteOn; else if (type == "noteOff") event.type = engine::MidiEventType::noteOff;
        else if (type == "pitchBend") event.type = engine::MidiEventType::pitchBend; else if (type == "controlChange") event.type = engine::MidiEventType::controlChange;
        else { result.error = "unknown MIDI event type"; return result; }
        previous = event.sampleOffset; result.events.push_back(event);
    }
    return result;
}

std::string inspectCatalog()
{
    auto root = object(); put(root, "productVersion", juce::String(domain::productVersion().data())); put(root, "architecture", juce::String(domain::architectureVersion().data())); put(root, "patchVersion", domain::patchVersion);
    juce::Array<juce::var> sources; for (std::size_t i = 0; i <= static_cast<std::size_t>(domain::ModulationSource::macro4); ++i) sources.add(juce::String(sourceName(static_cast<domain::ModulationSource>(i)))); put(root, "modulationSources", sources);
    juce::Array<juce::var> modules;
    for (const auto& descriptor : domain::moduleCatalog())
    {
        auto module = object(); put(module, "id", juce::String(descriptor.id.data())); put(module, "source", descriptor.source); put(module, "typeCap", static_cast<int>(descriptor.typeCap)); juce::Array<juce::var> parameters;
        for (const auto& descriptorParameter : descriptor.parameters)
        {
            auto parameter = object(); put(parameter, "id", juce::String(descriptorParameter.id.data())); put(parameter, "unit", juce::String(descriptorParameter.unit.data()));
            put(parameter, "minimum", descriptorParameter.minimum); put(parameter, "maximum", descriptorParameter.maximum); put(parameter, "default", descriptorParameter.defaultValue);
            put(parameter, "scale", juce::String(scaleName(descriptorParameter.scale))); put(parameter, "kind", juce::String(kindName(descriptorParameter.kind)));
            put(parameter, "modulatable", descriptorParameter.modulatable); put(parameter, "arraySize", static_cast<int>(descriptorParameter.arraySize)); parameters.add(parameter);
        }
        put(module, "parameters", parameters); modules.add(module);
    }
    put(root, "modules", modules); return json(root);
}

std::string graphSignature(const engine::CompiledPatch& patch) { return hash(canonicalGraph(patch)); }

std::string valueSignature(const engine::CompiledPatch& patch, const domain::HostControls& controls)
{
    std::ostringstream stream; stream << std::setprecision(9) << canonicalGraph(patch) << '|';
    // Compiled values, enabled rows and live edges only; IDs, provenance and traces never enter.
    for (std::size_t i = 0; i < patch.nodeCount; ++i)
    {
        const auto& value = patch.nodes[i].values; const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
        for (std::size_t byte = 0; byte < sizeof(value); ++byte) stream << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(bytes[byte]);
    }
    for (std::size_t i = 0; i < patch.edgeCount; ++i) stream << patch.edges[i].gain << ',';
    for (std::size_t i = 0; i < patch.rowCount; ++i) stream << patch.rows[i].depth << ',';
    for (const auto& envelope : patch.envelopes) stream << envelope.attack << ',' << envelope.decay << ',' << envelope.sustain << ',' << envelope.release << ',';
    for (const auto& lfo : patch.lfos) stream << lfo.rate << ',' << static_cast<int>(lfo.waveform) << ',';
    stream << patch.noiseSeed; for (const auto macro : controls.macros) stream << ',' << macro;
    stream << ',' << controls.outputGain << ',' << controls.width << ',' << controls.masterTune << ',' << controls.bypass;
    return hash(stream.str());
}

std::string inspectPatch(const domain::Patch& patch, bool compiled)
{
    if (!compiled) return domain::encodePatchJson(patch);
    const auto result = engine::compilePatch(patch); return result ? json(compiledValue(result.patch, {})) : std::string{};
}

std::string inspectEffective(const domain::State& state)
{
    const auto result = engine::compilePatch(state.editedPatch); if (!result) return {};
    auto root = object(); put(root, "productVersion", juce::String(domain::productVersion().data())); put(root, "architecture", juce::String(domain::architectureVersion().data())); put(root, "stateVersion", domain::stateVersion);
    put(root, "compiled", compiledValue(result.patch, state.controls)); put(root, "controls", domain::encodeStateValue(state).getProperty("controls", {}));
    put(root, "valueSignature", juce::String(valueSignature(result.patch, state.controls))); return json(root);
}

RenderResult renderSnapshot(const domain::State& state, std::span<const engine::MidiEvent> midi, const RenderSettings& settings, const std::filesystem::path& wav)
{
    if (!std::isfinite(settings.sampleRate) || settings.sampleRate < 8000 || settings.sampleRate > 192000 || settings.blockSize < 1 || settings.blockSize > engine::maximumModuleBlockSize || settings.samples < 1 || settings.samples > static_cast<std::size_t>(settings.sampleRate * 60.0)) return {{}, "render settings outside bounds"};
    const auto compiled = engine::compilePatch(state.editedPatch); if (!compiled) return {{}, compiled.error};
    engine::Engine engine; engine.prepare(settings.sampleRate, settings.blockSize); engine.setPatch(compiled.patch); engine.setControls(state.controls);
    std::vector<float> left(settings.samples), right(settings.samples); engine.render(left, right, midi);
    juce::AudioBuffer<float> buffer(2, static_cast<int>(settings.samples)); buffer.copyFrom(0, 0, left.data(), static_cast<int>(settings.samples)); buffer.copyFrom(1, 0, right.data(), static_cast<int>(settings.samples));
    std::error_code ec; if (!wav.parent_path().empty()) std::filesystem::create_directories(wav.parent_path(), ec); if (ec) return {{}, "cannot create WAV directory"};
    auto output = std::make_unique<juce::FileOutputStream>(juce::File(wav.string())); if (!output->openedOk()) return {{}, "cannot create WAV"};
    juce::WavAudioFormat format; std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(output.release(), settings.sampleRate, 2, 32, {}, 0));
    if (!writer || !writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples())) return {{}, "cannot write WAV"}; writer.reset();
    double sum = 0, squared = 0, peak = 0; for (std::size_t i = 0; i < left.size(); ++i) { sum += left[i] + right[i]; squared += left[i]*left[i] + right[i]*right[i]; peak = std::max({peak, std::abs(static_cast<double>(left[i])), std::abs(static_cast<double>(right[i]))}); }
    auto root = object(); put(root, "productVersion", juce::String(domain::productVersion().data())); put(root, "architecture", juce::String(domain::architectureVersion().data())); put(root, "patchVersion", domain::patchVersion); put(root, "stateVersion", domain::stateVersion);
    put(root, "command", juce::String("iupac-cli render")); put(root, "sampleRate", settings.sampleRate); put(root, "blockSize", static_cast<int>(settings.blockSize)); put(root, "samples", static_cast<juce::int64>(settings.samples));
    put(root, "latencySamples", engine.latencySamples()); put(root, "guardHits", static_cast<juce::int64>(engine.guardHits())); put(root, "peak", peak); put(root, "rms", std::sqrt(squared / (2 * left.size()))); put(root, "dc", sum / (2 * left.size())); put(root, "graphSignature", juce::String(graphSignature(compiled.patch)));
    std::string pcmBytes; pcmBytes.reserve((left.size() + right.size()) * sizeof(float));
    pcmBytes.append(reinterpret_cast<const char*>(left.data()), left.size() * sizeof(float)); pcmBytes.append(reinterpret_cast<const char*>(right.data()), right.size() * sizeof(float));
    put(root, "valueSignature", juce::String(valueSignature(compiled.patch, state.controls))); put(root, "pcmSha256", juce::String(hash(pcmBytes))); put(root, "wav", juce::String(wav.string()));
    return {json(root), {}};
}

int verifyPanel(const std::filesystem::path& panel, const std::filesystem::path& output, std::string& report, std::string& error)
{
    const auto input = readBoundedFile(panel); if (!input) { error = input.error; return 2; }
    const auto root = juce::JSON::parse(*input.value); const auto* panelObject = root.getDynamicObject(); const auto* cases = panelObject ? panelObject->getProperty("cases").getArray() : nullptr;
    if (!panelObject || !cases || cases->isEmpty() || cases->size() > static_cast<int>(maximumPanelCases)) { error = "invalid authored panel"; return 2; }
    const auto sampleRateValue = panelObject->getProperty("sampleRate"), blockSizeValue = panelObject->getProperty("blockSize");
    if (!(sampleRateValue.isInt() || sampleRateValue.isInt64()) || !(blockSizeValue.isInt() || blockSizeValue.isInt64())) { error = "panel requires integer sampleRate and blockSize"; return 2; }
    std::error_code ec; std::filesystem::create_directories(output, ec); if (ec) { error = "cannot create panel output directory"; return 2; }
    juce::Array<juce::var> results;
    for (const auto& item : *cases)
    {
        const auto* caseObject = item.getDynamicObject(); if (!caseObject || !caseObject->getProperty("id").isString()) { error = "invalid panel case"; return 2; }
        const auto id = caseObject->getProperty("id").toString().toStdString(); const auto base = panel.parent_path();
        const auto snapshotText = readBoundedFile(base / caseObject->getProperty("snapshot").toString().toStdString()), midiText = readBoundedFile(base / caseObject->getProperty("midi").toString().toStdString());
        if (!snapshotText || !midiText) { error = id + ": " + (snapshotText ? midiText.error : snapshotText.error); return 2; }
        const auto state = domain::decodeStateJson(*snapshotText.value); if (!state) { error = id + ": " + state.error; return 2; }
        const auto samplesValue = caseObject->getProperty("samples"); if (!(samplesValue.isInt() || samplesValue.isInt64()) || static_cast<juce::int64>(samplesValue) <= 0) { error = id + ": invalid samples"; return 2; }
        RenderSettings settings {static_cast<double>(sampleRateValue), static_cast<std::size_t>(static_cast<juce::int64>(blockSizeValue)), static_cast<std::size_t>(static_cast<juce::int64>(samplesValue))};
        const auto midi = decodeMidiJson(*midiText.value, settings.samples); if (!midi) { error = id + ": " + midi.error; return 2; }
        const auto rendered = renderSnapshot(*state.value, midi.events, settings, output / (id + ".wav")); if (!rendered) { error = id + ": " + rendered.error; return 2; }
        auto entry = juce::JSON::parse(rendered.manifestJson); entry.getDynamicObject()->setProperty("id", juce::String(id)); results.add(entry);
    }
    auto result = object(); put(result, "panelVersion", 1); put(result, "productVersion", juce::String(domain::productVersion().data())); put(result, "architecture", juce::String(domain::architectureVersion().data())); put(result, "patchVersion", domain::patchVersion);
    put(result, "command", juce::String("iupac-cli verify-panel")); put(result, "panelSha256", juce::String(hash(*input.value))); put(result, "results", results); report = json(result);
    std::ofstream manifest(output / "manifest.json", std::ios::binary); manifest << report << '\n'; if (!manifest) { error = "cannot write panel manifest"; return 2; } return 0;
}

std::string benchmark(const domain::State& state, std::size_t seconds, std::string& error)
{
    constexpr double sampleRate = 48000; constexpr std::size_t blockSize = 128;
    if (seconds < 1 || seconds > 60) { error = "benchmark duration must be 1..60 seconds"; return {}; }
    const auto compiled = engine::compilePatch(state.editedPatch); if (!compiled) { error = compiled.error; return {}; }
    auto alternatePatch = state.editedPatch;
    if (alternatePatch.nodes.empty()) { error = "benchmark requires an audible authored patch"; return {}; }
    const auto oldId = alternatePatch.nodes.front().id;
    alternatePatch.nodes.front().id += "-transition";
    for (auto& edge : alternatePatch.edges) { if (edge.source == oldId) edge.source = alternatePatch.nodes.front().id; if (edge.destination == oldId) edge.destination = alternatePatch.nodes.front().id; }
    for (auto& row : alternatePatch.matrix) if (row.destinationNode == oldId) row.destinationNode = alternatePatch.nodes.front().id;
    const auto alternate = engine::compilePatch(alternatePatch); if (!alternate) { error = "cannot prepare transition benchmark: " + alternate.error; return {}; }
    engine::PatchCoordinator engine; engine.prepare(sampleRate, blockSize); (void) engine.publish(compiled.patch, state.controls);
    std::array<float, blockSize> left{}, right{}; std::vector<double> blockTimes; blockTimes.reserve(seconds * 375);
    engine.render(left, right); for (int block = 0; block < 8; ++block) engine.render(left, right);
    std::vector<engine::MidiEvent> starts; for (int i = 0; i < 16; ++i) starts.push_back({0, engine::MidiEventType::noteOn, 1, static_cast<std::uint8_t>(36 + i * 3), 100, 8192}); engine.render(left, right, starts);
    std::size_t transitions = 0;
    for (std::size_t warmup = 0; warmup < 5 * 375; ++warmup) { if (warmup % 32 == 0) (void) engine.publish((transitions++ % 2) ? compiled.patch : alternate.patch, state.controls); engine.render(left, right); }
    const auto residentBytes = [] {
#if defined(__linux__)
        long pages = 0, resident = 0; std::ifstream stat("/proc/self/statm"); stat >> pages >> resident;
        return resident > 0 ? static_cast<std::uint64_t>(resident) * static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE)) : 0;
#elif defined(__APPLE__)
        mach_task_basic_info info{}; mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        return ::task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS ? static_cast<std::uint64_t>(info.resident_size) : 0;
#else
        return std::uint64_t{};
#endif
    };
    const auto rssBefore = residentBytes(); std::uint64_t rssSteady = rssBefore;
    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t block = 0; block < seconds * 375; ++block)
    {
        if (block % 32 == 0) { (void) engine.publish((transitions++ % 2) ? compiled.patch : alternate.patch, state.controls); }
        const auto blockBegin = std::chrono::steady_clock::now(); engine.render(left, right); const auto blockEnd = std::chrono::steady_clock::now();
        blockTimes.push_back(std::chrono::duration<double>(blockEnd - blockBegin).count());
        if (block + 1 == seconds * 375 / 2) rssSteady = residentBytes();
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count(); std::ranges::sort(blockTimes);
    auto root = object(); put(root, "productVersion", juce::String(domain::productVersion().data())); put(root, "architecture", juce::String(domain::architectureVersion().data())); put(root, "command", juce::String("iupac-cli benchmark --snapshot FILE --seconds N"));
    put(root, "sampleRate", sampleRate); put(root, "blockSize", static_cast<int>(blockSize)); put(root, "voices", 16); put(root, "warmupSeconds", 5); put(root, "seconds", static_cast<int>(seconds));
    put(root, "renderRatio", elapsed / static_cast<double>(seconds)); put(root, "p99BlockSeconds", blockTimes[static_cast<std::size_t>(std::floor((blockTimes.size() - 1) * .99))]);
    put(root, "structuralTransitions", static_cast<juce::int64>(transitions)); put(root, "activeBanksMaximum", 2); put(root, "residentBytesBefore", static_cast<juce::int64>(rssBefore)); put(root, "residentBytesSteady", static_cast<juce::int64>(rssSteady)); put(root, "residentBytesAfter", static_cast<juce::int64>(residentBytes()));
    put(root, "graphSignature", juce::String(graphSignature(compiled.patch))); put(root, "valueSignature", juce::String(valueSignature(compiled.patch, state.controls))); return json(root);
}
}
