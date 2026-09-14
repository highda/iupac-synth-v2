#include "iupac/engine/Engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
    engine::ModulatorBank mods;mods.prepare(48000);std::array<domain::Envelope,3> es{};std::array<domain::Lfo,2> ls{};ls[0].waveform=domain::LfoWaveform::triangle;mods.configure(es,ls);mods.noteOn();auto a=mods.next();ok&=expect(a[0]>=0&&a[3]>=-1&&a[3]<=1,"ADSR and LFO run");mods.noteOff();
    return ok;
}
