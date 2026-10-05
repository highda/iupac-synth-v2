#include "iupac/chemistry/Mapping.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <numeric>
#include <string>
#include <vector>

// Mapper version 3 (D13, #168). Three stages, each inspectable through `iupac-cli inspect`:
//   1. profile()  — structural facts read off the Analysis graph (Profile.cpp);
//   2. project()  — perceptual traits in [0, 1], rescaled against the discovery snapshot;
//   3. generate() — a scored competition between source *voices*, then continuous rules for the
//                   envelope, filter, unison, layers, effects and modulation.
// Nothing here hashes identity, names a molecule or branches on a fixture. Every assignment is
// traced with its rule id, the traits that drove it, and its value before and after clamping.
namespace iupac::chemistry
{
namespace
{
using domain::ModuleType;
using Drivers = std::initializer_list<const char*>;

double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }
// Saturating count: 0 at 0, 0.5 at `half`, approaching 1.
double sat(double x, double half) { return x <= 0.0 ? 0.0 : x / (x + half); }
double mix(double a, double b, double t) { return a + (b - a) * t; }
// Exponential interpolation, for times and frequencies.
double emix(double a, double b, double t) { return a * std::pow(b / a, clamp01(t)); }
// Piecewise-linear rescale through the snapshot's quartiles: `k` are the values at 0, 0.25, 0.5,
// 0.75 and 1 (the ends are a little inside the observed extremes so outliers clamp).
double quantile(double x, const std::array<double, 5>& k)
{
    if (x <= k[0]) return 0.0;
    for (std::size_t i = 1; i < k.size(); ++i)
        if (x <= k[i]) return (static_cast<double>(i - 1) + (k[i] > k[i - 1] ? (x - k[i - 1]) / (k[i] - k[i - 1]) : 1.0)) * 0.25;
    return 1.0;
}

// Every value the generator publishes is rounded to six decimals. The rules use pow/exp/log, whose
// last bit differs between C libraries; rounding is what keeps the generated Patch and its trace
// byte-identical on Linux and macOS (V1 cross-platform parity) rather than equal to within an ulp.
double settle(double x) { return std::isfinite(x) ? std::round(x * 1e6) / 1e6 : x; }

juce::var object() { return juce::var(new juce::DynamicObject); }
void put(juce::var& v, const char* k, juce::var x) { v.getDynamicObject()->setProperty(k, std::move(x)); }

// --- voices ---------------------------------------------------------------------------------------
// A voice is one way a source can sound. The primary and the optional secondary source are the
// two best-scoring voices of different kinds; scores are sums of the traits a voice stands for.
enum Voice : std::size_t
{
    saw, square, pulse, triangle, sine,                                              // classic oscillator
    wtVoice, wtOrgan, wtStrings, wtWind, wtKeys, wtPlucked, wtChip, wtEdge, wtGrain, wtComplex, wtReed, // wavetable families
    fmHarmonic, fmMetal, additive,
    voiceCount
};
constexpr std::array<const char*, voiceCount> voiceNames{"saw", "square", "pulse", "triangle", "sine",
    "wt-voice", "wt-organ", "wt-strings", "wt-wind", "wt-keys", "wt-plucked", "wt-chip", "wt-edge", "wt-grain", "wt-complex", "wt-reed",
    "fm-harmonic", "fm-metal", "additive"};
ModuleType voiceModule(Voice v) { return v <= sine ? ModuleType::osc : v <= wtReed ? ModuleType::wavetable : v <= fmMetal ? ModuleType::fm : ModuleType::harmonic; }

std::array<double, voiceCount> voiceScores(const Traits& t)
{
    std::array<double, voiceCount> s{};
    const double openChain = (1.0 - t.cyclic);
    // Classic shapes are the defaults of the carbon skeleton itself.
    s[saw]      = 0.34 + 1.10 * t.unsaturated + 0.55 * t.conjugated * (1.0 - t.aromatic) + 0.45 * t.chain + 0.25 * openChain * t.lipophilic;
    s[square]   = 0.10 + 1.50 * t.aromatic * (1.0 - t.heteroaromatic) + 0.25 * t.symmetric * t.cyclic;
    s[pulse]    = 0.05 + 1.70 * t.aromatic * t.heteroaromatic + 0.60 * t.heteroaromatic;
    s[triangle] = 0.22 + 1.15 * t.alicyclic + 0.35 * t.saturated * (1.0 - t.polar);
    s[sine]     = 0.05 + (1.05 * (1.0 - t.size) * (1.0 - t.size) + 0.30 * t.symmetric * (1.0 - t.size)) * (1.0 - t.metallic);
    // Wavetable families answer to functional groups and to overall build.
    s[wtVoice]   = 1.70 * t.amide + 0.25 * t.donor * t.amide;
    s[wtOrgan]   = 1.35 * t.fused + 0.30 * t.fused * t.saturated;
    s[wtStrings] = 0.95 * t.flexible * t.size + 0.55 * t.chain * t.size + 0.25 * t.lipophilic * t.flexible;
    s[wtWind]    = 1.25 * t.hydroxyl + 0.75 * t.ether + 0.45 * t.phenolic;
    s[wtKeys]    = 1.20 * t.ester + 1.25 * t.carboxyl + 0.25 * t.carbonyl;
    s[wtPlucked] = 1.25 * t.amine + 0.25 * t.branched * t.amine;
    s[wtChip]    = 1.90 * t.halogenated * (t.heavyHalogen <= 0.5 ? 1.0 : 0.3) * (1.0 - t.metallic) + 0.25 * t.halogenated * (1.0 - t.size);
    s[wtEdge]    = 1.60 * t.nitro + 1.50 * t.nitrile + 0.95 * t.charged + 0.90 * t.strained;
    s[wtGrain]   = 1.20 * t.salt * (1.0 - 0.6 * t.inorganic) + 0.30 * t.salt * t.charged;
    s[wtComplex] = 0.85 * t.diverse + 0.45 * (1.0 - t.symmetric) * t.size + 0.90 * t.isotopic;
    s[wtReed]    = 1.50 * t.thio + 0.35 * t.sulfur;
    // FM: sulfur and phosphorus oxo groups ring like a struck tine; metals and the heavy halogens
    // are the only inharmonic, bell-like voice — a metal sounds like metal, and nothing else does.
    s[fmHarmonic] = 1.55 * t.sulfonyl + 1.55 * t.phosphorus + 0.30 * t.sulfur * t.polar;
    s[fmMetal]    = 2.30 * t.metallic + 0.60 * t.metallic * t.inorganic + 1.15 * t.heavyHalogen * t.halogenated;
    s[additive]   = 0.24 + 1.00 * t.diverse * t.polar + 0.45 * t.polar * t.acceptor;
    return s;
}

// Amplitude-envelope archetypes: seconds for attack/decay/release, level for sustain, and the
// stage bends. The patch's envelope is a weighted blend of these, in the log domain for times.
struct Archetype { double attack, decay, sustain, release, attackCurve, decayCurve, releaseCurve; };
enum Shape : std::size_t { perc, pluck, keys, lead, organ, swell, pad, shapeCount };
constexpr std::array<const char*, shapeCount> shapeNames{"perc", "pluck", "keys", "lead", "organ", "swell", "pad"};
constexpr std::array<Archetype, shapeCount> archetypes{{
    {0.001, 0.16, 0.00, 0.14, -0.9, -0.9, -0.8},
    {0.002, 0.38, 0.06, 0.30, -0.8, -0.7, -0.5},
    {0.003, 1.20, 0.28, 0.45, -0.6, -0.6, -0.3},
    {0.008, 0.25, 0.85, 0.16, -0.2, -0.2,  0.0},
    {0.004, 0.06, 1.00, 0.07,  0.0,  0.0,  0.0},
    {0.140, 0.90, 0.85, 0.65,  0.4, -0.2,  0.3},
    {0.550, 1.60, 0.80, 1.80,  0.6, -0.3,  0.6},
}};
// How each voice likes to be played; added to the weights the molecule's build produces, so a
// piano table tends to decay like keys and a string table tends to swell, without forcing either.
constexpr std::array<std::array<double, shapeCount>, voiceCount> voiceShape{{
    /* saw       */ {0.0, 0.1, 0.0, 0.5, 0.0, 0.1, 0.1},
    /* square    */ {0.0, 0.0, 0.0, 0.4, 0.3, 0.0, 0.0},
    /* pulse     */ {0.0, 0.1, 0.0, 0.5, 0.0, 0.0, 0.1},
    /* triangle  */ {0.0, 0.3, 0.3, 0.1, 0.0, 0.0, 0.0},
    /* sine      */ {0.1, 0.4, 0.0, 0.2, 0.0, 0.0, 0.0},
    /* wtVoice   */ {0.0, 0.0, 0.0, 0.1, 0.0, 0.3, 0.8},
    /* wtOrgan   */ {0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0},
    /* wtStrings */ {0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.3},
    /* wtWind    */ {0.0, 0.0, 0.0, 0.5, 0.0, 0.6, 0.0},
    /* wtKeys    */ {0.0, 0.1, 1.2, 0.0, 0.0, 0.0, 0.0},
    /* wtPlucked */ {0.0, 1.2, 0.2, 0.0, 0.0, 0.0, 0.0},
    /* wtChip    */ {0.4, 0.1, 0.0, 0.5, 0.0, 0.0, 0.0},
    /* wtEdge    */ {0.1, 0.1, 0.0, 0.6, 0.0, 0.0, 0.0},
    /* wtGrain   */ {0.0, 0.0, 0.0, 0.0, 0.0, 0.3, 0.6},
    /* wtComplex */ {0.0, 0.0, 0.1, 0.2, 0.0, 0.2, 0.4},
    /* wtReed    */ {0.0, 0.0, 0.0, 0.6, 0.0, 0.3, 0.0},
    /* fmHarmonic*/ {0.0, 0.2, 1.0, 0.0, 0.0, 0.0, 0.0},
    /* fmMetal   */ {0.5, 0.2, 0.8, 0.0, 0.0, 0.0, 0.0},
    /* additive  */ {0.0, 0.0, 0.1, 0.2, 0.5, 0.0, 0.3},
}};

// Level trim per voice, so a square (whose RMS equals its peak) and a peak-normalized wavetable
// frame land at a comparable loudness before the patch's own level rules apply.
constexpr std::array<double, voiceCount> voiceLevel{0.80, 0.58, 0.66, 1.00, 1.00,
    1.15, 1.10, 1.15, 1.15, 1.15, 1.15, 1.00, 0.95, 1.10, 1.10, 1.10,
    0.90, 0.70, 0.95};

// --- patch assembly with tracing ----------------------------------------------------------------
struct Builder
{
    domain::Patch patch;
    juce::Array<juce::var> items;

    void trace(const char* kind, const std::string& id, const char* rule, Drivers drivers, double pre, double value)
    {
        auto x = object(); put(x, "kind", kind); put(x, "id", juce::String(id)); put(x, "ruleId", rule);
        juce::Array<juce::var> names; for (const auto* d : drivers) names.add(juce::String(d));
        put(x, "drivers", names); put(x, "preClamp", settle(pre)); put(x, "value", settle(value)); items.add(x);
    }
    domain::Node node(std::string id, ModuleType type, const char* rule, Drivers drivers, double evidence)
    {
        domain::Node n{std::move(id), type, {}};
        for (const auto& p : domain::moduleCatalog().at(static_cast<std::size_t>(type)).parameters)
            n.parameters.push_back({std::string(p.id), std::vector<double>(p.arraySize == 0 ? 1 : p.arraySize, p.defaultValue)});
        trace("node", n.id, rule, drivers, evidence, 1.0);
        return n;
    }
    // Assigns one scalar, clamped to the catalog range (and rounded for a discrete control).
    double set(domain::Node& n, std::string_view id, double value, const char* rule, Drivers drivers)
    {
        const auto& descriptor = domain::moduleCatalog().at(static_cast<std::size_t>(n.type));
        const auto* p = domain::findParameter(descriptor, id);
        if (p == nullptr || !std::isfinite(value)) return 0.0;
        auto final = settle(std::clamp(value, p->minimum, p->maximum));
        if (p->kind == domain::ParameterKind::discrete) final = std::round(final);
        for (auto& stored : n.parameters) if (stored.id == id) stored.values[0] = final;
        trace("parameter", n.id + "." + std::string(id), rule, drivers, value, final);
        return final;
    }
    void edge(const std::string& from, const std::string& to, double gain, const char* rule, Drivers drivers, domain::AudioPort port = domain::AudioPort::in)
    {
        const auto g = settle(std::clamp(gain, 0.0, 1.0));
        patch.edges.push_back({from, to, g, port});
        trace("edge", from + "->" + to, rule, drivers, gain, g);
    }
    void row(std::string id, domain::ModulationSource source, const std::string& node, const char* parameter, double depth, const char* rule, Drivers drivers)
    {
        const auto d = settle(std::clamp(depth, -1.0, 1.0));
        patch.matrix.push_back({id, true, source, node, parameter, d});
        trace("matrix", id, rule, drivers, depth, d);
    }
};
}

// --- stage 2: traits --------------------------------------------------------------------------------
SonicIntent project(const Analysis& a)
{
    SonicIntent s; s.profile = profile(a);
    // Settled before anything reads them, and again after the traits are formed, so both platforms
    // run the rules below from identical inputs whatever their compilers fused into one operation.
#define X(name) s.profile.name = settle(s.profile.name);
    IUPAC_PROFILE_FIELDS(X)
#undef X
    const auto& p = s.profile; auto& t = s.traits;
    const double heavy = std::max(1.0, p.heavyAtoms);
    // Quartile knots measured on the ranked discovery snapshot (4,957 molecules, mapper v3 calibration).
    t.size = quantile(p.heavyAtoms, {1.0, 10.0, 17.0, 26.0, 60.0});
    t.weight = quantile(p.massPerAtom, {12.0, 13.75, 14.24, 16.2, 40.0});
    t.aromatic = clamp01(p.aromaticFraction); t.saturated = clamp01(p.sp3Fraction);
    t.unsaturated = sat(p.alkeneBonds + 2.0 * p.tripleBonds, 1.5);
    t.conjugated = clamp01(p.conjugatedFraction); t.cyclic = clamp01(p.ringAtomFraction);
    t.alicyclic = clamp01(p.ringAtomFraction - p.aromaticFraction);
    t.fused = clamp01(p.fusedPerRing); t.strained = sat(p.smallRingAtoms, 3.0);
    t.polar = quantile(p.polarSurfacePerAtom, {0.0, 1.51, 2.78, 4.85, 14.0});
    t.lipophilic = quantile(p.logP, {-4.0, 0.39, 2.15, 3.67, 8.0});
    t.flexible = quantile(p.flexibility, {0.0, 0.08, 0.16, 0.29, 0.70});
    t.linear = quantile(p.elongation, {0.30, 0.47, 0.55, 0.67, 1.0});
    t.branched = quantile(p.branchedFraction, {0.0, 0.18, 0.29, 0.36, 0.50});
    t.chain = sat(std::max(0.0, p.longestChain - 1.0), 4.0);
    t.symmetric = quantile(p.symmetry, {0.0, 0.04, 0.11, 0.40, 0.90});
    t.diverse = clamp01((p.elementKinds - 1.0) / 4.0);
    t.charged = sat(p.chargedAtoms, 2.0);
    // Opposite charges inside one molecule; a salt written as separate ions is not a zwitterion.
    t.zwitterionic = p.positiveAtoms > 0 && p.negativeAtoms > 0 && p.fragments <= 1.0 ? 1.0 : 0.0;
    t.halogenated = sat(p.halogenLoad, 4.0); t.heavyHalogen = p.heaviestHalogen / 4.0;
    // A metal counts by presence and by how much of the compound it is: a bare element or an oxide
    // is all metal, the sodium of a drug salt is a trace.
    t.metallic = clamp01(0.45 * sat(p.metalAtoms + 0.5 * p.metalloidAtoms, 1.0) + 1.6 * (p.metalAtoms + 0.5 * p.metalloidAtoms) / heavy);
    t.inorganic = a.elementCounts[0] == 0 ? 1.0 : 0.0; t.isotopic = sat(p.isotopeAtoms, 2.0);
    t.chiral = sat(p.stereocenters, 4.0); t.handed = std::clamp(p.chiralBalance, -1.0, 1.0); t.geometric = sat(p.geometricBonds, 2.0); t.cisTrans = std::clamp(p.geometricBalance, -1.0, 1.0);
    t.donor = quantile(p.donorsPerAtom, {0.0, 0.02, 0.04, 0.10, 0.30});
    t.acceptor = quantile(p.acceptorsPerAtom, {0.0, 0.10, 0.17, 0.26, 0.55});
    const double aromaticAtoms = p.aromaticFraction * heavy;
    t.heteroaromatic = aromaticAtoms > 0 ? clamp01(2.5 * p.heteroaromaticAtoms / aromaticAtoms) : 0.0;
    t.salt = p.fragments > 1 ? clamp01(0.4 + 1.2 * (1.0 - p.largestFragmentFraction)) : 0.0;
    // Functional groups: presence matters more than count, and a group in a small molecule matters
    // more than the same group lost in a large one, hence the per-atom term.
    const auto group = [&](double count) { return clamp01(0.6 * sat(count, 1.0) + 2.4 * count / heavy); };
    t.amide = group(p.amides); t.amine = group(p.amines); t.hydroxyl = group(p.alcohols + p.phenols); t.phenolic = group(p.phenols);
    t.ether = group(p.ethers); t.carboxyl = group(p.carboxyls); t.ester = group(p.esters); t.carbonyl = group(p.carbonyls);
    t.nitro = group(p.nitros); t.nitrile = group(p.nitriles); t.sulfonyl = group(p.sulfonyls); t.thio = group(p.thios);
    t.sulfur = group(p.sulfurAtoms); t.phosphorus = group(p.phosphorusAtoms + p.phosphates);
    // Spectral emphasis: polarity first, then unsaturation and halogens, darkened by lipophilicity.
    t.bright = clamp01(0.55 * t.polar + 0.20 * t.unsaturated + 0.15 * t.halogenated + 0.25 * (1.0 - t.lipophilic) - 0.05);
    // Where the functional detail sits: one number that moves when a group, a branch or a ring
    // changes position without any count changing. It is what tells constitutional isomers apart.
    t.placement = clamp01(0.30 * p.heteroCentrality + 0.25 * p.branchCentrality + 0.20 * p.nitrogenOxygenDistance
                          + 0.35 * p.branchNitrogenDistance + 0.30 * p.ringNitrogenDistance + 0.15 * p.ringCentrality);
#define X(name) t.name = settle(t.name);
    IUPAC_TRAIT_FIELDS(X)
#undef X
    return s;
}

juce::var encodeSonicIntent(const SonicIntent& s)
{
    auto v = object(); put(v, "sonicIntentVersion", sonicIntentVersion);
    auto traits = object();
#define X(name) put(traits, #name, s.traits.name);
    IUPAC_TRAIT_FIELDS(X)
#undef X
    put(v, "traits", traits); put(v, "profile", encodeProfile(s.profile));
    return v;
}

// --- stage 3: the patch -------------------------------------------------------------------------------
GenerationResult generate(const Analysis& a)
{
    if (a.heavyAtoms == 0) return {{}, {}, {}, "unsupported empty Analysis"};
    const auto intent = project(a); const auto& t = intent.traits; const auto& p = intent.profile;
    Builder b; b.patch.noiseSeed = 0x49555041;

    // ---- V1: voice competition -------------------------------------------------------------------
    const auto scores = voiceScores(t);
    std::array<std::size_t, voiceCount> order{}; std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return scores[x] > scores[y]; });
    const auto primaryVoice = static_cast<Voice>(order[0]);
    // The layer is the best voice of another module kind, or another oscillator shape; it is
    // present when the molecule has enough going on to deserve two timbres, and always for a salt,
    // whose second fragment is the second voice.
    const double complexity = clamp01(0.40 * (1.0 - t.symmetric) + 0.30 * t.size + 0.20 * t.diverse + 0.25 * t.salt + 0.10 * t.branched);
    const auto secondVoice = static_cast<Voice>(order[1]);
    const double secondRatio = scores[order[0]] > 0 ? scores[order[1]] / scores[order[0]] : 0.0;
    const bool layered = (complexity >= 0.56 && secondRatio >= 0.55) || t.salt > 0.0;
    b.trace("decision", "complexity", "V2-layer-voice", {"symmetric", "size", "diverse", "salt", "branched"}, complexity, layered ? 1.0 : 0.0);
    for (std::size_t v = 0; v < voiceCount; ++v) b.trace("score", voiceNames[v], "V1-voice-score", {}, scores[v], scores[v]);

    // ---- E1: envelope archetype weights ----------------------------------------------------------
    std::array<double, shapeCount> shape{};
    shape[perc]  = 0.9 * (1.0 - t.size) * (1.0 - t.size) * (1.0 - t.cyclic) + 0.35 * t.charged + 0.3 * t.strained;
    shape[pluck] = 0.9 * (1.0 - t.cyclic) * (1.0 - t.flexible) * (1.0 - 0.5 * t.size) + 0.3 * t.branched * (1.0 - t.cyclic)
                 + 0.75 * t.cyclic * (1.0 - t.flexible) * (1.0 - t.size);
    shape[keys]  = 0.5 * (t.ester + t.carboxyl) + 0.45 * (1.0 - t.cyclic) * t.size * (1.0 - t.flexible) + 0.55 * t.carbonyl * (1.0 - t.flexible);
    shape[lead]  = 0.9 * t.cyclic * (1.0 - t.flexible) * (1.0 - 0.5 * t.fused) * (0.35 + 0.65 * t.size);
    shape[organ] = 0.8 * t.fused + 0.5 * t.aromatic * t.symmetric;
    shape[swell] = 0.5 * t.flexible * (1.0 - t.size) + 0.5 * t.chain + 0.3 * t.donor * t.flexible;
    shape[pad]   = 0.9 * t.flexible * t.size + 0.45 * t.donor * t.size + 0.3 * t.lipophilic * t.flexible;
    // Cubing the combined weight lets the strongest archetype lead instead of averaging every
    // envelope toward the same medium attack and medium sustain.
    for (std::size_t i = 0; i < shapeCount; ++i) { const double w = shape[i] + voiceShape[primaryVoice][i] * 0.6; shape[i] = w * w * w; }
    const double shapeSum = std::max(1e-9, std::accumulate(shape.begin(), shape.end(), 0.0));
    for (auto& w : shape) w /= shapeSum;
    const auto blendLog = [&](double Archetype::* field) { double x = 0; for (std::size_t i = 0; i < shapeCount; ++i) x += shape[i] * std::log(archetypes[i].*field); return std::exp(x); };
    const auto blendLinear = [&](double Archetype::* field) { double x = 0; for (std::size_t i = 0; i < shapeCount; ++i) x += shape[i] * (archetypes[i].*field); return x; };
    for (std::size_t i = 0; i < shapeCount; ++i) b.trace("shape", shapeNames[i], "E1-envelope-shape", {"size", "cyclic", "flexible", "fused", "chain", "donor"}, shape[i], shape[i]);
    // How struck the sound is: it sets the filter sweep, the noise transient and the dryness.
    const double struck = clamp01(shape[perc] + shape[pluck] + 0.6 * shape[keys]);
    const double held = clamp01(shape[pad] + shape[swell] + 0.5 * shape[organ] + 0.4 * shape[lead]);
    // Placement stretches the decay: the same atoms arranged differently ring for a different time.
    const double ring = std::pow(2.0, 1.4 * (p.nitrogenOxygenDistance - 0.15) + 1.0 * (t.placement - 0.4) + 2.8 * (p.branchCentrality - 0.55));
    domain::Envelope e1{std::clamp(blendLog(&Archetype::attack), 0.001, 2.0), std::clamp(blendLog(&Archetype::decay) * ring, 0.01, 4.0),
                        clamp01(blendLinear(&Archetype::sustain)), std::clamp(blendLog(&Archetype::release) * emix(0.7, 1.9, 0.6 * t.size + 0.4 * t.lipophilic), 0.02, 6.0),
                        blendLinear(&Archetype::attackCurve), blendLinear(&Archetype::decayCurve), blendLinear(&Archetype::releaseCurve)};
    b.trace("envelope", "e1.attack", "E1-envelope", {"flexible", "size", "donor"}, e1.attack, e1.attack);
    b.trace("envelope", "e1.decay", "E1-envelope", {"cyclic", "placement"}, e1.decay, e1.decay);
    b.trace("envelope", "e1.sustain", "E1-envelope", {"cyclic", "fused", "flexible"}, e1.sustain, e1.sustain);
    b.trace("envelope", "e1.release", "E1-envelope", {"size", "lipophilic"}, e1.release, e1.release);

    // ---- S1/S2: sources ----------------------------------------------------------------------------
    // Unison thickness follows build: big, floppy, conjugated molecules stack; small rigid ones stay one voice.
    const double thickness = clamp01(0.45 * t.size + 0.30 * t.flexible + 0.25 * t.conjugated + 0.20 * held - 0.35 * struck);
    const double detune = 5.0 + 22.0 * t.flexible + 9.0 * (1.0 - t.symmetric);
    // Handedness and E/Z geometry lean the image opposite ways for the two forms of a stereo pair.
    const double spread = clamp01(0.30 + 0.45 * t.chiral + 0.30 * t.size + 0.10 * t.handed + 0.12 * t.cisTrans);
    const double drift = clamp01(0.04 + 0.30 * t.flexible * t.lipophilic + 0.15 * held);
    // Timbre position inside a wavetable, and the pulse width of a pulse: brightness first, moved by placement.
    // Branch and ring centrality carry most of the weight: they are what moves when the same
    // residues or substituents are attached in a different order.
    const double position = clamp01(0.12 + 0.60 * t.bright + 0.45 * (t.placement - 0.40) + 1.9 * (p.branchCentrality - 0.55) + 0.45 * (t.branched - 0.5) + 1.2 * (p.ringCentrality - 0.40) * (p.rings > 0 ? 1.0 : 0.0));
    // Isotopes are heavier, so they sound a touch lower; a rare accent, a few cents at most.
    const double isotopeCents = -14.0 * t.isotopic;
    const auto buildSource = [&](const std::string& id, Voice voice, bool isPrimary, const char* rule)
    {
        auto n = b.node(id, voiceModule(voice), rule, {}, scores[voice]);
        int maxCopies = 1;
        switch (voice)
        {
            case saw: b.set(n, "waveform", 2, "S1-osc-shape", {"unsaturated", "conjugated", "chain"}); maxCopies = 7; break;
            case square: b.set(n, "waveform", 3, "S1-osc-shape", {"aromatic"}); b.set(n, "pulseWidth", 0.5 - 0.10 * t.branched - 0.08 * (t.placement - 0.4), "S1-pulse-width", {"branched", "placement"}); maxCopies = 5; break;
            case pulse: b.set(n, "waveform", 3, "S1-osc-shape", {"aromatic", "heteroaromatic"}); b.set(n, "pulseWidth", 0.40 - 0.28 * t.heteroaromatic + 0.18 * (t.placement - 0.4), "S1-pulse-width", {"heteroaromatic", "placement"}); maxCopies = 5; break;
            case triangle: b.set(n, "waveform", 1, "S1-osc-shape", {"alicyclic", "saturated"}); maxCopies = 3; break;
            case sine: b.set(n, "waveform", 0, "S1-osc-shape", {"size", "symmetric"}); maxCopies = 2; break;
            case fmHarmonic:
                // Whole-number ratios only: a harmonic, tine-like FM tone. The ratio is the oxo count.
                b.set(n, "modulatorRatio", std::clamp(1.0 + std::floor(p.sulfonyls + p.phosphates + 0.5 * p.sulfurAtoms), 1.0, 5.0), "S1-fm-harmonic", {"sulfonyl", "phosphorus", "sulfur"});
                b.set(n, "index", 0.7 + 2.2 * t.polar + 0.8 * t.charged, "S1-fm-index", {"polar", "charged"}); maxCopies = 3; break;
            case fmMetal:
                // The one deliberately inharmonic voice: an irrational ratio that grows with atomic mass.
                b.set(n, "modulatorRatio", 1.41 + 2.2 * t.weight + 0.9 * t.placement, "S1-fm-metal", {"weight", "placement"});
                b.set(n, "index", 1.2 + 2.6 * t.metallic + 1.2 * t.heavyHalogen, "S1-fm-index", {"metallic", "heavyHalogen"}); maxCopies = 2; break;
            case additive:
            {
                // Sixteen integer partials with a brightness tilt; aromatic rings hollow out the even ones.
                (void) domain::applyHarmonicSpectrum(n, -1.5 + 1.3 * t.bright, 0.0);
                for (const auto* authored : {".tilt", ".partialAmplitudes", ".partialRatios"}) b.trace("parameter", id + authored, "S1-additive-spectrum", {"bright"}, -1.5 + 1.3 * t.bright, -1.5 + 1.3 * t.bright);
                // Fixed, not driven: the additive voice is always an exact harmonic series.
                b.set(n, "harmonicityMorph", 1.0, "S1-additive-harmonic", {});
                b.set(n, "oddEvenBalance", 0.7 * t.aromatic - 0.6 * t.saturated, "S1-additive-parity", {"aromatic", "saturated"});
                b.set(n, "symmetry", 0.35 + 0.4 * t.placement, "S1-additive-parity", {"placement"});
                std::vector<double> pans(16);
                for (std::size_t i = 0; i < pans.size(); ++i) pans[i] = std::clamp((i % 2 == 0 ? -1.0 : 1.0) * 0.6 * t.chiral * (static_cast<double>(i) / 15.0), -1.0, 1.0);
                for (auto& value : pans) value = settle(value);
                for (auto& stored : n.parameters) { if (stored.id == "partialPans") stored.values = pans; if (stored.id == "partialAmplitudes" || stored.id == "partialRatios" || stored.id == "tilt") for (auto& value : stored.values) value = settle(value); }
                b.trace("parameter", id + ".partialPans", "S1-additive-pan", {"chiral"}, t.chiral, pans.back());
                maxCopies = 2; break;
            }
            default:
            {
                int table = 0; const char* why = "S1-table";
                switch (voice)
                {
                    case wtVoice: table = t.amide >= 0.8 ? 3 : 21; break;                         // formant (polyamides) | voice
                    case wtOrgan: table = t.aromatic > 0.2 ? 16 : 4; break;                       // eorgan | organ
                    case wtStrings: table = t.size > 0.6 ? 8 : 9; break;                          // cello | violin
                    case wtWind: table = t.phenolic > 0.2 ? 11 : t.amine > 0.3 ? 13 : t.ether > t.hydroxyl ? 12 : 10; break; // clarinet | altosax | oboe | flute
                    case wtKeys: table = t.carboxyl >= t.ester ? (t.aromatic > 0.3 ? 31 : 14) : 15; break; // clavinet | piano | epiano
                    case wtPlucked: table = t.size > 0.55 && t.lipophilic > 0.6 ? (t.aromatic > 0.2 ? 19 : 20) : t.aromatic > 0.3 ? 18 : 17; break; // ebass | dbass | eguitar | aguitar
                    case wtChip: table = p.heaviestHalogen <= 1 ? 23 : 24; break;                 // chip | vgame
                    case wtEdge: table = t.nitro >= std::max({t.nitrile, t.charged, t.strained}) ? 28 : t.nitrile >= std::max(t.charged, t.strained) ? 5 : t.charged >= t.strained ? 7 : 2; break; // distorted | sync | digital | pwm
                    case wtGrain: table = 25; break;                                             // granular
                    case wtComplex: table = t.isotopic > 0 ? 26 : t.phosphorus > 0.3 ? 22 : t.conjugated > 0.6 ? 27 : t.diverse >= 0.5 ? 29 : 1; break; // overtone | fmsynth | blended | handdrawn | harmonics
                    default: table = 30; break;                                                  // wtReed: theremin
                }
                b.set(n, "table", table, why, {"amide", "fused", "hydroxyl", "ester", "carboxyl", "amine", "halogenated", "nitro", "nitrile", "charged", "strained", "salt", "diverse", "thio"});
                b.set(n, "position", isPrimary ? position : clamp01(1.0 - position), "S1-table-position", {"bright", "placement"});
                maxCopies = 5; break;
            }
        }
        const int copies = std::clamp(static_cast<int>(std::lround(1.0 + (maxCopies - 1) * thickness * thickness * (isPrimary ? 1.0 : 0.6))), 1, isPrimary ? maxCopies : std::min(maxCopies, 3));
        b.set(n, "unisonVoices", copies, "S2-unison", {"size", "flexible", "conjugated"});
        b.set(n, "detuneCents", detune, "S2-unison", {"flexible", "symmetric"});
        b.set(n, "unisonSpread", spread, "S2-unison", {"chiral", "size", "handed", "cisTrans"});
        b.set(n, "phaseRandom", 0.15 + 0.7 * t.flexible, "S2-unison", {"flexible"});
        b.set(n, "drift", drift, "S2-drift", {"flexible", "lipophilic"});
        b.set(n, "fine", isotopeCents + (isPrimary ? 0.0 : 6.0 + 10.0 * t.placement), "S2-fine", {"isotopic", "placement"});
        return std::pair{n, copies};
    };
    // Register: only the extremes move. Big greasy molecules sit an octave down; the very smallest sit an octave up.
    const double octave = (t.size > 0.80 && t.lipophilic > 0.62) ? -1.0 : (p.heavyAtoms <= 2.0 ? 1.0 : 0.0);
    auto [primary, primaryCopies] = buildSource("primary", primaryVoice, true, "V1-primary-voice");
    b.set(primary, "octave", octave, "S2-register", {"size", "lipophilic"});
    const double unisonGain = 1.0 / (1.0 + 0.10 * (primaryCopies - 1));
    b.set(primary, "outputLevel", (0.50 + 0.10 * (1.0 - t.size)) * unisonGain * voiceLevel[primaryVoice], "S2-level", {"size"});

    std::vector<std::string> sources{"primary"};
    std::vector<domain::Node> nodes;
    if (layered)
    {
        auto [secondary, copies] = buildSource("secondary", secondVoice, false, "V2-layer-voice");
        // Consonant intervals only, chosen by build: fused rings add body below, long chains add
        // shimmer above, branching adds a fifth, and otherwise the layer is a detuned double.
        const double interval = t.fused > 0.35 ? -12.0 : t.chain > 0.45 ? 12.0 : t.branched > 0.62 ? 7.0 : t.linear > 0.75 ? 19.0 : 0.0;
        b.set(secondary, interval == -12.0 ? "octave" : "coarse", interval == -12.0 ? -1.0 : interval, "V2-layer-interval", {"fused", "chain", "branched", "linear"});
        b.set(secondary, "outputLevel", (0.16 + 0.26 * clamp01(secondRatio)) / (1.0 + 0.10 * (copies - 1)) * voiceLevel[secondVoice], "V2-layer-level", {"symmetric", "size", "diverse"});
        nodes.push_back(std::move(secondary)); sources.push_back("secondary");
    }
    // N1: air and transients. Halogens hiss, charges spark, and the smallest volatile molecules breathe.
    const double air = clamp01(0.75 * t.halogenated + 0.55 * t.charged + 0.5 * (1.0 - t.size) * (1.0 - t.size) * (1.0 - t.polar) + 0.25 * struck * t.strained);
    const bool noisy = air >= 0.40;
    if (noisy)
    {
        auto n = b.node("noise", ModuleType::noise, "N1-noise", {"halogenated", "charged", "size", "polar"}, air);
        b.set(n, "color", t.lipophilic > 0.5 ? 1 : 0, "N1-noise", {"lipophilic"});
        b.set(n, "mode", t.charged > 0.2 || struck > 0.45 ? 1 : 0, "N1-noise", {"charged"});
        b.set(n, "burstMs", emix(12.0, 160.0, 1.0 - struck), "N1-noise", {"cyclic", "flexible"});
        b.set(n, "outputLevel", 0.05 + 0.16 * air, "N1-noise", {"halogenated", "charged"});
        nodes.push_back(std::move(n)); sources.push_back("noise");
    }
    // B1: weight. Large lipophilic molecules, and anything already dropped an octave, get the sub.
    const double heft = clamp01(0.55 * t.size + 0.45 * t.lipophilic + 0.2 * t.weight);
    const bool sub = heft >= 0.70 || octave < 0.0;
    if (sub)
    {
        auto n = b.node("sub", ModuleType::sub, "B1-sub", {"size", "lipophilic", "weight"}, heft);
        b.set(n, "waveform", t.bright >= 0.5 ? 1 : 0, "B1-sub", {"bright"});
        b.set(n, "octave", t.size > 0.92 && octave == 0.0 ? -2 : -1, "B1-sub", {"size"});
        b.set(n, "drift", drift, "B1-sub", {"flexible", "lipophilic"});
        b.set(n, "outputLevel", 0.16 + 0.26 * heft, "B1-sub", {"size", "lipophilic"});
        nodes.push_back(std::move(n));
    }

    // ---- P1..P3: processors ------------------------------------------------------------------------
    // R1: the resonator is now the exception. A comb for strained and caged skeletons (a tight
    // body), a modal bank only for inorganic compounds with two or more metal atoms.
    const bool cage = t.strained >= 0.35 || (t.fused >= 0.9 && t.saturated >= 0.8 && t.alicyclic >= 0.8);
    const bool bell = primaryVoice == fmMetal && p.metalAtoms >= 2.0 && t.inorganic > 0.0;
    const bool resonator = cage || bell;
    if (resonator)
    {
        auto n = b.node("resonator", ModuleType::resonator, "R1-resonator", {"strained", "fused", "metallic"}, cage ? t.strained : t.metallic);
        b.set(n, "mode", bell ? 1 : 0, "R1-resonator", {"metallic", "strained"});
        b.set(n, "tuneRatio", bell ? 1.0 + 1.6 * t.weight : 1.0, "R1-resonator", {"weight"});
        b.set(n, "combFeedback", 0.35 + 0.45 * t.cyclic, "R1-resonator", {"cyclic"});
        b.set(n, "modalQ", 3.0 + 7.0 * t.metallic, "R1-resonator", {"metallic"});
        for (auto& stored : n.parameters)
        {
            if (stored.id == "modeRatios") stored.values = {1.0, 1.9 + 0.8 * t.weight, 2.7 + 0.9 * t.placement, 3.6 + 0.4 * t.diverse};
            if (stored.id == "modeLevels") stored.values = {1.0, 0.6, 0.35 + 0.2 * t.bright, 0.2};
            if (stored.id == "modeRatios" || stored.id == "modeLevels") for (auto& value : stored.values) value = settle(value);
        }
        b.trace("parameter", "resonator.modeRatios", "R1-resonator", {"weight", "placement", "diverse"}, 1.9 + 0.8 * t.weight, 1.9 + 0.8 * t.weight);
        b.trace("parameter", "resonator.modeLevels", "R1-resonator", {"bright"}, 0.35 + 0.2 * t.bright, 0.35 + 0.2 * t.bright);
        nodes.push_back(std::move(n));
    }
    // D1: drive. Charge, nitro groups, ring strain, conjugated unsaturation and halogens all add
    // edge, and the cause picks the curve: strain folds, nitro clips, charge is asymmetric (even
    // harmonics), halogens wrap, unsaturation saturates softly.
    const std::array<double, 5> edges{0.75 * t.unsaturated * (0.4 + 0.6 * t.conjugated), t.nitro, t.strained, 0.7 * t.halogenated, std::max(t.charged, t.zwitterionic * 0.6)};
    const auto strongest = static_cast<std::size_t>(std::max_element(edges.begin(), edges.end()) - edges.begin());
    const double edge = edges[strongest];
    const bool shaper = edge >= 0.30;
    if (shaper)
    {
        auto n = b.node("shaper", ModuleType::shaper, "D1-drive", {"unsaturated", "nitro", "strained", "halogenated", "charged"}, edge);
        constexpr std::array<int, 5> curves{0, 1, 2, 3, 4}; // tanh, hardClip, fold, sine, asymmetric
        b.set(n, "curve", curves[strongest], "D1-drive-curve", {"unsaturated", "nitro", "strained", "halogenated", "charged"});
        b.set(n, "drive", 1.4 + 5.5 * edge, "D1-drive", {"unsaturated", "nitro", "strained", "halogenated", "charged"});
        b.set(n, "wet", 0.25 + 0.5 * edge, "D1-drive", {"unsaturated", "nitro", "strained", "halogenated", "charged"});
        b.set(n, "outputLevel", 1.0 - 0.25 * edge, "D1-drive", {"charged"});
        nodes.push_back(std::move(n));
    }
    // F1: the filter. A lone sine or triangle has nothing to filter and goes without.
    const bool plain = (primaryVoice == sine || primaryVoice == triangle) && !layered && !noisy && !shaper;
    const bool filter = !plain;
    int filterMode = 0;
    if (filter)
    {
        auto n = b.node("filter", ModuleType::filter, "F1-filter", {"bright"}, t.bright);
        // Mode by character: zwitterions cancel (notch), aryl sulfonyls are nasal (band-pass), big
        // lipophilic molecules are fat (24 dB ladder), tiny polar ones are thin (high-pass).
        filterMode = t.zwitterionic > 0 ? 4 : (t.sulfonyl > 0.5 && t.aromatic > 0.2) ? 1 : (t.lipophilic > 0.70 && t.size > 0.45) ? 3 : (t.size < 0.14 && t.polar > 0.6) ? 2 : 0;
        b.set(n, "mode", filterMode, "F1-filter-mode", {"zwitterionic", "sulfonyl", "lipophilic", "size", "polar"});
        const bool opens = filterMode == 0 || filterMode == 3;
        // The sweep: struck sounds start closed and are opened by E2; held sounds sit where they are.
        const double sweep = opens ? 0.10 + 0.42 * struck : 0.06 + 0.10 * struck;
        const double openness = clamp01(0.68 + 0.90 * (t.bright - 0.37) + 0.5 * (p.branchCentrality - 0.55) + 0.4 * (p.ringNitrogenDistance - 0.12) - 0.40 * (t.branched - 0.5) - 0.45 * sweep);
        // High-pass and band-pass stay near the played register so they thin the tone without removing it.
        const double cutoff = filterMode == 2 ? emix(90.0, 420.0, t.bright) : filterMode == 1 ? emix(500.0, 2400.0, openness) : filterMode == 4 ? emix(350.0, 3800.0, openness) : emix(160.0, 15000.0, openness);
        b.set(n, "cutoff", cutoff, "F1-cutoff", {"bright", "placement", "branched"});
        b.set(n, "q", (filterMode == 1 ? 0.6 : 0.7) + 2.6 * t.carbonyl * (0.5 + 0.5 * struck) * (filterMode == 1 ? 0.3 : 1.0) + 1.2 * t.strained + 1.6 * t.thio, "F1-resonance", {"carbonyl", "strained", "thio"});
        b.set(n, "envAmount", sweep, "F1-sweep", {"cyclic", "flexible", "size"});
        b.set(n, "keytrack", opens ? 0.35 + 0.3 * t.bright : 0.5, "F1-keytrack", {"bright"});
        b.set(n, "drive", 1.0 + 2.5 * t.charged + 1.5 * t.nitro, "F1-drive", {"charged", "nitro"});
        nodes.push_back(std::move(n));
    }

    // ---- X1..X4: the effects tail --------------------------------------------------------------------
    const bool chorus = t.flexible >= 0.70 || (held > 0.5 && primaryCopies == 1 && t.size > 0.4);
    const double echo = clamp01(0.9 * t.amide + 0.5 * t.symmetric * t.size + 0.5 * t.chain + 0.3 * t.geometric);
    const bool delay = echo >= 0.45;
    const double space = clamp01(0.10 + 0.40 * t.size + 0.25 * t.acceptor + 0.35 * held - 0.30 * shape[perc]);
    const bool reverb = space >= 0.22;
    const bool width = p.stereocenters > 0 || p.geometricBonds > 0 || t.salt > 0;
    if (chorus)
    {
        auto n = b.node("chorus", ModuleType::chorus, "X1-chorus", {"flexible", "size"}, t.flexible);
        b.set(n, "rate", emix(0.15, 2.2, t.flexible), "X1-chorus", {"flexible"}); b.set(n, "depth", 0.2 + 0.5 * t.flexible, "X1-chorus", {"flexible"});
        b.set(n, "voices", 2.0 + std::round(2.0 * t.size), "X1-chorus", {"size"}); b.set(n, "mix", 0.14 + 0.24 * t.flexible, "X1-chorus", {"flexible"});
        nodes.push_back(std::move(n));
    }
    if (delay)
    {
        auto n = b.node("delay", ModuleType::delay, "X2-delay", {"amide", "symmetric", "chain", "geometric"}, echo);
        // Repeating units set the rhythm: more amide links or a longer chain, shorter synced repeats.
        b.set(n, "syncMode", 1, "X2-delay-sync", {});
        b.set(n, "syncDivision", std::clamp(2.0 + std::round(1.5 * sat(p.amides + 0.3 * p.longestChain, 2.0) * 2.0), 2.0, 6.0), "X2-delay", {"amide", "chain"});
        b.set(n, "feedback", 0.18 + 0.36 * echo, "X2-delay", {"amide", "symmetric", "chain"});
        b.set(n, "damping", 0.75 - 0.5 * t.bright, "X2-delay", {"bright"});
        b.set(n, "spread", 0.6 * (p.geometricBonds > 0 ? t.cisTrans : t.handed), "X2-delay", {"handed", "cisTrans"});
        b.set(n, "mix", 0.10 + 0.20 * echo, "X2-delay", {"amide", "symmetric", "chain"});
        nodes.push_back(std::move(n));
    }
    if (reverb)
    {
        auto n = b.node("reverb", ModuleType::reverb, "X3-reverb", {"size", "acceptor", "flexible"}, space);
        b.set(n, "size", 0.25 + 0.65 * t.size, "X3-reverb", {"size"});
        b.set(n, "decaySeconds", emix(0.5, 7.0, 0.5 * t.size + 0.5 * held), "X3-reverb", {"size", "flexible"});
        b.set(n, "damping", 0.75 - 0.5 * t.bright, "X3-reverb", {"bright"});
        b.set(n, "preDelayMs", 4.0 + 50.0 * t.linear, "X3-reverb", {"linear"});
        b.set(n, "mix", 0.06 + 0.30 * space, "X3-reverb", {"size", "acceptor"});
        nodes.push_back(std::move(n));
    }
    if (width)
    {
        auto n = b.node("width", ModuleType::width, "X4-width", {"chiral", "geometric", "salt"}, t.chiral);
        b.set(n, "width", 1.0 + 0.55 * t.chiral + 0.25 * t.geometric + 0.2 * t.salt, "X4-width", {"chiral", "geometric", "salt"});
        b.set(n, "bassMonoHz", 80.0 + 160.0 * heft, "X4-width", {"size", "lipophilic"});
        nodes.push_back(std::move(n));
    }

    // ---- G1: wiring ------------------------------------------------------------------------------------
    std::vector<std::string> tail; if (chorus) tail.push_back("chorus"); if (delay) tail.push_back("delay"); if (reverb) tail.push_back("reverb"); if (width) tail.push_back("width");
    std::vector<std::string> chain; if (resonator) chain.push_back("resonator");
    // Nitro compounds distort after the filter (the resonance is clipped); everything else before it.
    if (shaper && filter && strongest == 1) { chain.push_back("filter"); chain.push_back("shaper"); }
    else { if (shaper) chain.push_back("shaper"); if (filter) chain.push_back("filter"); }
    const bool mixer = sources.size() >= 2;
    if (mixer)
    {
        auto n = b.node("mix", ModuleType::mixer, "G1-mixer", {"handed"}, static_cast<double>(sources.size()));
        // Enantiomers lean opposite ways: R to one side, S to the other.
        b.set(n, "pan", 0.28 * t.handed, "G1-handedness", {"handed"});
        nodes.push_back(std::move(n));
    }
    b.patch.nodes.push_back(std::move(primary));
    for (auto& n : nodes) b.patch.nodes.push_back(std::move(n));
    const std::string out = "output";
    const std::string afterChain = tail.empty() ? out : tail.front();
    const std::string afterSources = chain.empty() ? afterChain : chain.front();
    // Headroom: layers, drive and a ringing body each add peak level, so the bus gives some back.
    const double bus = 0.75 - (layered ? 0.09 : 0.0) - (shaper ? 0.05 : 0.0) - (resonator ? 0.10 : 0.0);
    if (mixer)
    {
        for (const auto& s : sources) b.edge(s, "mix", s == "primary" ? 1.0 : s == "secondary" ? 0.85 : 0.7, "G1-routing", {});
        b.edge("mix", afterSources, bus, "G1-routing", {});
    }
    else b.edge("primary", afterSources, bus, "G1-routing", {});
    // The sub joins after the resonator and the shaper so its weight stays clean, but before the filter.
    if (sub) b.edge("sub", filter ? "filter" : afterChain, 0.8, "G1-routing", {});
    for (std::size_t i = 1; i < chain.size(); ++i) b.edge(chain[i - 1], chain[i], 0.9, "G1-routing", {});
    if (!chain.empty()) b.edge(chain.back(), afterChain, 0.9, "G1-routing", {});
    // A comb or modal body colours the sound rather than replacing it: keep a dry path beside it.
    if (resonator) b.edge(mixer ? "mix" : "primary", chain.size() > 1 ? chain[1] : afterChain, 0.35, "G1-parallel-body", {"strained", "metallic"});
    for (std::size_t i = 1; i < tail.size(); ++i) b.edge(tail[i - 1], tail[i], 1.0, "G1-routing", {});
    if (!tail.empty()) b.edge(tail.back(), out, 1.0, "G1-routing", {});
    // The noise source also excites a comb directly, so a caged skeleton gets a plucked transient.
    if (resonator && noisy && cage) b.edge("noise", "resonator", 0.5, "G1-excite", {"strained"}, domain::AudioPort::exciteIn);

    // ---- M1: modulators and the matrix ------------------------------------------------------------------
    // E2 is the timbre envelope (filter sweep, FM bite): always quicker and more struck than E1.
    const domain::Envelope e2{std::clamp(e1.attack * 0.5, 0.001, 2.0), std::clamp(emix(0.08, 1.2, 0.5 * t.size + 0.5 * held) * ring, 0.01, 4.0), clamp01(0.10 + 0.55 * held), e1.release, 0.0, -0.6, 0.0};
    // E3 is the slow movement envelope: it carries a wavetable through its frames over the note.
    const domain::Envelope e3{std::clamp(emix(0.25, 2.0, 0.6 * t.size + 0.4 * t.flexible), 0.001, 2.0), 1.0, 1.0, e1.release, 0.3, 0.0, 0.0};
    b.patch.envelopes = {e1, e2, e3, domain::Envelope{}};
    for (auto& e : b.patch.envelopes) for (auto field : {&domain::Envelope::attack, &domain::Envelope::decay, &domain::Envelope::sustain, &domain::Envelope::release,
                                                          &domain::Envelope::attackCurve, &domain::Envelope::decayCurve, &domain::Envelope::releaseCurve}) e.*field = settle(e.*field);
    domain::Lfo l1{std::clamp(emix(0.18, 6.5, t.flexible), 0.05, 12.0), domain::LfoWaveform::sine};
    if (primaryVoice == square || primaryVoice == pulse) l1.waveform = domain::LfoWaveform::triangle;
    if (primaryVoice == wtChip) l1.waveform = domain::LfoWaveform::sampleHold;
    l1.fadeMs = shape[lead] > 0.35 ? 250.0 + 500.0 * t.cyclic : 0.0;
    domain::Lfo l2{std::clamp(emix(0.45, 0.06, t.size), 0.05, 12.0), held > 0.5 ? domain::LfoWaveform::randomSmooth : domain::LfoWaveform::sine};
    l1.rate = settle(l1.rate); l1.fadeMs = settle(l1.fadeMs); l2.rate = settle(l2.rate);
    b.patch.lfos = {l1, l2};
    b.trace("lfo", "l1.rate", "M1-lfo", {"flexible"}, l1.rate, l1.rate); b.trace("lfo", "l2.rate", "M1-lfo", {"size"}, l2.rate, l2.rate);

    // The one parameter that is "the movement" of this voice.
    const auto primaryType = voiceModule(primaryVoice);
    const char* motion = primaryType == ModuleType::wavetable ? "position" : primaryType == ModuleType::fm ? "index"
                       : (primaryVoice == square || primaryVoice == pulse) ? "pulseWidth" : primaryType == ModuleType::harmonic ? "symmetry" : "fine";
    const bool vibrato = std::string_view(motion) == "fine";
    const double motionDepth = vibrato ? 0.02 + 0.07 * t.flexible : 0.08 + 0.34 * t.flexible + 0.10 * held;
    b.row("velocity-level", domain::ModulationSource::velocity, "primary", "outputLevel", 0.22 + 0.25 * struck, "M1-velocity", {"cyclic", "flexible"});
    b.row("l1-motion", domain::ModulationSource::l1, "primary", motion, motionDepth, "M1-motion", {"flexible"});
    if (primaryType == ModuleType::wavetable) b.row("e3-sweep", domain::ModulationSource::e3, "primary", "position", (struck > held ? -1.0 : 1.0) * (0.12 + 0.30 * (1.0 - t.symmetric)), "M1-sweep", {"symmetric", "cyclic"});
    if (primaryType == ModuleType::fm) b.row("e2-bite", domain::ModulationSource::e2, "primary", "index", 0.15 + 0.35 * struck, "M1-sweep", {"cyclic", "flexible"});
    if (filter)
    {
        b.row("velocity-tone", domain::ModulationSource::velocity, "filter", "cutoff", 0.06 + 0.12 * struck, "M1-velocity", {"cyclic", "flexible"});
        if (held > 0.4) b.row("l2-drift", domain::ModulationSource::l2, "filter", "cutoff", 0.03 + 0.07 * held, "M1-drift", {"flexible", "size"});
    }
    if (layered && voiceModule(secondVoice) == ModuleType::wavetable) b.row("l2-layer", domain::ModulationSource::l2, "secondary", "position", 0.15 + 0.25 * t.flexible, "M1-motion", {"flexible"});
    b.row("cc1-expression", domain::ModulationSource::cc1, "primary", vibrato ? "fine" : motion, vibrato ? 0.12 : 0.35, "M1-expression", {"flexible"});
    // Four macros with honest labels: tone, motion, edge, space.
    const auto macro = [&](std::size_t index, const char* label, domain::ModulationSource source, const std::string& node, const char* parameter, double depth)
    {
        b.patch.macros[index] = {std::string(label) + ": " + node + " " + parameter, 0};
        b.row("macro" + std::to_string(index + 1), source, node, parameter, depth, "M1-macro", {});
    };
    if (filter) macro(0, "Tone", domain::ModulationSource::macro1, "filter", "cutoff", 0.30); else macro(0, "Tone", domain::ModulationSource::macro1, "primary", "outputLevel", 0.25);
    macro(1, "Motion", domain::ModulationSource::macro2, "primary", vibrato ? "detuneCents" : motion, vibrato ? 0.5 : 0.4);
    if (shaper) macro(2, "Edge", domain::ModulationSource::macro3, "shaper", "drive", 0.4); else if (filter) macro(2, "Edge", domain::ModulationSource::macro3, "filter", "q", 0.4); else macro(2, "Edge", domain::ModulationSource::macro3, "primary", "drift", 0.5);
    if (reverb) macro(3, "Space", domain::ModulationSource::macro4, "reverb", "mix", 0.35); else if (delay) macro(3, "Space", domain::ModulationSource::macro4, "delay", "mix", 0.35);
    else if (chorus) macro(3, "Space", domain::ModulationSource::macro4, "chorus", "mix", 0.35); else macro(3, "Space", domain::ModulationSource::macro4, "primary", "unisonSpread", 0.5);

    if (const auto error = domain::validate(b.patch); !error.empty()) return {intent, {}, {}, "generated Patch invalid: " + error};
    auto trace = object();
    put(trace, "projectionVersion", sonicIntentVersion); put(trace, "mappingVersion", mappingVersion);
    put(trace, "canonicalIdentity", juce::String(a.canonicalIsomericSmiles)); put(trace, "sonicIntent", encodeSonicIntent(intent));
    put(trace, "primaryVoice", voiceNames[primaryVoice]); put(trace, "layerVoice", layered ? juce::var(voiceNames[secondVoice]) : juce::var());
    put(trace, "rules", b.items); put(trace, "patch", domain::encodePatchValue(b.patch));
    return {intent, std::move(b.patch), std::move(trace), {}};
}
}
