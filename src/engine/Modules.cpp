#include "iupac/engine/Engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace iupac::engine
{
namespace
{
constexpr double twoPi = std::numbers::pi_v<double> * 2.0;
float panGain(float pan, bool right) noexcept { const auto a=(std::clamp(pan,-1.0f,1.0f)+1.0f)*std::numbers::pi_v<float>*.25f; return right?std::sin(a):std::cos(a); }
}
float ModuleProcessor::clampFinite(float v,float lo,float hi,float fallback) noexcept { return std::isfinite(v)?std::clamp(v,lo,hi):fallback; }
void ModuleProcessor::prepare(double rate,std::size_t block)
{
    sampleRate_=std::max(1.0,rate); juce::dsp::ProcessSpec spec{sampleRate_,static_cast<juce::uint32>(std::min(block,maximumModuleBlockSize)),2};
    if(type_==domain::ModuleType::resonator){comb_=std::make_unique<juce::dsp::DelayLine<float,juce::dsp::DelayLineInterpolationTypes::Linear>>(110000);comb_->prepare(spec);}filter_.prepare(spec);for(auto& m:modes_)m.prepare(spec);reset();
}
void ModuleProcessor::reset() noexcept {phases_.fill(0);modPhase_=0;pink_.fill(0);burstSamplesRemaining_=0;gate_=false;if(comb_)comb_->reset();filter_.reset();for(auto& m:modes_)m.reset();}
void ModuleProcessor::noteOn(int note,int channel,std::uint32_t seed,std::uint32_t nodeHash) noexcept
{
    phases_.fill(0);modPhase_=0;pink_.fill(0);gate_=true;randomState_=seed^(nodeHash*0x9e3779b9u)^(static_cast<std::uint32_t>(note)<<16u)^(static_cast<std::uint32_t>(channel)*0x85ebca6bu);if(!randomState_)randomState_=1;burstSamplesRemaining_=std::numeric_limits<std::size_t>::max();
}
float ModuleProcessor::nextNoise() noexcept { randomState_^=randomState_<<13u;randomState_^=randomState_>>17u;randomState_^=randomState_<<5u;return static_cast<float>((randomState_>>8u)*(2.0/16777215.0)-1.0); }
void ModuleProcessor::process(const ModuleValues& p,float fundamental,std::span<const float> inL,std::span<const float> inR,std::span<float> outL,std::span<float> outR) noexcept
{
    const auto count=std::min({inL.size(),inR.size(),outL.size(),outR.size(),maximumModuleBlockSize});fundamental=clampFinite(fundamental,1,static_cast<float>(sampleRate_*.45),440);const auto output=clampFinite(p.outputLevel,0,1,0);
    if(type_==domain::ModuleType::filter){filter_.setCutoffFrequency(std::min(clampFinite(p.cutoff,30,18000,1000),static_cast<float>(sampleRate_*.4)));filter_.setResonance(clampFinite(p.q,.5f,8,.707f));filter_.setType(p.mode==1?juce::dsp::StateVariableTPTFilterType::bandpass:p.mode==2?juce::dsp::StateVariableTPTFilterType::highpass:juce::dsp::StateVariableTPTFilterType::lowpass);}
    for(std::size_t i=0;i<count;++i){float l=inL[i],r=inR[i];
        if(type_==domain::ModuleType::harmonic){l=r=0;for(std::size_t n=0;n<16;++n){const auto ratio=clampFinite(p.ratios[n],.5f,32,static_cast<float>(n+1)),hz=fundamental*ratio;if(hz>sampleRate_*.45)continue;const auto s=static_cast<float>(std::sin(phases_[n]))*clampFinite(p.amplitudes[n],0,1,0);l+=s*panGain(p.pans[n],false);r+=s*panGain(p.pans[n],true);phases_[n]=std::fmod(phases_[n]+twoPi*hz/sampleRate_,twoPi);}}
        else if(type_==domain::ModuleType::fm){const auto cr=clampFinite(p.carrierRatio,.5f,4,1),mr=clampFinite(p.modulatorRatio,.25f,8,1);auto index=clampFinite(p.index,0,6,0);const auto midi=69+12*std::log2(fundamental/440);if(midi>84)index*=std::max(0.0f,1-(midi-84)/43);const auto s=static_cast<float>(std::sin(phases_[0]+index*std::sin(modPhase_)));l=r=s;phases_[0]=std::fmod(phases_[0]+twoPi*fundamental*cr/sampleRate_,twoPi);modPhase_=std::fmod(modPhase_+twoPi*fundamental*mr/sampleRate_,twoPi);}
        else if(type_==domain::ModuleType::noise){const auto burst=static_cast<std::size_t>(sampleRate_*clampFinite(p.burstMilliseconds,1,500,80)*.001);if(p.mode==1&&burstSamplesRemaining_==std::numeric_limits<std::size_t>::max())burstSamplesRemaining_=burst;auto s=(gate_&&(p.mode==0||burstSamplesRemaining_>0))?nextNoise():0.0f;if(p.mode==1&&burstSamplesRemaining_>0)--burstSamplesRemaining_;if(p.color==1){pink_[0]=.99765f*pink_[0]+.099046f*s;pink_[1]=.963f*pink_[1]+.2965164f*s;s=.35f*(pink_[0]+pink_[1]+s*.1848f);}l=r=s;}
        else if(type_==domain::ModuleType::resonator){const auto hz=fundamental*clampFinite(p.tuneRatio,.5f,4,1);if(p.mode==0){comb_->setDelay(std::clamp(static_cast<float>(sampleRate_/hz),1.0f,static_cast<float>(comb_->getMaximumDelayInSamples())));const auto fb=clampFinite(p.combFeedback,0,.97f,.4f),dl=comb_->popSample(0),dr=comb_->popSample(1);comb_->pushSample(0,l+dl*fb);comb_->pushSample(1,r+dr*fb);l=dl;r=dr;}else{float sl=0,sr=0;for(std::size_t m=0;m<4;++m){modes_[m].setType(juce::dsp::StateVariableTPTFilterType::bandpass);modes_[m].setCutoffFrequency(std::min(hz*clampFinite(p.modeRatios[m],.5f,4,1),static_cast<float>(sampleRate_*.4)));modes_[m].setResonance(clampFinite(p.modalQ,.5f,12,3));const auto g=clampFinite(p.modeLevels[m],0,1,0);sl+=modes_[m].processSample(0,l)*g;sr+=modes_[m].processSample(1,r)*g;}l=sl;r=sr;}}
        else if(type_==domain::ModuleType::filter){l=filter_.processSample(0,l);r=filter_.processSample(1,r);}
        else if(type_==domain::ModuleType::shaper){const auto d=clampFinite(p.drive,1,16,1),w=clampFinite(p.wet,0,1,1);l=std::lerp(l,std::tanh(l*d),w);r=std::lerp(r,std::tanh(r*d),w);}
        else{const auto g=clampFinite(p.level,0,1,1);l*=g*panGain(p.pan,false);r*=g*panGain(p.pan,true);}outL[i]=std::isfinite(l)?l*output:0;outR[i]=std::isfinite(r)?r*output:0;}
}
void ModulatorBank::prepare(double rate) noexcept {sampleRate_=std::max(1.0,rate);for(auto& e:envelopes_)e.setSampleRate(sampleRate_);}
void ModulatorBank::configure(const std::array<domain::Envelope,3>& es,const std::array<domain::Lfo,2>& ls) noexcept {lfoSettings_=ls;for(std::size_t i=0;i<3;++i){juce::ADSR::Parameters p;p.attack=static_cast<float>(std::clamp(es[i].attack,.001,2.0));p.decay=static_cast<float>(std::clamp(es[i].decay,.01,4.0));p.sustain=static_cast<float>(std::clamp(es[i].sustain,0.0,1.0));p.release=static_cast<float>(std::clamp(es[i].release,.02,6.0));envelopes_[i].setParameters(p);}}
void ModulatorBank::reset() noexcept {lfoPhases_.fill(0);for(auto&e:envelopes_)e.reset();}
void ModulatorBank::noteOn() noexcept {lfoPhases_.fill(0);for(auto& e:envelopes_)e.noteOn();}void ModulatorBank::noteOff() noexcept {for(auto& e:envelopes_)e.noteOff();}
std::array<float,5> ModulatorBank::next() noexcept {std::array<float,5> r{};for(std::size_t i=0;i<3;++i)r[i]=envelopes_[i].getNextSample();for(std::size_t i=0;i<2;++i){const auto ph=lfoPhases_[i];r[3+i]=lfoSettings_[i].waveform==domain::LfoWaveform::sine?static_cast<float>(std::sin(ph)):static_cast<float>(2/std::numbers::pi*std::asin(std::sin(ph)));lfoPhases_[i]=std::fmod(ph+twoPi*std::clamp(lfoSettings_[i].rate,.05,12.0)/sampleRate_,twoPi);}return r;}
}
