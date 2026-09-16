#include "iupac/domain/ProductInfo.hpp"
#include "iupac/tools/Headless.hpp"
#if IUPAC_ENABLE_CHEMISTRY
#include "iupac/chemistry/Analysis.hpp"
#include "iupac/chemistry/Extension.hpp"
#include "iupac/chemistry/Mapping.hpp"
#endif

#include <iostream>
#include <map>
#include <fstream>
#include <string>

namespace
{
int fail(const std::string& message) { std::cerr << message << '\n'; return 2; }
std::map<std::string, std::string> options(int argc, char** argv, int begin, std::string& error)
{
    std::map<std::string, std::string> result;
    for (int i = begin; i < argc; i += 2)
    {
        if (i + 1 >= argc || std::string_view(argv[i]).substr(0, 2) != "--") { error = "options require --name VALUE pairs"; return {}; }
        if (!result.emplace(argv[i], argv[i + 1]).second) { error = "duplicate option: " + std::string(argv[i]); return {}; }
    }
    return result;
}
std::string required(const std::map<std::string, std::string>& opts, const char* key, std::string& error)
{
    const auto it = opts.find(key);
    if (it == opts.end() || it->second.empty()) error = "missing required option: " + std::string(key);
    return it == opts.end() ? std::string{} : it->second;
}
#if IUPAC_ENABLE_CHEMISTRY
juce::var protocolRequest(std::string_view action)
{
    juce::var value(new juce::DynamicObject); auto* object=value.getDynamicObject();
    object->setProperty("protocolVersion", iupac::chemistry::protocolVersion);
    object->setProperty("requestId", "cli-1"); object->setProperty("action", juce::String::fromUTF8(action.data(), static_cast<int>(action.size()))); return value;
}
#endif
}

int main(int argc, char** argv)
{
    using namespace iupac;
    if (argc == 1) { std::cout << "{\"product\":\"" << domain::productName() << "\",\"architecture\":" << domain::architectureVersion() << ",\"chemistryEnabled\":"
#if IUPAC_ENABLE_CHEMISTRY
    << "true"
#else
    << "false"
#endif
    << "}\n"; return 0; }
    if (argc == 2 && std::string_view(argv[1]) == "--catalog") { std::cout << tools::inspectCatalog() << '\n'; return 0; }
    if (argc == 3 && std::string_view(argv[1]) == "--validate-patch")
    {
        const auto input = tools::readBoundedFile(argv[2]); if (!input) return fail(input.error);
        const auto patch = domain::decodePatchJson(*input.value); if (!patch) return fail(patch.error);
        std::cout << domain::encodePatchJson(*patch.value, true) << '\n'; return 0;
    }
    const std::string command = argv[1]; std::string error; const auto opts = options(argc, argv, 2, error); if (!error.empty()) return fail(error);
#if IUPAC_ENABLE_CHEMISTRY
    if (command == "discover" || command == "record" || command == "cache-inspect" || command == "cache-clear" || command == "cache-get")
    {
        auto request=protocolRequest(command); auto* object=request.getDynamicObject();
        if(command=="discover") { const auto query=required(opts,"--query",error); if(!error.empty())return fail(error); object->setProperty("query",juce::String(query)); object->setProperty("prefix",opts.contains("--prefix")&&opts.at("--prefix")!="0"); if(opts.contains("--limit")){try{object->setProperty("limit",std::stoi(opts.at("--limit")));}catch(...){return fail("discovery limit is invalid");}} }
        else if(command=="record") object->setProperty("recordId",juce::String(required(opts,"--record-id",error)));
        else if(command=="cache-get") object->setProperty("key",juce::String(required(opts,"--key",error)));
        if(!error.empty())return fail(error);
        auto reply=chemistry::invokeProtocol(chemistry::locatePackagedHelper(opts.contains("--helper-root")?opts.at("--helper-root"):std::string{}),request);
        if(!reply)return fail(reply.error);std::cout<<reply.responseJson<<'\n';return 0;
    }
    if (command == "analyze" || command == "generate")
    {
        const auto modeText=required(opts,"--mode",error), text=required(opts,"--text",error);
        if(!error.empty()||(opts.size()!=2&&opts.size()!=3))return fail(error.empty()?"unexpected chemistry option":error);
        if(modeText!="name"&&modeText!="smiles")return fail("mode must be name or smiles");
        chemistry::HelperConfiguration configuration=chemistry::locatePackagedHelper(opts.contains("--helper-root")?opts.at("--helper-root"):std::string{});
        auto reply=chemistry::invokeHelper(configuration,modeText=="name"?chemistry::InputMode::name:chemistry::InputMode::smiles,text,"cli-1");
        if(!reply)return fail(reply.error);
        if(command=="analyze"){std::cout<<reply.responseJson<<'\n';return 0;}
        auto generated=chemistry::generate(*reply.analysis);if(!generated)return fail(generated.error);
        chemistry::ApplyResult applied;applied.generation=1;applied.mode=modeText=="name"?chemistry::InputMode::name:chemistry::InputMode::smiles;applied.input=text;applied.analysis=reply.analysis;applied.intent=generated.intent;applied.patch=generated.patch;applied.trace=generated.trace;applied.helperResponse=reply.response;
        domain::State state{*generated.patch,*generated.patch,{},chemistry::makeProvenance(applied)};
        std::cout<<domain::encodeStateJson(state,true)<<'\n';return 0;
    }
#endif
    if (command == "inspect")
    {
        const auto stage = required(opts, "--stage", error); if (!error.empty()) return fail(error);
        if (stage == "catalog") { if (opts.size() != 1) return fail("catalog inspection accepts no input"); std::cout << tools::inspectCatalog() << '\n'; return 0; }
        if (stage == "patch" || stage == "compiled")
        {
            const auto path = required(opts, "--patch", error); if (!error.empty() || opts.size() != 2) return fail(error.empty() ? "unexpected inspect option" : error);
            const auto input = tools::readBoundedFile(path); if (!input) return fail(input.error); const auto patch = domain::decodePatchJson(*input.value); if (!patch) return fail(patch.error);
            std::cout << tools::inspectPatch(*patch.value, stage == "compiled") << '\n'; return 0;
        }
        if (stage == "effective")
        {
            const auto path = required(opts, "--snapshot", error); if (!error.empty() || opts.size() != 2) return fail(error.empty() ? "unexpected inspect option" : error);
            const auto input = tools::readBoundedFile(path); if (!input) return fail(input.error); const auto state = domain::decodeStateJson(*input.value); if (!state) return fail(state.error);
            std::cout << tools::inspectEffective(*state.value) << '\n'; return 0;
        }
#if IUPAC_ENABLE_CHEMISTRY
        if (stage == "sonic" || stage == "mapping")
        {
            const auto path=required(opts,"--analysis",error), request=required(opts,"--request-id",error);
            if(!error.empty()||opts.size()!=3)return fail(error.empty()?"unexpected chemistry inspect option":error);
            const auto input=tools::readBoundedFile(path);if(!input)return fail(input.error);
            const auto analysis=chemistry::decodeAnalysisResponse(*input.value,request);if(!analysis)return fail(analysis.error);
            const auto generated=chemistry::generate(*analysis.value);if(!generated)return fail(generated.error);
            std::cout<<juce::JSON::toString(stage=="sonic"?chemistry::encodeSonicIntent(*generated.intent):generated.trace,true)<<'\n';return 0;
        }
#endif
        return fail("unsupported inspection stage");
    }
    if (command == "render")
    {
        const auto snapshotPath = required(opts, "--snapshot", error), midiPath = required(opts, "--midi", error), outputPath = required(opts, "--output", error);
        const auto sampleRate = required(opts, "--sample-rate", error), blockSize = required(opts, "--block-size", error);
        if (!error.empty() || (opts.size() != 5 && opts.size() != 6)) return fail(error.empty() ? "unexpected render option" : error);
        const auto snapshot = tools::readBoundedFile(snapshotPath), midiText = tools::readBoundedFile(midiPath); if (!snapshot) return fail(snapshot.error); if (!midiText) return fail(midiText.error);
        const auto state = domain::decodeStateJson(*snapshot.value); if (!state) return fail(state.error); tools::RenderSettings settings;
        try { settings.sampleRate = std::stod(sampleRate); settings.blockSize = std::stoull(blockSize); settings.samples = opts.contains("--samples") ? std::stoull(opts.at("--samples")) : static_cast<std::size_t>(settings.sampleRate * 3.0); }
        catch (...) { return fail("render numeric option is invalid"); }
        const auto midi = tools::decodeMidiJson(*midiText.value, settings.samples); if (!midi) return fail(midi.error);
        const auto result = tools::renderSnapshot(*state.value, midi.events, settings, outputPath); if (!result) return fail(result.error); std::cout << result.manifestJson << '\n'; return 0;
    }
    if (command == "verify-panel")
    {
        const auto panel = required(opts, "--panel", error), output = required(opts, "--output-dir", error); if (!error.empty() || opts.size() != 2) return fail(error.empty() ? "unexpected verify-panel option" : error);
        std::string report; if (tools::verifyPanel(panel, output, report, error) != 0) return fail(error); std::cout << report << '\n'; return 0;
    }
    if (command == "benchmark")
    {
        const auto snapshotPath = required(opts, "--snapshot", error), secondsText = required(opts, "--seconds", error); if (!error.empty() || opts.size() != 2) return fail(error.empty() ? "unexpected benchmark option" : error);
        const auto snapshot = tools::readBoundedFile(snapshotPath); if (!snapshot) return fail(snapshot.error); const auto state = domain::decodeStateJson(*snapshot.value); if (!state) return fail(state.error);
        std::size_t seconds{}; try { seconds = std::stoull(secondsText); } catch (...) { return fail("invalid benchmark duration"); }
        const auto report = tools::benchmark(*state.value, seconds, error); if (!error.empty()) return fail(error); std::cout << report << '\n'; return 0;
    }
    return fail("unknown command");
}
