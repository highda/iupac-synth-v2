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
float fastSin(double phase) noexcept {while(phase>std::numbers::pi_v<double>)phase-=twoPi;while(phase<-std::numbers::pi_v<double>)phase+=twoPi;return juce::dsp::FastMathApproximations::sin(static_cast<float>(phase));}
float fastTanh(float value)noexcept{if(value>=5)return 1;if(value<=-5)return-1;return juce::dsp::FastMathApproximations::tanh(value);}
}
float ModuleProcessor::clampFinite(float v,float lo,float hi,float fallback) noexcept { return std::isfinite(v)?std::clamp(v,lo,hi):fallback; }
void ModuleProcessor::prepare(double rate,std::size_t block)
{
    sampleRate_=std::max(1.0,rate); juce::dsp::ProcessSpec spec{sampleRate_,static_cast<juce::uint32>(std::min(block,maximumModuleBlockSize)),2};
    if(type_==domain::ModuleType::resonator&&!comb_){ownedComb_=std::make_unique<CombDelay>(110000);ownedComb_->prepare(spec);comb_=ownedComb_.get();}filter_.prepare(spec);for(auto& m:modes_)m.prepare(spec);reset();
}
void ModuleProcessor::reset() noexcept {phases_.fill(0);harmonicSin_.fill(0);harmonicCos_.fill(1);cachedRatios_.fill(-1);cachedHarmonicFundamental_=-1;harmonicRenormalizeCountdown_=4096;modPhase_=0;pink_.fill(0);burstSamplesRemaining_=0;gate_=false;filterControlCountdown_=0;cachedPan_=cachedFundamental_=cachedFilterCutoff_=cachedFilterQ_=cachedModalQ_=std::numeric_limits<float>::quiet_NaN();cachedModeCutoffs_.fill(-1);cachedPans_.fill(std::numeric_limits<float>::quiet_NaN());if(comb_)comb_->reset();filter_.reset();for(auto& m:modes_)m.reset();}
void ModuleProcessor::noteOn(int note,int channel,std::uint32_t seed,std::uint32_t nodeHash) noexcept
{
    phases_.fill(0);modPhase_=0;pink_.fill(0);gate_=true;randomState_=seed^(nodeHash*0x9e3779b9u)^(static_cast<std::uint32_t>(note)<<16u)^(static_cast<std::uint32_t>(channel)*0x85ebca6bu);if(!randomState_)randomState_=1;burstSamplesRemaining_=std::numeric_limits<std::size_t>::max();
}
float ModuleProcessor::nextNoise() noexcept { randomState_^=randomState_<<13u;randomState_^=randomState_>>17u;randomState_^=randomState_<<5u;return static_cast<float>((randomState_>>8u)*(2.0/16777215.0)-1.0); }
void ModuleProcessor::process(const ModuleValues& p,float fundamental,std::span<const float> inL,std::span<const float> inR,std::span<float> outL,std::span<float> outR) noexcept
{
    const auto count=std::min({inL.size(),inR.size(),outL.size(),outR.size(),maximumModuleBlockSize});fundamental=clampFinite(fundamental,1,static_cast<float>(sampleRate_*.45),440);const auto output=clampFinite(p.outputLevel,0,1,0);
    if(type_==domain::ModuleType::filter&&filterControlCountdown_--==0){filterControlCountdown_=15;const auto cutoff=std::min(clampFinite(p.cutoff,30,18000,1000),static_cast<float>(sampleRate_*.2)),q=clampFinite(p.q,.5f,8,.707f);if(cutoff!=cachedFilterCutoff_){filter_.setCutoffFrequency(cutoff);cachedFilterCutoff_=cutoff;}if(q!=cachedFilterQ_){filter_.setResonance(q);cachedFilterQ_=q;}filter_.setType(p.mode==1?juce::dsp::StateVariableTPTFilterType::bandpass:p.mode==2?juce::dsp::StateVariableTPTFilterType::highpass:juce::dsp::StateVariableTPTFilterType::lowpass);}
    if(type_==domain::ModuleType::harmonic)for(std::size_t n=0;n<16;++n)if(p.pans[n]!=cachedPans_[n]){cachedPans_[n]=p.pans[n];panLeft_[n]=panGain(p.pans[n],false);panRight_[n]=panGain(p.pans[n],true);}
    if(type_==domain::ModuleType::harmonic)for(std::size_t n=0;n<16;++n){const auto ratio=clampFinite(p.ratios[n],.5f,32,static_cast<float>(n+1));if(fundamental!=cachedHarmonicFundamental_||ratio!=cachedRatios_[n]){cachedRatios_[n]=ratio;const auto delta=twoPi*fundamental*ratio/sampleRate_;harmonicDeltaSin_[n]=static_cast<float>(std::sin(delta));harmonicDeltaCos_[n]=static_cast<float>(std::cos(delta));}}if(type_==domain::ModuleType::harmonic)cachedHarmonicFundamental_=fundamental;
    if(type_==domain::ModuleType::mixer&&p.pan!=cachedPan_){cachedPan_=p.pan;cachedPanLeft_=panGain(p.pan,false);cachedPanRight_=panGain(p.pan,true);}
    if(type_==domain::ModuleType::fm&&fundamental!=cachedFundamental_){cachedFundamental_=fundamental;const auto midi=69+12*std::log2(fundamental/440);cachedFmRolloff_=midi>84?std::max(0.0f,1-(midi-84)/43):1.f;}
    for(std::size_t i=0;i<count;++i){float l=inL[i],r=inR[i];
        if(type_==domain::ModuleType::harmonic){l=r=0;for(std::size_t n=0;n<16;++n){const auto hz=fundamental*cachedRatios_[n];if(hz>sampleRate_*.45)continue;const auto s=harmonicSin_[n]*clampFinite(p.amplitudes[n],0,1,0);l+=s*panLeft_[n];r+=s*panRight_[n];const auto nextSin=harmonicSin_[n]*harmonicDeltaCos_[n]+harmonicCos_[n]*harmonicDeltaSin_[n];harmonicCos_[n]=harmonicCos_[n]*harmonicDeltaCos_[n]-harmonicSin_[n]*harmonicDeltaSin_[n];harmonicSin_[n]=nextSin;}if(--harmonicRenormalizeCountdown_==0){harmonicRenormalizeCountdown_=4096;for(std::size_t n=0;n<16;++n){const auto magnitude=std::sqrt(harmonicSin_[n]*harmonicSin_[n]+harmonicCos_[n]*harmonicCos_[n]);if(magnitude>0){harmonicSin_[n]/=magnitude;harmonicCos_[n]/=magnitude;}}}}
        else if(type_==domain::ModuleType::fm){const auto cr=clampFinite(p.carrierRatio,.5f,4,1),mr=clampFinite(p.modulatorRatio,.25f,8,1);const auto index=clampFinite(p.index,0,6,0)*cachedFmRolloff_;const auto s=fastSin(phases_[0]+index*fastSin(modPhase_));l=r=s;phases_[0]+=twoPi*fundamental*cr/sampleRate_;modPhase_+=twoPi*fundamental*mr/sampleRate_;if(phases_[0]>std::numbers::pi_v<double>)phases_[0]-=twoPi;if(modPhase_>std::numbers::pi_v<double>)modPhase_-=twoPi;}
        else if(type_==domain::ModuleType::noise){const auto burst=static_cast<std::size_t>(sampleRate_*clampFinite(p.burstMilliseconds,1,500,80)*.001);if(p.mode==1&&burstSamplesRemaining_==std::numeric_limits<std::size_t>::max())burstSamplesRemaining_=burst;auto s=(gate_&&(p.mode==0||burstSamplesRemaining_>0))?nextNoise():0.0f;if(p.mode==1&&burstSamplesRemaining_>0)--burstSamplesRemaining_;if(p.color==1){pink_[0]=.99765f*pink_[0]+.099046f*s;pink_[1]=.963f*pink_[1]+.2965164f*s;s=.35f*(pink_[0]+pink_[1]+s*.1848f);}l=r=s;}
        else if(type_==domain::ModuleType::resonator){const auto hz=fundamental*clampFinite(p.tuneRatio,.5f,4,1);if(p.mode==0){comb_->setDelay(std::clamp(static_cast<float>(sampleRate_/hz),1.0f,static_cast<float>(comb_->getMaximumDelayInSamples())));const auto fb=clampFinite(p.combFeedback,0,.97f,.4f),dl=comb_->popSample(0),dr=comb_->popSample(1);comb_->pushSample(0,l+dl*fb);comb_->pushSample(1,r+dr*fb);l=dl;r=dr;}else{float sl=0,sr=0;const auto q=clampFinite(p.modalQ,.5f,12,3);for(std::size_t m=0;m<4;++m){const auto cutoff=std::min(hz*clampFinite(p.modeRatios[m],.5f,4,1),static_cast<float>(sampleRate_*.4));modes_[m].setType(juce::dsp::StateVariableTPTFilterType::bandpass);if(cutoff!=cachedModeCutoffs_[m]){modes_[m].setCutoffFrequency(cutoff);cachedModeCutoffs_[m]=cutoff;}if(q!=cachedModalQ_)modes_[m].setResonance(q);const auto g=clampFinite(p.modeLevels[m],0,1,0);sl+=modes_[m].processSample(0,l)*g;sr+=modes_[m].processSample(1,r)*g;}cachedModalQ_=q;l=sl;r=sr;}}
        else if(type_==domain::ModuleType::filter){l=filter_.processSample(0,l);r=filter_.processSample(1,r);}
        else if(type_==domain::ModuleType::shaper){const auto d=clampFinite(p.drive,1,16,1),w=clampFinite(p.wet,0,1,1);l=std::lerp(l,fastTanh(l*d),w);r=std::lerp(r,fastTanh(r*d),w);}
        else{const auto g=clampFinite(p.level,0,1,1);l*=g*cachedPanLeft_;r*=g*cachedPanRight_;}outL[i]=std::isfinite(l)?l*output:0;outR[i]=std::isfinite(r)?r*output:0;}
}
void ModulatorBank::prepare(double rate) noexcept {sampleRate_=std::max(1.0,rate);smoothingLength_=static_cast<std::uint64_t>(std::max(1.0,sampleRate_*.020));for(auto& e:envelopes_)e.setSampleRate(sampleRate_);}
void ModulatorBank::configure(const std::array<domain::Envelope,3>& es,const std::array<domain::Lfo,2>& ls) noexcept {previousLfoSettings_=lfoSettings_;lfoSettings_=ls;smoothingSample_=0;for(std::size_t i=0;i<3;++i){juce::ADSR::Parameters p;p.attack=static_cast<float>(std::clamp(es[i].attack,.001,2.0));p.decay=static_cast<float>(std::clamp(es[i].decay,.01,4.0));p.sustain=static_cast<float>(std::clamp(es[i].sustain,0.0,1.0));p.release=static_cast<float>(std::clamp(es[i].release,.02,6.0));envelopes_[i].setParameters(p);}}
void ModulatorBank::reset() noexcept {lfoPhases_.fill(0);for(auto&e:envelopes_)e.reset();}
void ModulatorBank::noteOn() noexcept {lfoPhases_.fill(0);for(auto& e:envelopes_)e.noteOn();}void ModulatorBank::noteOff() noexcept {for(auto& e:envelopes_)e.noteOff();}
std::array<float,5> ModulatorBank::next() noexcept {std::array<float,5> r{};for(std::size_t i=0;i<3;++i)r[i]=envelopes_[i].getNextSample();const float x=smoothingSample_>=smoothingLength_?1.f:static_cast<float>(smoothingSample_)/static_cast<float>(smoothingLength_);for(std::size_t i=0;i<2;++i){const auto ph=lfoPhases_[i];const auto wave=[ph](domain::LfoWaveform waveform){if(waveform==domain::LfoWaveform::sine)return fastSin(ph);const auto scaled=static_cast<float>(2*ph/std::numbers::pi_v<double>);return ph>std::numbers::pi_v<double>*.5?2-scaled:ph<-std::numbers::pi_v<double>*.5?-2-scaled:scaled;};r[3+i]=std::lerp(wave(previousLfoSettings_[i].waveform),wave(lfoSettings_[i].waveform),x);const auto rate=std::lerp(previousLfoSettings_[i].rate,lfoSettings_[i].rate,static_cast<double>(x));lfoPhases_[i]=ph+twoPi*std::clamp(rate,.05,12.0)/sampleRate_;if(lfoPhases_[i]>std::numbers::pi_v<double>)lfoPhases_[i]-=twoPi;}if(smoothingSample_<smoothingLength_)++smoothingSample_;return r;}
}
