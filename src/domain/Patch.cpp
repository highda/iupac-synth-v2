#include "iupac/domain/Patch.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <unordered_map>

namespace iupac::domain
{
namespace
{
using PD = ParameterDescriptor;
using PK = ParameterKind;
using PS = ParameterScale;

PD scalar(std::string_view id, std::string_view unit, double lo, double hi, double def, PS scale = PS::linear, bool mod = true)
{
    return {id, unit, lo, hi, def, scale, PK::continuous, mod, 0, id == "outputLevel" || id == "level" ? 5.0 : 20.0, {}};
}
// `defaultIndex` names the entry the v3.2 parameter table declares as the default; it is zero for
// every choice that existed before D8 and 4 (`1/8`) for the delay's `syncDivision` (#123).
PD choice(std::string_view id, std::initializer_list<std::string_view> values, std::size_t defaultIndex = 0)
{
    return {id, "", 0, static_cast<double>(values.size() - 1), static_cast<double>(defaultIndex), PS::linear, PK::discrete, false, 0, 0, values};
}
PD array(std::string_view id, double lo, double hi, double def, std::size_t size)
{
    return {id, "", lo, hi, def, PS::linear, PK::coefficientArray, false, size, 20, {}};
}
PD convenience(std::string_view id, double lo, double hi, double def)
{
    return {id, "", lo, hi, def, PS::linear, PK::convenience, false, 0, 0, {}};
}
// A numeric discrete control (octave, coarse, unison copies): integral values only, never a matrix destination.
PD step(std::string_view id, std::string_view unit, double lo, double hi, double def)
{
    return {id, unit, lo, hi, def, PS::linear, PK::discrete, false, 0, 0, {}};
}
// D8 marks every parameter introduced after the original v1 catalog: the decoder may fill it from
// this default when a stored node omits it, which is what keeps every pre-D8 patch valid.
PD added(PD descriptor) { descriptor.postV1 = true; return descriptor; }

const std::array<ModuleDescriptor, moduleTypeCount> catalog {{
    {ModuleType::harmonic, "harmonic", true, 3, true, false, AudioPort::in, {array("partialAmplitudes", 0, 1, 0, 16), array("partialRatios", 0.5, 32, 1, 16), array("partialPans", -1, 1, 0, 16), convenience("tilt", -2, 2, 0), convenience("inharmonicity", 0, 0.02, 0), scalar("outputLevel", "", 0, 1, 0.7),
        added(step("unisonVoices", "count", 1, 7, 1)), added(scalar("detuneCents", "cents", 0, 50, 12)), added(scalar("unisonSpread", "", 0, 1, 0.5)), added(scalar("phaseRandom", "", 0, 1, 0, PS::linear, false)), added(scalar("drift", "", 0, 1, 0)),
        added(scalar("harmonicityMorph", "", 0, 1, 0)), added(scalar("oddEvenBalance", "", -1, 1, 0)), added(scalar("symmetry", "", 0, 1, 0.5)),
        added(step("octave", "oct", -3, 3, 0)), added(step("coarse", "semitones", -12, 12, 0)), added(scalar("fine", "cents", -100, 100, 0)), added(scalar("keytrack", "", 0, 1, 1, PS::linear, false))}},
    {ModuleType::fm, "fm", true, 3, true, false, AudioPort::modIn, {scalar("carrierRatio", "ratio", 0.5, 4, 1, PS::logarithmic), scalar("modulatorRatio", "ratio", 0.25, 8, 1, PS::logarithmic), scalar("index", "", 0, 6, 0), scalar("outputLevel", "", 0, 1, 0.7),
        added(step("unisonVoices", "count", 1, 7, 1)), added(scalar("detuneCents", "cents", 0, 50, 12)), added(scalar("unisonSpread", "", 0, 1, 0.5)), added(scalar("phaseRandom", "", 0, 1, 0, PS::linear, false)), added(scalar("drift", "", 0, 1, 0)),
        added(step("octave", "oct", -3, 3, 0)), added(step("coarse", "semitones", -12, 12, 0)), added(scalar("fine", "cents", -100, 100, 0)), added(scalar("keytrack", "", 0, 1, 1, PS::linear, false)), added(scalar("modInDepth", "", 0, 1, 0))}},
    {ModuleType::noise, "noise", true, 3, true, false, AudioPort::in, {choice("color", {"white", "pink"}), choice("mode", {"continuous", "burst"}), scalar("burstMs", "ms", 1, 500, 80, PS::logarithmic), scalar("outputLevel", "", 0, 1, 0.3)}},
    {ModuleType::resonator, "resonator", false, 2, false, false, AudioPort::exciteIn, {choice("mode", {"comb", "modal"}), scalar("tuneRatio", "ratio", 0.5, 4, 1, PS::logarithmic), scalar("combFeedback", "", 0, 0.97, 0.4), scalar("modalQ", "", 0.5, 12, 3), array("modeRatios", 0.5, 4, 1, 4), array("modeLevels", 0, 1, 0, 4), scalar("outputLevel", "", 0, 1, 0.8),
        added(scalar("exciteDepth", "", 0, 1, 0))}},
    {ModuleType::filter, "filter", false, 2, false, false, AudioPort::in, {choice("mode", {"lowpass", "bandpass", "highpass", "ladder24", "notch"}), scalar("cutoff", "Hz", 30, 18000, 1000, PS::logarithmic), scalar("q", "", 0.5, 8, 0.707), scalar("outputLevel", "", 0, 1, 1),
        added(scalar("drive", "", 1, 16, 1)), added(scalar("keytrack", "", -1, 1, 0)), added(scalar("envAmount", "", -1, 1, 0))}},
    {ModuleType::shaper, "shaper", false, 2, false, false, AudioPort::in, {scalar("drive", "", 1, 16, 1), scalar("wet", "", 0, 1, 1), scalar("outputLevel", "", 0, 1, 1),
        added(choice("curve", {"tanh", "hardClip", "fold", "sine", "asymmetric"}))}},
    {ModuleType::mixer, "mixer", false, 2, false, false, AudioPort::in, {scalar("level", "", 0, 1, 1), scalar("pan", "", -1, 1, 0), scalar("outputLevel", "", 0, 1, 1)}},
    {ModuleType::sub, "sub", true, 1, false, false, AudioPort::in, {added(choice("waveform", {"sine", "triangle"})), added(step("octave", "oct", -2, -1, -1)), added(scalar("drift", "", 0, 1, 0)), added(scalar("fine", "cents", -100, 100, 0)), added(scalar("keytrack", "", 0, 1, 1, PS::linear, false)), added(scalar("outputLevel", "", 0, 1, 0.5))}},
    {ModuleType::chorus, "chorus", false, 1, false, true, AudioPort::in, {added(scalar("rate", "Hz", 0.01, 8, 0.5, PS::logarithmic)), added(scalar("depth", "", 0, 1, 0.3)), added(step("voices", "count", 2, 4, 2)), added(scalar("feedback", "", 0, 0.9, 0)), added(scalar("mix", "", 0, 1, 0.3)), added(scalar("outputLevel", "", 0, 1, 1))}},
    {ModuleType::delay, "delay", false, 1, false, true, AudioPort::in, {added(choice("syncMode", {"free", "sync"})), added(scalar("timeMs", "ms", 1, 2000, 375, PS::logarithmic)), added(choice("syncDivision", {"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16"}, 4)), added(scalar("spread", "", -1, 1, 0)), added(scalar("feedback", "", 0, 0.95, 0.35)), added(scalar("damping", "", 0, 1, 0.4)), added(scalar("mix", "", 0, 1, 0.3)), added(scalar("outputLevel", "", 0, 1, 1))}},
    {ModuleType::reverb, "reverb", false, 1, false, true, AudioPort::in, {added(scalar("size", "", 0, 1, 0.5)), added(scalar("decaySeconds", "s", 0.1, 20, 2.0, PS::logarithmic)), added(scalar("damping", "", 0, 1, 0.5)), added(scalar("preDelayMs", "ms", 0, 200, 20)), added(scalar("width", "", 0, 1, 1)), added(scalar("mix", "", 0, 1, 0.25)), added(scalar("outputLevel", "", 0, 1, 1))}},
    {ModuleType::width, "width", false, 1, false, true, AudioPort::in, {added(scalar("width", "", 0, 2, 1)), added(scalar("bassMonoHz", "Hz", 20, 500, 120, PS::logarithmic)), added(scalar("outputLevel", "", 0, 1, 1))}}
}};

std::string_view typeId(ModuleType type) { return catalog.at(static_cast<std::size_t>(type)).id; }
std::optional<ModulationSource> sourceFrom(std::string_view s)
{
    constexpr std::array names{"e1", "e2", "e3", "l1", "l2", "velocity", "keyTracking", "pitchBend", "cc1", "macro1", "macro2", "macro3", "macro4"};
    for (std::size_t i = 0; i < names.size(); ++i) if (s == names[i]) return static_cast<ModulationSource>(i);
    return {};
}
std::string_view sourceId(ModulationSource s)
{
    constexpr std::array names{"e1", "e2", "e3", "l1", "l2", "velocity", "keyTracking", "pitchBend", "cc1", "macro1", "macro2", "macro3", "macro4"};
    return names.at(static_cast<std::size_t>(s));
}

const juce::DynamicObject* object(const juce::var& v) { return v.getDynamicObject(); }
const juce::Array<juce::var>* arrayValue(const juce::var& v) { return v.getArray(); }
bool exactKeys(const juce::DynamicObject& o, std::initializer_list<const char*> required, std::initializer_list<const char*> optional = {})
{
    std::set<std::string> allowed;
    for (auto* k : required) { allowed.emplace(k); if (!o.hasProperty(k)) return false; }
    for (auto* k : optional) allowed.emplace(k);
    for (const auto& p : o.getProperties()) if (!allowed.contains(p.name.toString().toStdString())) return false;
    return true;
}
bool number(const juce::var& v, double& out)
{
    if (!(v.isInt() || v.isInt64() || v.isDouble())) return false;
    out = static_cast<double>(v); return std::isfinite(out);
}
bool integer(const juce::var& v, std::int64_t& out)
{
    if (!(v.isInt() || v.isInt64())) return false;
    out = static_cast<std::int64_t>(static_cast<juce::int64>(v)); return true;
}
juce::var obj() { return juce::var(new juce::DynamicObject()); }
void put(juce::var& v, const char* key, juce::var value) { v.getDynamicObject()->setProperty(key, std::move(value)); }
void put(juce::var& v, const char* key, const std::string& value) { put(v, key, juce::String::fromUTF8(value.c_str())); }
void put(juce::var& v, const char* key, std::string_view value) { put(v, key, juce::String::fromUTF8(value.data(), static_cast<int>(value.size()))); }
juce::var doubles(const std::vector<double>& values) { juce::Array<juce::var> a; for (auto v : values) a.add(v); return a; }

DecodeResult fail(std::string e) { return {{}, std::move(e)}; }
StateDecodeResult stateFail(std::string e) { return {{}, std::move(e)}; }

std::string validateJsonInput(std::string_view input)
{
    if (input.size() > maximumDocumentBytes) return "document exceeds 1 MiB";
    if (!juce::CharPointer_UTF8::isValidString(input.data(), static_cast<int>(input.size()))) return "document is not valid UTF-8";
    std::size_t depth = 0, containers = 0, stringBytes = 0;
    bool quoted = false, escaped = false;
    for (const unsigned char c : input)
    {
        if (quoted)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') { quoted = false; stringBytes = 0; }
            else if (++stringBytes > 65536) return "JSON string exceeds bound";
            continue;
        }
        if (c == '"') { quoted = true; stringBytes = 0; }
        else if (c == '{' || c == '[')
        {
            if (++depth > maximumJsonDepth) return "JSON nesting exceeds bound";
            if (++containers > 4096) return "JSON container count exceeds bound";
        }
        else if ((c == '}' || c == ']') && depth > 0) --depth;
    }
    if (quoted) return "unterminated JSON string";
    return {};
}
}

double ParameterDescriptor::normalize(double value) const noexcept
{
    value = std::clamp(value, minimum, maximum);
    if (scale == ParameterScale::logarithmic) return std::log(value / minimum) / std::log(maximum / minimum);
    return (value - minimum) / (maximum - minimum);
}
double ParameterDescriptor::denormalize(double value) const noexcept
{
    value = std::clamp(value, 0.0, 1.0);
    if (scale == ParameterScale::logarithmic) return minimum * std::pow(maximum / minimum, value);
    return minimum + value * (maximum - minimum);
}
const std::array<ModuleDescriptor, moduleTypeCount>& moduleCatalog() { return catalog; }
std::string_view audioPortId(AudioPort port) noexcept { return port == AudioPort::modIn ? "modIn" : port == AudioPort::exciteIn ? "exciteIn" : "in"; }
bool declaresPort(const ModuleDescriptor& m, AudioPort port) noexcept { return port == AudioPort::in ? !m.source : m.audioRateInput == port; }
const ModuleDescriptor* findModule(std::string_view id) noexcept { auto i = std::ranges::find(catalog, id, &ModuleDescriptor::id); return i == catalog.end() ? nullptr : &*i; }
const ParameterDescriptor* findParameter(const ModuleDescriptor& m, std::string_view id) noexcept { auto i = std::ranges::find(m.parameters, id, &ParameterDescriptor::id); return i == m.parameters.end() ? nullptr : &*i; }

bool applyHarmonicSpectrum(Node& node, double tilt, double inharmonicity) noexcept
{
    if (node.type != ModuleType::harmonic || !std::isfinite(tilt) || !std::isfinite(inharmonicity)) return false;
    tilt=std::clamp(tilt,-2.0,2.0);inharmonicity=std::clamp(inharmonicity,0.0,0.02);
    auto find=[&](std::string_view id)->ParameterValue*{for(auto& value:node.parameters)if(value.id==id)return &value;return nullptr;};
    auto* amplitudes=find("partialAmplitudes");auto* ratios=find("partialRatios");auto* tiltValue=find("tilt");auto* inharmonicityValue=find("inharmonicity");
    if(!amplitudes||!ratios||!tiltValue||!inharmonicityValue||amplitudes->values.size()!=16||ratios->values.size()!=16)return false;
    for(std::size_t i=0;i<16;++i){const auto harmonic=static_cast<double>(i+1);amplitudes->values[i]=std::clamp(std::pow(harmonic,tilt-1.0),0.0,1.0);ratios->values[i]=std::clamp(harmonic*std::sqrt(1.0+inharmonicity*harmonic*harmonic),0.5,32.0);}
    tiltValue->values={tilt};inharmonicityValue->values={inharmonicity};return true;
}

std::string validate(const Patch& p)
{
    if (p.nodes.size() > maximumNodes || p.edges.size() > maximumEdges || p.matrix.size() > maximumMatrixRows) return "patch exceeds structural cap";
    std::unordered_map<std::string, const Node*> nodes;
    std::array<std::size_t, moduleTypeCount> counts{}; std::size_t sources = 0;
    for (const auto& n : p.nodes)
    {
        if (static_cast<std::size_t>(n.type) >= catalog.size()) return "unknown module type";
        if (n.id.empty() || n.id == "output" || !nodes.emplace(n.id, &n).second) return "invalid or duplicate node id";
        const auto& d = catalog.at(static_cast<std::size_t>(n.type));
        if (++counts[static_cast<std::size_t>(n.type)] > d.typeCap || (d.sharedSourceSlot && ++sources > generalSourceSlots)) return "module type cap exceeded";
        if (n.parameters.size() != d.parameters.size()) return "node parameters are not complete";
        std::set<std::string> seen;
        for (const auto& pv : n.parameters)
        {
            const auto* pd = findParameter(d, pv.id);
            if (pd == nullptr || !seen.emplace(pv.id).second) return "unknown or duplicate parameter";
            const auto expected = pd->arraySize == 0 ? 1U : pd->arraySize;
            if (pv.values.size() != expected) return "parameter has wrong arity";
            for (double v : pv.values) if (!std::isfinite(v) || v < pd->minimum || v > pd->maximum || (pd->kind == ParameterKind::discrete && std::floor(v) != v)) return "parameter outside descriptor";
        }
    }
    std::set<std::tuple<std::string, std::string, AudioPort>> pairs;
    std::unordered_map<std::string, std::vector<std::string>> next;
    for (const auto& e : p.edges)
    {
        if (!std::isfinite(e.gain) || e.gain < 0 || e.gain > 1 || !nodes.contains(e.source) || (e.destination != "output" && !nodes.contains(e.destination))) return "invalid audio edge";
        if (e.source == e.destination || !pairs.emplace(e.source, e.destination, e.port).second) return "self or duplicate audio edge";
        // The OUT bus has only the ordinary summed input; every other target must declare the port.
        if (e.destination == "output") { if (e.port != AudioPort::in) return "audio edge targets an undeclared port"; }
        else
        {
            const auto& destination = catalog.at(static_cast<std::size_t>(nodes.at(e.destination)->type));
            if (e.port == AudioPort::in && destination.source) return "audio edge enters source";
            if (!declaresPort(destination, e.port)) return "audio edge targets an undeclared port";
        }
        // Effects region router constraint: the global tail may only feed itself or the OUT bus.
        if (catalog.at(static_cast<std::size_t>(nodes.at(e.source)->type)).effects && e.destination != "output"
            && !catalog.at(static_cast<std::size_t>(nodes.at(e.destination)->type)).effects) return "effects tail edge enters a per-voice node";
        if (e.destination != "output") next[e.source].push_back(e.destination);
    }
    std::unordered_map<std::string, int> color;
    std::function<bool(const std::string&)> cycle = [&](const std::string& id) { color[id] = 1; for (const auto& n : next[id]) { if (color[n] == 1 || (color[n] == 0 && cycle(n))) return true; } color[id] = 2; return false; };
    for (const auto& [id, _] : nodes) if (color[id] == 0 && cycle(id)) return "audio graph contains cycle";
    std::set<std::string> rowIds;
    for (const auto& row : p.matrix)
    {
        if (static_cast<std::size_t>(row.source) > static_cast<std::size_t>(ModulationSource::macro4) || row.id.empty() || !rowIds.emplace(row.id).second || !std::isfinite(row.depth) || row.depth < -1 || row.depth > 1 || !nodes.contains(row.destinationNode)) return "invalid matrix row";
        const auto& nd = catalog.at(static_cast<std::size_t>(nodes.at(row.destinationNode)->type));
        const auto* pd = findParameter(nd, row.destinationParameter);
        if (pd == nullptr || !pd->modulatable) return "illegal matrix destination";
    }
    for (const auto& e : p.envelopes) if (!std::isfinite(e.attack) || e.attack < .001 || e.attack > 2 || !std::isfinite(e.decay) || e.decay < .01 || e.decay > 4 || !std::isfinite(e.sustain) || e.sustain < 0 || e.sustain > 1 || !std::isfinite(e.release) || e.release < .02 || e.release > 6) return "invalid envelope";
    for (const auto& l : p.lfos) if (static_cast<std::size_t>(l.waveform) > static_cast<std::size_t>(LfoWaveform::triangle) || !std::isfinite(l.rate) || l.rate < .05 || l.rate > 12) return "invalid lfo";
    for (const auto& m : p.macros) if (!std::isfinite(m.defaultValue) || m.defaultValue < 0 || m.defaultValue > 1 || m.label.size() > 64) return "invalid macro";
    return {};
}

juce::var encodePatchValue(const Patch& p)
{
    auto root = obj(); put(root, "patchVersion", patchVersion); put(root, "noiseSeed", static_cast<juce::int64>(p.noiseSeed));
    juce::Array<juce::var> nodes;
    for (const auto& n : p.nodes) { auto v=obj(); put(v,"id",n.id); put(v,"type",std::string(typeId(n.type))); auto ps=obj(); for(const auto& x:n.parameters) put(ps,x.id.c_str(),x.values.size()==1?juce::var(x.values[0]):doubles(x.values)); put(v,"parameters",ps); nodes.add(v); } put(root,"nodes",nodes);
    juce::Array<juce::var> edges; for(const auto& e:p.edges){auto v=obj();put(v,"source",e.source);put(v,"destination",e.destination);put(v,"gain",e.gain);if(e.port!=AudioPort::in)put(v,"port",audioPortId(e.port));edges.add(v);} put(root,"edges",edges);
    juce::Array<juce::var> envs; for(const auto& e:p.envelopes){auto v=obj();put(v,"attack",e.attack);put(v,"decay",e.decay);put(v,"sustain",e.sustain);put(v,"release",e.release);envs.add(v);} put(root,"envelopes",envs);
    juce::Array<juce::var> lfos; for(const auto& l:p.lfos){auto v=obj();put(v,"rate",l.rate);put(v,"waveform",std::string_view(l.waveform==LfoWaveform::sine?"sine":"triangle"));lfos.add(v);} put(root,"lfos",lfos);
    juce::Array<juce::var> rows; for(const auto& r:p.matrix){auto v=obj();put(v,"id",r.id);put(v,"enabled",r.enabled);put(v,"source",std::string(sourceId(r.source)));put(v,"destinationNode",r.destinationNode);put(v,"destinationParameter",r.destinationParameter);put(v,"depth",r.depth);rows.add(v);} put(root,"matrix",rows);
    juce::Array<juce::var> macros; for(const auto& m:p.macros){auto v=obj();put(v,"label",m.label);put(v,"default",m.defaultValue);macros.add(v);} put(root,"macros",macros);
    return root;
}
std::string encodePatchJson(const Patch& p, bool pretty) { return juce::JSON::toString(encodePatchValue(p), pretty).toStdString(); }

DecodeResult decodePatchValue(const juce::var& root)
{
    const auto* o=object(root); if(!o || !exactKeys(*o,{"patchVersion","noiseSeed","nodes","edges","envelopes","lfos","matrix","macros"})) return fail("invalid patch object fields");
    std::int64_t version=0, seed=0; if(!integer(o->getProperty("patchVersion"),version)||version!=patchVersion||!integer(o->getProperty("noiseSeed"),seed)||seed<0||seed>UINT32_MAX) return fail("unsupported version or invalid seed");
    Patch p; p.noiseSeed=static_cast<std::uint32_t>(seed);
    const auto* ns=arrayValue(o->getProperty("nodes")); if(!ns || static_cast<std::size_t>(ns->size()) > maximumNodes) return fail("nodes must be bounded array");
    for(const auto& nv:*ns){const auto* no=object(nv);if(!no||!exactKeys(*no,{"id","type","parameters"})||!no->getProperty("id").isString()||!no->getProperty("type").isString())return fail("invalid node");const auto* md=findModule(no->getProperty("type").toString().toStdString());const auto* po=object(no->getProperty("parameters"));if(!md||!po)return fail("invalid node type or parameters");Node n{no->getProperty("id").toString().toStdString(),md->type,{}};for(const auto& pd:md->parameters){ParameterValue pv{std::string(pd.id),{}};if(!po->hasProperty(pd.id.data())){if(!pd.postV1)return fail("missing parameter");pv.values.assign(pd.arraySize==0?1:pd.arraySize,pd.defaultValue);n.parameters.push_back(std::move(pv));continue;}auto value=po->getProperty(pd.id.data());if(pd.arraySize){const auto* a=arrayValue(value);if(!a)return fail("coefficient must be array");for(const auto& x:*a){double d;if(!number(x,d))return fail("nonfinite coefficient");pv.values.push_back(d);}}else{double d;if(!number(value,d))return fail("parameter must be finite number");pv.values.push_back(d);}n.parameters.push_back(std::move(pv));}for(const auto& property:po->getProperties())if(findParameter(*md,property.name.toString().toStdString())==nullptr)return fail("unknown parameter");p.nodes.push_back(std::move(n));}
    const auto* es=arrayValue(o->getProperty("edges"));if(!es||static_cast<std::size_t>(es->size())>maximumEdges)return fail("edges must be bounded array");for(const auto& ev:*es){const auto* eo=object(ev);double gain;if(!eo||!exactKeys(*eo,{"source","destination","gain"},{"port"})||!eo->getProperty("source").isString()||!eo->getProperty("destination").isString()||!number(eo->getProperty("gain"),gain))return fail("invalid edge");auto port=AudioPort::in;if(eo->hasProperty("port")){const auto name=eo->getProperty("port").toString();if(name=="in")port=AudioPort::in;else if(name=="modIn")port=AudioPort::modIn;else if(name=="exciteIn")port=AudioPort::exciteIn;else return fail("unknown audio port");}p.edges.push_back({eo->getProperty("source").toString().toStdString(),eo->getProperty("destination").toString().toStdString(),gain,port});}
    const auto* envs=arrayValue(o->getProperty("envelopes"));if(!envs||(envs->size()!=static_cast<int>(envelopeCount)&&envs->size()!=static_cast<int>(envelopeCount)-1))return fail("three or four envelopes required");for(int i=0;i<envs->size();++i){const auto* x=object((*envs)[i]);auto& e=p.envelopes[i];if(!x||!exactKeys(*x,{"attack","decay","sustain","release"})||!number(x->getProperty("attack"),e.attack)||!number(x->getProperty("decay"),e.decay)||!number(x->getProperty("sustain"),e.sustain)||!number(x->getProperty("release"),e.release))return fail("invalid envelope");}
    const auto* ls=arrayValue(o->getProperty("lfos"));if(!ls||ls->size()!=2)return fail("two lfos required");for(int i=0;i<2;++i){const auto* x=object((*ls)[i]);auto& l=p.lfos[i];if(!x||!exactKeys(*x,{"rate","waveform"})||!number(x->getProperty("rate"),l.rate)||!x->getProperty("waveform").isString())return fail("invalid lfo");auto w=x->getProperty("waveform").toString();if(w=="sine")l.waveform=LfoWaveform::sine;else if(w=="triangle")l.waveform=LfoWaveform::triangle;else return fail("unknown lfo waveform");}
    const auto* rs=arrayValue(o->getProperty("matrix"));if(!rs||static_cast<std::size_t>(rs->size())>maximumMatrixRows)return fail("matrix must be bounded array");for(const auto& rv:*rs){const auto* ro=object(rv);double depth;if(!ro||!exactKeys(*ro,{"id","enabled","source","destinationNode","destinationParameter","depth"})||!ro->getProperty("id").isString()||!ro->getProperty("enabled").isBool()||!ro->getProperty("source").isString()||!ro->getProperty("destinationNode").isString()||!ro->getProperty("destinationParameter").isString()||!number(ro->getProperty("depth"),depth))return fail("invalid matrix row");auto src=sourceFrom(ro->getProperty("source").toString().toStdString());if(!src)return fail("unknown modulation source");p.matrix.push_back({ro->getProperty("id").toString().toStdString(),static_cast<bool>(ro->getProperty("enabled")),*src,ro->getProperty("destinationNode").toString().toStdString(),ro->getProperty("destinationParameter").toString().toStdString(),depth});}
    const auto* ms=arrayValue(o->getProperty("macros"));if(!ms||ms->size()!=4)return fail("four macros required");for(int i=0;i<4;++i){const auto* x=object((*ms)[i]);double d;if(!x||!exactKeys(*x,{"label","default"})||!x->getProperty("label").isString()||!number(x->getProperty("default"),d))return fail("invalid macro");p.macros[i]={x->getProperty("label").toString().toStdString(),d};}
    if(auto e=validate(p);!e.empty())return fail(std::move(e)); return {std::move(p),{}};
}
DecodeResult decodePatchJson(std::string_view json) { if(auto e=validateJsonInput(json);!e.empty())return fail(std::move(e));auto v=juce::JSON::parse(juce::String::fromUTF8(json.data(),static_cast<int>(json.size())));if(v.isVoid())return fail("malformed JSON");return decodePatchValue(v); }

juce::var encodeStateValue(const State& s)
{
    auto root=obj();put(root,"stateVersion",stateVersion);put(root,"basePatch",encodePatchValue(s.basePatch));put(root,"editedPatch",encodePatchValue(s.editedPatch));auto c=obj();juce::Array<juce::var> m;for(auto v:s.controls.macros)m.add(v);put(c,"macros",m);put(c,"outputGain",s.controls.outputGain);put(c,"width",s.controls.width);put(c,"masterTune",s.controls.masterTune);put(c,"bypass",s.controls.bypass);put(root,"controls",c);if(s.provenance)put(root,"provenance",*s.provenance);return root;
}
std::string encodeStateJson(const State& s,bool pretty){return juce::JSON::toString(encodeStateValue(s),pretty).toStdString();}
StateDecodeResult decodeStateValue(const juce::var& root)
{
    const auto* o=object(root);if(!o||!exactKeys(*o,{"stateVersion","basePatch","editedPatch","controls"},{"provenance"}))return stateFail("invalid state object fields");std::int64_t v;if(!integer(o->getProperty("stateVersion"),v)||v!=stateVersion)return stateFail("unsupported state version");auto b=decodePatchValue(o->getProperty("basePatch"));if(!b)return stateFail("invalid base patch: "+b.error);auto e=decodePatchValue(o->getProperty("editedPatch"));if(!e)return stateFail("invalid edited patch: "+e.error);const auto* c=object(o->getProperty("controls"));if(!c||!exactKeys(*c,{"macros","outputGain","width","masterTune","bypass"})||!c->getProperty("bypass").isBool())return stateFail("invalid controls");const auto* ma=arrayValue(c->getProperty("macros"));if(!ma||ma->size()!=4)return stateFail("four controls macros required");State s{std::move(*b.value),std::move(*e.value)};for(int i=0;i<4;++i)if(!number((*ma)[i],s.controls.macros[i])||s.controls.macros[i]<0||s.controls.macros[i]>1)return stateFail("invalid macro control");if(!number(c->getProperty("outputGain"),s.controls.outputGain)||s.controls.outputGain < -60||s.controls.outputGain>6||!number(c->getProperty("width"),s.controls.width)||s.controls.width<0||s.controls.width>1||!number(c->getProperty("masterTune"),s.controls.masterTune)||s.controls.masterTune < -12||s.controls.masterTune>12)return stateFail("invalid global control");s.controls.bypass=static_cast<bool>(c->getProperty("bypass"));if(o->hasProperty("provenance"))s.provenance=o->getProperty("provenance");return {std::move(s),{}};
}
StateDecodeResult decodeStateJson(std::string_view json){if(auto e=validateJsonInput(json);!e.empty())return stateFail(std::move(e));auto v=juce::JSON::parse(juce::String::fromUTF8(json.data(),static_cast<int>(json.size())));if(v.isVoid())return stateFail("malformed JSON");return decodeStateValue(v);}
}
