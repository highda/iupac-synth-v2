#include "iupac/chemistry/Mapping.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

namespace iupac::chemistry
{
namespace
{
double num(const juce::DynamicObject* o, const char* key) { return o == nullptr ? 0.0 : static_cast<double>(o->getProperty(key)); }
double sat(double x) { return x <= 0.0 ? 0.0 : x / (1.0 + x); }
double clamp(double x) { return std::clamp(x, 0.0, 1.0); }
juce::var object() { return juce::var(new juce::DynamicObject); }
void put(juce::var& v, const char* k, juce::var x) { v.getDynamicObject()->setProperty(k, std::move(x)); }
void put(juce::var& v, const char* k, const std::string& x) { put(v, k, juce::String(x)); }
void put(juce::var& v, const char* k, const char* x) { put(v, k, juce::String(x)); }

domain::Node node(std::string id, domain::ModuleType type)
{
    domain::Node n{std::move(id), type, {}};
    const auto& d = domain::moduleCatalog().at(static_cast<std::size_t>(type));
    for (const auto& p : d.parameters) n.parameters.push_back({std::string(p.id), std::vector<double>(p.arraySize == 0 ? 1 : p.arraySize, p.defaultValue)});
    return n;
}
void set(domain::Node& n, std::string_view id, double value)
{
    for (auto& p : n.parameters) if (p.id == id) { p.values[0] = value; return; }
}
void setArray(domain::Node& n, std::string_view id, std::initializer_list<double> values)
{
    for (auto& p : n.parameters) if (p.id == id) { p.values.assign(values); return; }
}
void setArray(domain::Node& n, std::string_view id, const std::vector<double>& values)
{
    for (auto& p : n.parameters) if (p.id == id) { p.values = values; return; }
}
// Re-expansion helpers for the phase-4 coverage block (#128). `bip` turns a [0,1] driver into the
// bipolar form a signed descriptor wants; `quant` lands a continuous driver on a discrete
// descriptor's integral values inside its declared range. Both are plain arithmetic on the axes the
// projection already publishes — no hashing, no fixture exceptions.
double bip(double t) { return 2.0 * clamp(t) - 1.0; }
double quant(double lo, double hi, double value) { return std::clamp(std::round(value), lo, hi); }
int motif(const Analysis& a, const char* name)
{
    const auto* d = a.descriptors.getDynamicObject();
    const auto* m = d == nullptr ? nullptr : d->getProperty("motifCounts").getDynamicObject();
    return m == nullptr ? 0 : static_cast<int>(m->getProperty(name));
}
void traceItem(juce::Array<juce::var>& items, const char* kind, const std::string& id, const char* rule, double pre, double value)
{
    auto x=object(); put(x,"kind",kind); put(x,"id",id); put(x,"ruleId",rule); put(x,"preClamp",pre); put(x,"value",value); items.add(x);
}
}

SonicIntent project(const Analysis& a)
{
    const auto* d=a.descriptors.getDynamicObject();
    const double heavy=static_cast<double>(a.heavyAtoms), aromatic=num(d,"aromaticAtomFraction");
    const double rot=num(d,"rotatableBonds"), rings=num(d,"ringCount"), tpsa=num(d,"tpsa");
    const double motion=clamp(rot/std::max(1.0,heavy-1.0));
    const double rigidity=clamp((aromatic+(1.0-motion))*0.5);
    const double rough=sat((static_cast<double>(a.elementCounts[5]+a.elementCounts[6]+a.elementCounts[7]+a.elementCounts[8])+std::abs(a.formalCharge))/2.0);
    SonicIntent s{sat(heavy/24.0),sat((tpsa/std::max(1.0,heavy))/6.0),rigidity,rough,sat((rings+motif(a,"amide"))/3.0),clamp(1.0-(rough+aromatic/2.0)*0.5),motion,{}};
    const auto* detail=a.detail.getDynamicObject(); const auto* bonds=detail?detail->getProperty("bonds").getArray():nullptr;
    double sum=0, square=0, placement=0; int count=0;
    if(bonds) for(const auto& v:*bonds) if(const auto* b=v.getDynamicObject()){const double order=num(b,"order");sum+=order;square+=order*order;placement+=(num(b,"begin")+1.0)*(num(b,"end")+1.0)*order;++count;}
    if(count){s.detail.bondOrderMean=sum/count;s.detail.bondOrderSpread=std::sqrt(std::max(0.0,square/count-s.detail.bondOrderMean*s.detail.bondOrderMean));s.detail.motifPlacement=clamp(std::fmod(placement,17.0)/16.0);}
    const auto* atoms=detail?detail->getProperty("atoms").getArray():nullptr; double hetero=0;
    if(atoms) for(int i=0;i<atoms->size();++i) if(const auto* atom=(*atoms)[i].getDynamicObject()) if(static_cast<int>(atom->getProperty("atomicNumber"))!=6) hetero+=(i+1.0)/std::max(1,atoms->size());
    s.detail.heteroPlacement=clamp(hetero/std::max<std::size_t>(1,a.heavyAtoms)); return s;
}

juce::var encodeSonicIntent(const SonicIntent& s)
{
    auto v=object(); put(v,"sonicIntentVersion",sonicIntentVersion);
    auto axes=object(); put(axes,"density",s.density);put(axes,"brightness",s.brightness);put(axes,"rigidity",s.rigidity);put(axes,"roughness",s.roughness);put(axes,"decay",s.decay);put(axes,"harmonicity",s.harmonicity);put(axes,"motion",s.motion);put(v,"axes",axes);
    auto d=object();put(d,"bondOrderMean",s.detail.bondOrderMean);put(d,"bondOrderSpread",s.detail.bondOrderSpread);put(d,"heteroPlacement",s.detail.heteroPlacement);put(d,"motifPlacement",s.detail.motifPlacement);put(v,"structuralDetail",d);return v;
}

GenerationResult generate(const Analysis& a)
{
    if(a.heavyAtoms==0) return {{},{},{},"unsupported empty Analysis"};
    const auto s=project(a); domain::Patch p; p.noiseSeed=0x49555041;
    juce::Array<juce::var> items;
    const bool harmonic=s.harmonicity>=0.7, secondary=s.motion>=0.25||motif(a,"carbonyl")||motif(a,"amine"), noise=s.roughness>=0.2;
    // Phase-4 coverage (#128, W19). Every parameter D8 added to the catalog is assigned below from
    // the existing SonicIntent axes and `structuralDetail` fields — phase 4 adds no analysis field —
    // under exactly the `ruleId` its `data/mapping-coverage.json` entry names, so the coverage table
    // and the generation trace describe the same mapper.
    const int voices=static_cast<int>(quant(1,7,1.0+3.0*s.density+2.0*s.motion));
    // A detuned stack beats, so its crest factor grows even though the engine's sqrt(2/n) pan gains
    // hold the incoherent level at one copy. Trimming `outputLevel` by the same amount is what keeps
    // the widened sources clear of the V5 sample-peak guard; `voices` derives from density and
    // motion, so this introduces no driver the coverage entry does not already list.
    const double unisonGain=1.0/(1.0+0.12*(voices-1));
    const double detune=std::clamp(6.0+26.0*s.roughness+10.0*s.detail.bondOrderSpread,0.0,50.0);
    const double keytrack=clamp(0.85+0.15*s.rigidity), driftDepth=clamp(0.12+0.45*s.motion+0.25*s.decay);
    // Register and interval are assigned on the complementary source role only. Transposing the
    // *primary* detunes it from the resonator's modal tuning, which costs far more level than the
    // V5 window has to spare, and it also moves every played note off its own pitch.
    const double octave=quant(-3,0,-std::floor(3.5*s.density));
    // Consonant upward intervals only: `motifPlacement` chooses which one, so ordered local
    // structure picks the harmony instead of an arbitrary semitone offset, and the complement stays
    // an overtone partner — dropped below the note it would fall under whatever high-pass or
    // band-pass `brightness` selected.
    constexpr std::array<double,8> interval{0,7,12,5,3,10,4,9};
    const double coarse=interval.at(static_cast<std::size_t>(std::clamp(static_cast<int>(8.0*s.detail.motifPlacement),0,7)));
    const double morph=clamp(0.9*s.harmonicity), modInDepth=clamp(0.3+0.5*s.roughness-0.3*s.harmonicity);
    auto sourceBlock=[&](domain::Node& n,bool lead)
    {
        set(n,"unisonVoices",voices);set(n,"detuneCents",detune);set(n,"unisonSpread",clamp(0.45+0.35*s.motion+0.2*s.detail.heteroPlacement));
        set(n,"phaseRandom",clamp(0.3+0.6*s.roughness));traceItem(items,"parameter",n.id+".unison","M5-unison",1.0+3.0*s.density+2.0*s.motion,voices);
        set(n,"drift",driftDepth);traceItem(items,"parameter",n.id+".drift","M5-drift",0.12+0.45*s.motion+0.25*s.decay,driftDepth);
        set(n,"keytrack",keytrack);
        // The primary keeps the played pitch; the complement carries the register and the interval,
        // the same role-shaped assignment `fm.carrierRatio` already records.
        if(!lead){set(n,"octave",octave);set(n,"coarse",coarse);}
        set(n,"fine",std::clamp(bip(s.detail.heteroPlacement)*(lead?8.0:35.0),-100.0,100.0));
        traceItem(items,"parameter",n.id+".pitch","M7-pitch",-std::floor(3.5*s.density),lead?0.0:octave);
        if(n.type==domain::ModuleType::harmonic)
        {
            // `oddEvenBalance` attenuates one parity group and `symmetry` tilts across the partial
            // index, so both are biased to the side that spares the fundamental: the spectrum `tilt`
            // authors is already fundamental-dominant, and taking level off it is what pushed the V5
            // active-RMS floor before this calibration.
            set(n,"harmonicityMorph",morph);set(n,"oddEvenBalance",std::clamp(0.05+(s.detail.bondOrderMean-1.5)*0.3+bip(s.rigidity)*0.2,-1.0,1.0));
            // `brightness` is the spectral-emphasis axis, so it carries the tilt: a dark molecule
            // leans low-heavy and a bright one high-heavy, which puts the amplitude tilt on the same
            // side of the spectrum as the filter mode `brightness` already selected.
            set(n,"symmetry",clamp(0.15+0.2*s.detail.motifPlacement+0.6*s.brightness));
            traceItem(items,"parameter",n.id+".spectralShape","M6-spectral-shape",0.9*s.harmonicity,morph);
            // `motion` opens the stereo width of the partial fan, `heteroPlacement` sets how fast it
            // alternates across the sixteen columns, so a saved spectrum gains an image rather than
            // a rewritten set of ratios.
            std::vector<double> pans(16);
            for(std::size_t i=0;i<pans.size();++i)pans[i]=std::clamp(std::sin(static_cast<double>(i+1)*(0.35+2.4*s.detail.heteroPlacement))*(0.15+0.75*s.motion),-1.0,1.0);
            setArray(n,"partialPans",pans);traceItem(items,"parameter",n.id+".partialPans","M1-partial-pan",s.motion,pans.front());
        }
        else { set(n,"modInDepth",modInDepth);traceItem(items,"parameter",n.id+".modInDepth","M10-audio-rate",0.3+0.5*s.roughness-0.3*s.harmonicity,modInDepth); }
    };
    auto primary=node("primary",harmonic?domain::ModuleType::harmonic:domain::ModuleType::fm);
    if(harmonic){const double tilt=-1.6+1.4*s.brightness, inh=0.02*clamp((1-s.rigidity)*0.7+s.detail.bondOrderSpread*0.3);if(!domain::applyHarmonicSpectrum(primary,tilt,inh))return {s,{},{},"harmonic spectrum composition failed"};set(primary,"outputLevel",(0.42+0.18*s.density)*unisonGain);traceItem(items,"parameter","primary.partialSpectrum","M1-primary-harmonic",tilt,tilt);}
    else{set(primary,"carrierRatio",0.75+1.5*s.rigidity);set(primary,"modulatorRatio",0.5+3*s.detail.motifPlacement);set(primary,"index",0.5+4*s.roughness);set(primary,"outputLevel",(0.42+0.18*s.density)*unisonGain);}
    sourceBlock(primary,true);
    p.nodes.push_back(primary); traceItem(items,"node","primary","M1-primary",s.harmonicity,harmonic?1:0);
    if(secondary){auto n=node("secondary",harmonic?domain::ModuleType::fm:domain::ModuleType::harmonic);if(n.type==domain::ModuleType::harmonic){if(!domain::applyHarmonicSpectrum(n,-1.2+s.brightness,0.012*s.detail.heteroPlacement))return {s,{},{},"secondary spectrum composition failed"};}else{set(n,"modulatorRatio",1+3*s.detail.heteroPlacement);set(n,"index",1+3*s.motion);}set(n,"outputLevel",(0.22+0.16*s.motion)*unisonGain);sourceBlock(n,false);p.nodes.push_back(n);traceItem(items,"node","secondary","M2-complement",s.motion,1);}
    if(noise){auto n=node("noise",domain::ModuleType::noise);set(n,"color",s.brightness<.5?1:0);set(n,"mode",s.decay<.45?1:0);set(n,"burstMs",30+300*s.decay);set(n,"outputLevel",.05+.18*s.roughness);p.nodes.push_back(n);traceItem(items,"node","noise","M2-noise",s.roughness,1);}
    // M7-sub: molecules with real mass or sustain get the dedicated sub oscillator, which is what
    // makes a pad or a stab sit under the note instead of on top of it.
    const bool subUnit=s.density>=.3||s.decay>=.45;
    if(subUnit){auto n=node("sub",domain::ModuleType::sub);set(n,"waveform",s.brightness>=.35?1:0);set(n,"octave",quant(-2,-1,-1.0-std::floor(2.0*s.density)));
        set(n,"drift",driftDepth);set(n,"fine",std::clamp(bip(s.detail.heteroPlacement)*8.0,-100.0,100.0));set(n,"keytrack",keytrack);set(n,"outputLevel",clamp(.16+.26*s.density));
        p.nodes.push_back(n);traceItem(items,"node","sub","M7-sub",s.density,1);}
    const bool resonator=motif(a,"amide")||num(a.descriptors.getDynamicObject(),"ringCount")>0;
    if(resonator){auto n=node("resonator",domain::ModuleType::resonator);set(n,"mode",s.rigidity<.5?0:1);set(n,"tuneRatio",.65+1.4*s.density+1.5*s.detail.motifPlacement);set(n,"combFeedback",.2+.65*s.decay);set(n,"modalQ",2+8*s.rigidity);setArray(n,"modeRatios",{1.0,1.48+.24*s.detail.bondOrderMean,2.05+.35*s.detail.heteroPlacement,2.9+.5*s.detail.motifPlacement});setArray(n,"modeLevels",{1.0,.55+.2*s.brightness,.32+.18*s.detail.bondOrderSpread,.16+.16*s.motion});
        set(n,"outputLevel",clamp(.7+.15*s.density));traceItem(items,"parameter","resonator.outputLevel","M3-resonator-level",.7+.15*s.density,clamp(.7+.15*s.density));
        set(n,"exciteDepth",clamp(.2+.4*s.rigidity+.2*s.roughness));traceItem(items,"parameter","resonator.exciteDepth","M10-audio-rate",.2+.4*s.rigidity+.2*s.roughness,clamp(.2+.4*s.rigidity+.2*s.roughness));
        p.nodes.push_back(n);traceItem(items,"node","resonator","M3-resonator",s.rigidity,1);}
    const bool shaper=s.roughness>=.2||motif(a,"carbonyl"); if(shaper){auto n=node("shaper",domain::ModuleType::shaper);set(n,"drive",1+8*s.roughness+2*s.detail.bondOrderSpread);set(n,"wet",.2+.45*s.roughness);
        set(n,"curve",quant(0,4,std::floor(3.0*s.roughness+2.0*s.detail.bondOrderSpread)));traceItem(items,"parameter","shaper.curve","M9-shaper-curve",3.0*s.roughness+2.0*s.detail.bondOrderSpread,quant(0,4,std::floor(3.0*s.roughness+2.0*s.detail.bondOrderSpread)));
        set(n,"outputLevel",clamp(1.0-.1*s.density));traceItem(items,"parameter","shaper.outputLevel","M4-shaper-level",1.0-.1*s.density,clamp(1.0-.1*s.density));
        p.nodes.push_back(n);traceItem(items,"node","shaper","M4-shaper",s.roughness,1);}
    const bool filter=s.brightness>=.1||resonator; if(filter){auto n=node("filter",domain::ModuleType::filter);const int mode=s.brightness<.3?0:s.brightness<.65?1:2;set(n,"mode",mode);set(n,"cutoff",mode==0?300+8000*s.brightness*s.brightness:mode==1?400+5000*s.brightness:500+2500*s.brightness);set(n,"q",.7+4*s.harmonicity);
        // `cutoff` is logarithmic across 30..18000 Hz, so a compiled row depth of 1 is the whole
        // nine-octave span. These depths stay inside about one octave, which is the difference
        // between an audible stab and a band-pass patch modulated off its own partials entirely.
        // Only the two low-pass modes gain level as the cutoff rises. Band-pass and notch are
        // narrow (q reaches 4.7 here) and high-pass removes more as it opens, so the depth that
        // makes a low-pass stab would walk those three clean off their own partials; they take a
        // quarter of it.
        const double depthScale=(mode==0||mode==3)?1.0:.25, envAmount=std::clamp((.06+.12*s.motion-.09*s.decay)*depthScale,-1.0,1.0);
        set(n,"drive",std::clamp(1.0+4.0*s.roughness,1.0,16.0));set(n,"keytrack",std::clamp(.12*s.brightness*depthScale,-1.0,1.0));set(n,"envAmount",envAmount);
        traceItem(items,"parameter","filter.depth","M9-filter-depth",.06+.12*s.motion-.09*s.decay,envAmount);
        set(n,"outputLevel",clamp(1.0-.08*s.density));traceItem(items,"parameter","filter.outputLevel","M4-filter-level",1.0-.08*s.density,clamp(1.0-.08*s.density));
        p.nodes.push_back(n);traceItem(items,"node","filter","M4-filter",s.brightness,1);}
    // M8: the global post-mixer tail. Each unit is present when the structure asks for it, in the
    // one chorus->delay->reverb->width order the router's acyclic constraint allows.
    // A delay needs something to repeat: a molecule with no rotatable bonds and no ordered motif
    // placement gets the room but not the echo, which also keeps the tail's series dry/wet
    // crossfades off a patch that has no level to spare.
    const bool chorusUnit=s.motion>=.2||voices>=3, delayUnit=s.decay>=.3&&(s.motion>=.05||s.detail.motifPlacement>=.2),
        reverbUnit=s.decay>=.2||s.rigidity>=.6, widthUnit=s.motion>=.15||s.detail.heteroPlacement>=.1;
    if(chorusUnit){auto n=node("chorus",domain::ModuleType::chorus);set(n,"rate",std::clamp(.2+2.5*s.motion,.01,8.0));set(n,"depth",clamp(.15+.4*s.motion+.2*s.roughness));
        set(n,"voices",quant(2,4,2.0+std::round(2.0*s.density)));set(n,"feedback",std::clamp(.3*s.decay,0.0,.9));set(n,"mix",clamp(.08+.22*s.motion));set(n,"outputLevel",clamp(1.0-.05*s.density));
        p.nodes.push_back(n);traceItem(items,"node","chorus","M8-chorus",s.motion,1);}
    if(delayUnit){auto n=node("delay",domain::ModuleType::delay);set(n,"syncMode",s.motion>=.4?1:0);set(n,"timeMs",std::clamp(120.0+700.0*s.decay+200.0*s.detail.motifPlacement,1.0,2000.0));
        set(n,"syncDivision",quant(0,6,std::round(6.0*(1.0-s.motion))));set(n,"spread",std::clamp(.8*bip(s.detail.heteroPlacement),-1.0,1.0));set(n,"feedback",std::clamp(.15+.45*s.decay,0.0,.95));
        set(n,"damping",clamp(.85-.6*s.brightness));set(n,"mix",clamp(.06+.16*s.motion+.08*s.decay));set(n,"outputLevel",clamp(1.0-.05*s.density));
        p.nodes.push_back(n);traceItem(items,"node","delay","M8-delay",s.decay,1);}
    if(reverbUnit){auto n=node("reverb",domain::ModuleType::reverb);set(n,"size",clamp(.25+.45*s.density+.3*s.decay));set(n,"decaySeconds",std::clamp(.6+6.0*s.decay,.1,20.0));
        set(n,"damping",clamp(.8-.55*s.brightness));set(n,"preDelayMs",std::clamp(5.0+60.0*s.detail.motifPlacement,0.0,200.0));set(n,"width",clamp(.55+.45*s.detail.heteroPlacement));
        set(n,"mix",clamp(.06+.18*s.decay+.08*s.motion));set(n,"outputLevel",clamp(1.0-.05*s.density));
        p.nodes.push_back(n);traceItem(items,"node","reverb","M8-reverb",s.decay,1);}
    if(widthUnit){auto n=node("width",domain::ModuleType::width);set(n,"width",std::clamp(.85+.6*s.detail.heteroPlacement+.3*s.motion,0.0,2.0));
        set(n,"bassMonoHz",std::clamp(70.0+180.0*s.density,20.0,500.0));set(n,"outputLevel",clamp(1.0-.05*s.density));
        p.nodes.push_back(n);traceItem(items,"node","width","M8-width",s.detail.heteroPlacement,1);}
    std::vector<std::string> sources{"primary"};if(secondary)sources.push_back("secondary");if(noise)sources.push_back("noise");
    std::vector<std::string> processors;if(shaper&&filter){if(s.rigidity<.5)processors={"shaper","filter"};else processors={"filter","shaper"};}else if(shaper)processors={"shaper"};else if(filter)processors={"filter"};
    std::vector<std::string> tail;if(chorusUnit)tail.push_back("chorus");if(delayUnit)tail.push_back("delay");if(reverbUnit)tail.push_back("reverb");if(widthUnit)tail.push_back("width");
    const std::string sink=tail.empty()?std::string{"output"}:tail.front();
    const std::string post=processors.empty()?sink:processors.front();
    // M4-mixer: two or more sources get an explicit summing bus, so the bus level and its stereo
    // placement are authored values rather than a consequence of the edge gains. The gains into it
    // and out of it are chosen so the bus is close to level-neutral against the pre-#128 sum.
    const bool mixUnit=sources.size()>=2;
    if(mixUnit){auto n=node("mix",domain::ModuleType::mixer);set(n,"level",clamp(.96+.04*(1.0-s.density)));
        set(n,"pan",std::clamp(.35*bip(s.detail.heteroPlacement)+.15*bip(s.motion),-1.0,1.0));set(n,"outputLevel",clamp(.96+.04*s.density));
        p.nodes.push_back(n);traceItem(items,"node","mix","M4-mixer",s.density,1);}
    const std::string bus{"mix"}, sourceSink=mixUnit?bus:(resonator?"resonator":post);
    for(const auto& src:sources){p.edges.push_back({src,sourceSink,mixUnit?(src=="primary"?1.0:.65):(src=="primary"?.75:.5)});traceItem(items,"edge",src+"->"+sourceSink,"M3/M4-routing",1,1);}
    if(mixUnit)p.edges.push_back({bus,resonator?"resonator":post,1.0});
    // The sub bypasses the resonator so its weight reaches the processors unfiltered by a comb.
    if(subUnit)p.edges.push_back({"sub",post,.8});
    if(resonator){p.edges.push_back({"resonator",post,.85});if(s.motion>=.25){if(mixUnit)p.edges.push_back({bus,post,.3});else for(const auto& src:sources)p.edges.push_back({src,post,src=="primary"?.3:.2});}}
    for(std::size_t i=1;i<processors.size();++i)p.edges.push_back({processors[i-1],processors[i],.85});
    if(!processors.empty())p.edges.push_back({processors.back(),sink,1.0});
    // Unity through the tail: the four units are dry/wet crossfades, so the region is a straight
    // wire at its mixes and must not cost level just for being present.
    for(std::size_t i=1;i<tail.size();++i)p.edges.push_back({tail[i-1],tail[i],1.0});
    if(!tail.empty())p.edges.push_back({tail.back(),"output",1.0});
    // M10: the two typed audio-rate inputs become real edges where the topology already holds a
    // partner for them, so the depths assigned above are audible rather than inert.
    if(!harmonic&&secondary&&s.roughness>=.35){p.edges.push_back({"secondary","primary",clamp(.4+.4*s.roughness),domain::AudioPort::modIn});traceItem(items,"edge","secondary->primary.modIn","M10-audio-rate",s.roughness,1);}
    if(resonator&&noise){p.edges.push_back({"noise","resonator",clamp(.3+.4*s.rigidity),domain::AudioPort::exciteIn});traceItem(items,"edge","noise->resonator.exciteIn","M10-audio-rate",s.rigidity,1);}
    p.envelopes={domain::Envelope{.005+.12*(1-s.density),.08+.5*s.decay,.55+.35*s.density,.08+1.5*s.decay},domain::Envelope{.01,.1+.4*s.motion,.6,.15+.5*s.decay},domain::Envelope{.02,.2,.5,.2+.5*s.rigidity}};
    p.lfos={domain::Lfo{.1+5*s.motion,domain::LfoWaveform::sine},domain::Lfo{.07+2*s.detail.motifPlacement,domain::LfoWaveform::triangle}};
    auto row=[&](std::string id,domain::ModulationSource src,std::string n,std::string param,double depth,const char* rule){p.matrix.push_back({id,true,src,n,param,depth});traceItem(items,"matrix",id,rule,depth,depth);};
    row("velocity-primary",domain::ModulationSource::velocity,"primary","outputLevel",.28,"M5-velocity");row("l1-primary",domain::ModulationSource::l1,"primary",harmonic?"outputLevel":"index",.08+.22*s.motion,"M5-motion");
    row("e2-tone",domain::ModulationSource::e2,filter?"filter":"primary",filter?"cutoff":"outputLevel",.12+.2*s.brightness,"M5-envelope");row("cc1-expression",domain::ModulationSource::cc1,resonator?"resonator":filter?"filter":"primary",resonator?(s.rigidity<.5?"combFeedback":"modalQ"):filter?"cutoff":"outputLevel",.2,"M5-cc1");
    const std::array<std::string,4> targetNode{filter?"filter":"primary",(!harmonic?"primary":"primary"),resonator?"resonator":shaper?"shaper":"primary",secondary?"secondary":"primary"};
    const std::array<std::string,4> targetParam{filter?"cutoff":"outputLevel",!harmonic?"index":"outputLevel",resonator?(s.rigidity<.5?"combFeedback":"modalQ"):shaper?"drive":"outputLevel","outputLevel"};
    const std::array<domain::ModulationSource,4> macroSrc{domain::ModulationSource::macro1,domain::ModulationSource::macro2,domain::ModulationSource::macro3,domain::ModulationSource::macro4};
    for(int i=0;i<4;++i){p.macros[i]={"Macro "+std::to_string(i+1)+": "+targetNode[i]+" "+targetParam[i],0};row("macro"+std::to_string(i+1),macroSrc[i],targetNode[i],targetParam[i],.25,"M5-macro");}
    if(const auto error=domain::validate(p);!error.empty())return {s,{},{},"generated Patch invalid: "+error};
    auto trace=object();put(trace,"projectionVersion",sonicIntentVersion);put(trace,"mappingVersion",mappingVersion);put(trace,"canonicalIdentity",a.canonicalIsomericSmiles);put(trace,"sonicIntent",encodeSonicIntent(s));put(trace,"rules",items);put(trace,"patch",domain::encodePatchValue(p));
    return {s,std::move(p),std::move(trace),{}};
}
}
