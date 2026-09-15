#include "iupac/chemistry/Mapping.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

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
    auto primary=node("primary",harmonic?domain::ModuleType::harmonic:domain::ModuleType::fm);
    if(harmonic){const double tilt=-1.6+1.4*s.brightness, inh=0.02*clamp((1-s.rigidity)*0.7+s.detail.bondOrderSpread*0.3);if(!domain::applyHarmonicSpectrum(primary,tilt,inh))return {s,{},{},"harmonic spectrum composition failed"};set(primary,"outputLevel",0.42+0.18*s.density);traceItem(items,"parameter","primary.partialSpectrum","M1-primary-harmonic",tilt,tilt);}
    else{set(primary,"carrierRatio",0.75+1.5*s.rigidity);set(primary,"modulatorRatio",0.5+3*s.detail.motifPlacement);set(primary,"index",0.5+4*s.roughness);set(primary,"outputLevel",0.42+0.18*s.density);}
    p.nodes.push_back(primary); traceItem(items,"node","primary","M1-primary",s.harmonicity,harmonic?1:0);
    if(secondary){auto n=node("secondary",harmonic?domain::ModuleType::fm:domain::ModuleType::harmonic);if(n.type==domain::ModuleType::harmonic){if(!domain::applyHarmonicSpectrum(n,-1.2+s.brightness,0.012*s.detail.heteroPlacement))return {s,{},{},"secondary spectrum composition failed"};}else{set(n,"modulatorRatio",1+3*s.detail.heteroPlacement);set(n,"index",1+3*s.motion);}set(n,"outputLevel",0.22+0.16*s.motion);p.nodes.push_back(n);traceItem(items,"node","secondary","M2-complement",s.motion,1);}
    if(noise){auto n=node("noise",domain::ModuleType::noise);set(n,"color",s.brightness<.5?1:0);set(n,"mode",s.decay<.45?1:0);set(n,"burstMs",30+300*s.decay);set(n,"outputLevel",.05+.18*s.roughness);p.nodes.push_back(n);traceItem(items,"node","noise","M2-noise",s.roughness,1);}
    const bool resonator=motif(a,"amide")||num(a.descriptors.getDynamicObject(),"ringCount")>0;
    if(resonator){auto n=node("resonator",domain::ModuleType::resonator);set(n,"mode",s.rigidity<.5?0:1);set(n,"tuneRatio",.65+1.4*s.density+1.5*s.detail.motifPlacement);set(n,"combFeedback",.2+.65*s.decay);set(n,"modalQ",2+8*s.rigidity);setArray(n,"modeRatios",{1.0,1.48+.24*s.detail.bondOrderMean,2.05+.35*s.detail.heteroPlacement,2.9+.5*s.detail.motifPlacement});setArray(n,"modeLevels",{1.0,.55+.2*s.brightness,.32+.18*s.detail.bondOrderSpread,.16+.16*s.motion});set(n,"outputLevel",.7);p.nodes.push_back(n);traceItem(items,"node","resonator","M3-resonator",s.rigidity,1);}
    const bool shaper=s.roughness>=.2||motif(a,"carbonyl"); if(shaper){auto n=node("shaper",domain::ModuleType::shaper);set(n,"drive",1+8*s.roughness+2*s.detail.bondOrderSpread);set(n,"wet",.2+.45*s.roughness);p.nodes.push_back(n);traceItem(items,"node","shaper","M4-shaper",s.roughness,1);}
    const bool filter=s.brightness>=.1||resonator; if(filter){auto n=node("filter",domain::ModuleType::filter);const int mode=s.brightness<.3?0:s.brightness<.65?1:2;set(n,"mode",mode);set(n,"cutoff",mode==0?300+8000*s.brightness*s.brightness:mode==1?400+5000*s.brightness:500+2500*s.brightness);set(n,"q",.7+4*s.harmonicity);p.nodes.push_back(n);traceItem(items,"node","filter","M4-filter",s.brightness,1);}
    std::vector<std::string> sources{"primary"};if(secondary)sources.push_back("secondary");if(noise)sources.push_back("noise");
    std::vector<std::string> processors;if(shaper&&filter){if(s.rigidity<.5)processors={"shaper","filter"};else processors={"filter","shaper"};}else if(shaper)processors={"shaper"};else if(filter)processors={"filter"};
    const std::string post=processors.empty()?"output":processors.front();
    for(const auto& src:sources){const auto destination=resonator?"resonator":post;p.edges.push_back({src,destination,src=="primary"?0.75:0.5});traceItem(items,"edge",src+"->"+destination,"M3/M4-routing",1,1);}
    if(resonator){p.edges.push_back({"resonator",post,.8});if(s.motion>=.25)for(const auto& src:sources)p.edges.push_back({src,post,src=="primary"?.3:.2});}
    for(std::size_t i=1;i<processors.size();++i)p.edges.push_back({processors[i-1],processors[i],.85});
    if(!processors.empty())p.edges.push_back({processors.back(),"output",.9});
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
