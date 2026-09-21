#include "iupac/engine/Engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{
using namespace iupac;
bool expect(bool v,const char* m){if(!v)std::cerr<<"module test failed: "<<m<<'\n';return v;}
bool finiteActive(const std::array<float,512>& x){return std::ranges::all_of(x,[](float v){return std::isfinite(v);})&&std::ranges::any_of(x,[](float v){return std::abs(v)>1e-5f;});}
}
bool runModuleTests()
{
    bool ok=true;std::array<float,512> zero{},impulse{},left{},right{};impulse[0]=1;
    engine::ModuleValues v;v.amplitudes[0]=1;for(std::size_t i=0;i<16;++i)v.ratios[i]=static_cast<float>(i+1);v.outputLevel=.5f;
    for(auto type:{domain::ModuleType::harmonic,domain::ModuleType::fm,domain::ModuleType::noise}){engine::ModuleProcessor m(type);m.prepare(96000,512);m.noteOn(60,1,123,456);m.process(v,261.6256f,zero,zero,left,right);ok&=expect(finiteActive(left),"source renders finite signal");}
    ok&=expect(engine::internalSampleRate(48000)==96000,"fixed two-times internal rate");
    engine::ModuleProcessor noise(domain::ModuleType::noise);noise.prepare(96000,512);noise.noteOn(60,2,42,7);noise.process(v,440,zero,zero,left,right);auto first=left;noise.reset();noise.noteOn(60,2,42,7);noise.process(v,440,zero,zero,left,right);ok&=expect(left==first,"noise reset deterministic");
    v.mode=1;v.color=1;v.burstMilliseconds=1;noise.reset();noise.noteOn(60,2,42,7);for(int i=0;i<20;++i)noise.process(v,440,zero,zero,left,right);ok&=expect(std::ranges::all_of(left,[](float x){return std::abs(x)<1e-5f;}),"pink burst reaches silence");
    v.outputLevel=1;v.mode=0;v.combFeedback=.97f;engine::ModuleProcessor resonator(domain::ModuleType::resonator);resonator.prepare(192000,512);resonator.noteOn(0,1,1,1);resonator.process(v,8.1758f,impulse,impulse,left,right);ok&=expect(std::ranges::all_of(left,[](float x){return std::isfinite(x);}),"worst-case comb finite");
    v.mode=1;v.modeLevels={1,.5f,.25f,.125f};resonator.process(v,440,impulse,impulse,left,right);ok&=expect(finiteActive(left),"modal resonator renders");
    engine::ModuleProcessor filter(domain::ModuleType::filter);filter.prepare(88200,512);v.cutoff=999999;v.q=999;filter.process(v,440,impulse,impulse,left,right);ok&=expect(std::ranges::all_of(left,[](float x){return std::isfinite(x);}),"filter clamps controls");
    engine::ModuleProcessor shaper(domain::ModuleType::shaper);shaper.prepare(96000,512);v.drive=16;v.wet=1;shaper.process(v,440,impulse,impulse,left,right);ok&=expect(std::abs(left[0])<=1,"shaper bounded");
    engine::ModuleProcessor mixer(domain::ModuleType::mixer);mixer.prepare(96000,512);v.level=.5f;v.pan=0;mixer.process(v,440,impulse,impulse,left,right);ok&=expect(left[0]>0&&right[0]>0,"mixer stereo output");
    // --- D8 unison, phase randomization and drift (#120) ---------------------------------------
    // Everything here goes through the production ModuleProcessor at its real control values.
    auto renderSource=[&](domain::ModuleType type,const engine::ModuleValues& values,int note,std::uint32_t seed,int blocks=1){
        engine::ModuleProcessor m(type);m.prepare(96000,512);m.noteOn(note,1,seed,456);
        std::array<float,512> zl{},zr{},ol{},orr{};std::array<std::array<float,512>,2> last{};
        for(int b=0;b<blocks;++b){m.process(values,261.6256f,zl,zr,ol,orr);last={ol,orr};}
        return last;};
    for(auto type:{domain::ModuleType::harmonic,domain::ModuleType::fm})
    {
        engine::ModuleValues one;one.amplitudes[0]=1;for(std::size_t i=0;i<16;++i)one.ratios[i]=static_cast<float>(i+1);
        // One copy with no phase randomization is the exact pre-D8 signal: `detuneCents` and
        // `unisonSpread` are inert there, whatever they are set to, and both channels are untouched
        // by the unison pan law. The production-path render equality against a real pre-D8 patch is
        // the `patch-compatibility` ctest; this pins the module in isolation.
        const auto reference=renderSource(type,one,60,123);
        auto inert=one;inert.detuneCents=50;inert.unisonSpread=1;inert.drift=0;
        const auto unchanged=renderSource(type,inert,60,123);
        ok&=expect(unchanged[0]==reference[0]&&unchanged[1]==reference[1],"one unison copy ignores detune and spread bit for bit");
        // Seven copies: audible change, still finite, and the spread has pulled the channels apart.
        auto stack=one;stack.unisonVoices=7;stack.detuneCents=20;stack.unisonSpread=1;
        const auto wide=renderSource(type,stack,60,123);
        ok&=expect(finiteActive(wide[0])&&finiteActive(wide[1]),"seven-copy unison stack renders finite signal");
        ok&=expect(wide[0]!=reference[0],"unison changes the rendered signal");
        ok&=expect(wide[0]!=wide[1],"unison spread decorrelates the channels");
        auto centred=stack;centred.unisonSpread=0;
        const auto mono=renderSource(type,centred,60,123);
        ok&=expect(mono[0]==mono[1],"zero unison spread keeps the stack centred");
        // Phase randomization and drift come from the patch-seeded stream: identical inputs render
        // identical samples, a different seed or note renders different ones, and nothing reaches
        // for the wall clock. `blocks` > 1 lets the block-rate drift wander actually move.
        auto moving=stack;moving.phaseRandom=1;moving.drift=1;
        const auto a=renderSource(type,moving,60,123,64),b=renderSource(type,moving,60,123,64);
        ok&=expect(a[0]==b[0]&&a[1]==b[1],"unison phase and drift are deterministic for a seed");
        ok&=expect(renderSource(type,moving,60,999,64)[0]!=a[0],"a different patch seed draws different phases");
        ok&=expect(renderSource(type,moving,61,123,64)[0]!=a[0],"a different note draws different phases");
        ok&=expect(finiteActive(a[0]),"drifting unison stack stays finite");
        // Drift alone, with one copy, still wanders — and is exactly inert at zero.
        auto drifting=one;drifting.drift=1;
        ok&=expect(renderSource(type,drifting,60,123,64)[0]!=renderSource(type,one,60,123,64)[0],"drift wanders the single-copy source");
        auto still=one;still.drift=0;still.phaseRandom=0;
        ok&=expect(renderSource(type,still,60,123,64)[0]==renderSource(type,one,60,123,64)[0],"zero drift leaves the source untouched");
    }
    engine::ModulatorBank mods;mods.prepare(48000);std::array<domain::Envelope,domain::envelopeCount> es{};std::array<domain::Lfo,domain::lfoCount> ls{};ls[0].waveform=domain::LfoWaveform::triangle;mods.configure(es,ls);mods.noteOn();auto a=mods.next();ok&=expect(a[0]>=0&&a[3]>=-1&&a[3]<=1,"ADSR and LFO run");mods.noteOff();
    return ok;
}
