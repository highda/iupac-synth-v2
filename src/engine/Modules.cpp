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
// D9 delegates the drift shape. At `drift` 1 the pitch wanders +/- this many cents and the level
// +/- this fraction, slowly enough (well under a hertz) to read as an analogue instability rather
// than vibrato or tremolo.
constexpr float driftCentsRange = 14.0f;
constexpr float driftLevelRange = 0.09f;
float panGain(float pan, bool right) noexcept { const auto a=(std::clamp(pan,-1.0f,1.0f)+1.0f)*std::numbers::pi_v<float>*.25f; return right?std::sin(a):std::cos(a); }
float fastSin(double phase) noexcept {while(phase>std::numbers::pi_v<double>)phase-=twoPi;while(phase<-std::numbers::pi_v<double>)phase+=twoPi;return juce::dsp::FastMathApproximations::sin(static_cast<float>(phase));}
// Unit triangle over the same [-pi, pi] phase the sine uses: 0 at 0, +1 at pi/2, -1 at -pi/2.
float triangleWave(double phase) noexcept {const auto scaled=static_cast<float>(2*phase/std::numbers::pi_v<double>);return phase>std::numbers::pi_v<double>*.5?2-scaled:phase<-std::numbers::pi_v<double>*.5?-2-scaled:scaled;}
// D8 pitch block (#122): `keytrack` scales the note's distance from this reference note, so 0 pins
// a source to one pitch and 1 is the ordinary keyboard. C4 is the reference the mapper already
// treats as the instrument centre (`keyTracking` is (note - 60) / 36 in the matrix).
constexpr int keytrackReferenceNote = 60;
float fastTanh(float value)noexcept{if(value>=5)return 1;if(value<=-5)return-1;return juce::dsp::FastMathApproximations::tanh(value);}
}
float ModuleProcessor::clampFinite(float v,float lo,float hi,float fallback) noexcept { return std::isfinite(v)?std::clamp(v,lo,hi):fallback; }
void ModuleProcessor::prepare(double rate,std::size_t block)
{
    sampleRate_=std::max(1.0,rate); juce::dsp::ProcessSpec spec{sampleRate_,static_cast<juce::uint32>(std::min(block,maximumModuleBlockSize)),2};
    if(type_==domain::ModuleType::resonator&&!comb_){ownedComb_=std::make_unique<CombDelay>(110000);ownedComb_->prepare(spec);comb_=ownedComb_.get();}filter_.prepare(spec);for(auto& m:modes_)m.prepare(spec);reset();
}
void ModuleProcessor::reset() noexcept {phases_.fill(0);modPhases_.fill(0);for(auto& c:harmonicSin_)c.fill(0);for(auto& c:harmonicCos_)c.fill(1);cachedRatios_.fill(-1);cachedHarmonicFundamental_=-1;harmonicRenormalizeCountdown_=4096;pink_.fill(0);burstSamplesRemaining_=0;gate_=false;filterControlCountdown_=0;cachedPan_=cachedFundamental_=cachedFilterCutoff_=cachedFilterQ_=cachedModalQ_=std::numeric_limits<float>::quiet_NaN();cachedModeCutoffs_.fill(-1);cachedPans_.fill(std::numeric_limits<float>::quiet_NaN());
    unisonPhase_.fill(0);unisonModPhase_.fill(0);cachedDetuneCents_=cachedUnisonSpread_=std::numeric_limits<float>::quiet_NaN();cachedUnisonVoices_=0;unisonDirty_=true;pendingPhaseOffset_=false;
    driftPhase_=driftLevelPhase_=0;driftRate_=.11;driftLevelRate_=.07;
    if(comb_)comb_->reset();filter_.reset();for(auto& m:modes_)m.reset();}
void ModuleProcessor::noteOn(int note,int channel,std::uint32_t seed,std::uint32_t nodeHash) noexcept
{
    phases_.fill(0);modPhases_.fill(0);for(auto& c:harmonicSin_)c.fill(0);for(auto& c:harmonicCos_)c.fill(1);pink_.fill(0);gate_=true;note_=note;randomState_=seed^(nodeHash*0x9e3779b9u)^(static_cast<std::uint32_t>(note)<<16u)^(static_cast<std::uint32_t>(channel)*0x85ebca6bu);if(!randomState_)randomState_=1;burstSamplesRemaining_=std::numeric_limits<std::size_t>::max();
    // D8: the unison start phases and the two drift wanders are drawn here, once, from the
    // patch-seeded stream this voice already owns — never from the wall clock, so V2 reproducibility
    // holds. Only the three unison/drift-capable source types draw, so the noise stream is untouched
    // and a pre-D8 noise node still renders the identical sequence. The draws happen whatever
    // `unisonVoices` is, so the stream does not depend on the copy count either.
    if(type_==domain::ModuleType::harmonic||type_==domain::ModuleType::fm||type_==domain::ModuleType::sub)
    {
        const auto unit=[this]{return (nextNoise()+1.0f)*.5f;};
        for(auto& v:unisonPhase_)v=unit();for(auto& v:unisonModPhase_)v=unit();
        driftRate_=.06+.16*unit();driftLevelRate_=.04+.11*unit();driftPhase_=twoPi*unit();driftLevelPhase_=twoPi*unit();pendingPhaseOffset_=true;
    }
}
float ModuleProcessor::nextNoise() noexcept { randomState_^=randomState_<<13u;randomState_^=randomState_>>17u;randomState_^=randomState_<<5u;return static_cast<float>((randomState_>>8u)*(2.0/16777215.0)-1.0); }
void ModuleProcessor::updateUnison(int voices,float detuneCents,float spread) noexcept
{
    const auto cents=clampFinite(detuneCents,0,50,12),width=clampFinite(spread,0,1,.5f);
    if(voices==cachedUnisonVoices_&&cents==cachedDetuneCents_&&width==cachedUnisonSpread_)return;
    cachedUnisonVoices_=static_cast<std::uint8_t>(voices);cachedDetuneCents_=cents;cachedUnisonSpread_=width;unisonDirty_=true;
    // One copy is the literal identity: ratio 1 and gain 1 on both channels, so every product below
    // collapses to the exact pre-D8 arithmetic. Two or more copies take evenly spaced positions in
    // [-1, 1] — symmetric about the note, with a centre copy for odd counts — and equal-power pan
    // gains scaled by sqrt(2)/sqrt(voices), which keeps a centred stack at the one-copy level.
    const auto gain=voices==1?1.0f:std::numbers::sqrt2_v<float>/std::sqrt(static_cast<float>(voices));
    for(int c=0;c<voices;++c)
    {
        const auto position=voices==1?0.0f:2.0f*static_cast<float>(c)/static_cast<float>(voices-1)-1.0f;
        detuneMultiplier_[static_cast<std::size_t>(c)]=voices==1?1.0f:std::exp2(position*cents/1200.0f);
        unisonPanLeft_[static_cast<std::size_t>(c)]=voices==1?1.0f:gain*panGain(position*width,false);
        unisonPanRight_[static_cast<std::size_t>(c)]=voices==1?1.0f:gain*panGain(position*width,true);
    }
}
void ModuleProcessor::process(const ModuleValues& p,float fundamental,std::span<const float> inL,std::span<const float> inR,std::span<float> outL,std::span<float> outR) noexcept
{
    const auto count=std::min({inL.size(),inR.size(),outL.size(),outR.size(),maximumModuleBlockSize});fundamental=clampFinite(fundamental,1,static_cast<float>(sampleRate_*.45),440);auto output=clampFinite(p.outputLevel,0,1,0);
    const bool unisonSource=type_==domain::ModuleType::harmonic||type_==domain::ModuleType::fm;
    const bool pitchedSource=unisonSource||type_==domain::ModuleType::sub;
    const auto copies=unisonSource?std::clamp(p.unisonVoices,1,static_cast<int>(maximumUnisonVoices)):1;
    // D8 pitch block (#122). `octave`, `coarse` and `fine` transpose the source away from the voice
    // fundamental and `keytrack` scales how much of the note it follows, all as one semitone offset.
    // At the catalog defaults (0 / 0 / 0 / 1) that offset is literally 0 and the branch below does
    // not run, so a pre-D8 patch keeps its exact samples. masterTune and bend are already inside
    // `fundamental`, so a transposed or partly keytracked source still follows both — and so does
    // the sub oscillator, whose own -1/-2 octave is just this same offset.
    if(pitchedSource)
    {
        if(p.octave!=cachedPitchOctave_||p.coarse!=cachedPitchCoarse_||p.fine!=cachedPitchFine_||p.keytrack!=cachedPitchKeytrack_||note_!=cachedPitchNote_)
        {
            cachedPitchOctave_=p.octave;cachedPitchCoarse_=p.coarse;cachedPitchFine_=p.fine;cachedPitchKeytrack_=p.keytrack;cachedPitchNote_=note_;
            const auto semitones=12.0f*static_cast<float>(std::clamp(p.octave,-3,3))+static_cast<float>(std::clamp(p.coarse,-12,12))
                                +clampFinite(p.fine,-100,100,0)*.01f
                                +(clampFinite(p.keytrack,0,1,1)-1.0f)*static_cast<float>(note_-keytrackReferenceNote);
            cachedPitchMultiplier_=semitones==0.0f?1.0f:std::exp2(semitones/12.0f);
        }
        if(cachedPitchMultiplier_!=1.0f)fundamental=clampFinite(fundamental*cachedPitchMultiplier_,1,static_cast<float>(sampleRate_*.45),440);
        updateUnison(copies,p.detuneCents,p.unisonSpread);
        // `drift` (D9: a pair of slow sines, rate and start phase drawn per note-on) wanders pitch
        // and level around the note. Both multipliers are literally 1 at drift 0 — exp2(0) and
        // 1 + 0 — so a pre-D8 patch multiplies by one and keeps its exact samples. The wanders step
        // once per block: at a fraction of a hertz that is inaudibly coarse and costs nothing.
        // Both wanders are multiplied by `amount`, so at drift 0 the two sines are multiplied away
        // and skipping them is bit for bit the same signal — exp2(0) is 1 and 1 + 0 is 1 exactly.
        // The engine calls process() once per sample, so that is two transcendentals per sample per
        // pitched node that a patch with drift off was paying for nothing (#120, measured in #122).
        const auto amount=clampFinite(p.drift,0,1,0);
        if(amount>0.0f)
        {
            fundamental*=std::exp2(amount*driftCentsRange*fastSin(driftPhase_)/1200.0f);
            output*=1.0f+amount*driftLevelRange*fastSin(driftLevelPhase_);
        }
        const auto step=twoPi*static_cast<double>(count)/sampleRate_;
        driftPhase_+=step*driftRate_;driftLevelPhase_+=step*driftLevelRate_;
        if(driftPhase_>std::numbers::pi_v<double>)driftPhase_-=twoPi;if(driftLevelPhase_>std::numbers::pi_v<double>)driftLevelPhase_-=twoPi;
        fundamental=clampFinite(fundamental,1,static_cast<float>(sampleRate_*.45),440);output=clampFinite(output,0,1,0);
    }
    if(pendingPhaseOffset_&&unisonSource)
    {
        // Deferred to the first block after note-on because `phaseRandom` is a patch value, not a
        // note property; it is not matrix-eligible, so its base value is stable for the note.
        pendingPhaseOffset_=false;const auto random=clampFinite(p.phaseRandom,0,1,0);
        if(random>0)for(std::size_t c=0;c<maximumUnisonVoices;++c)
        {
            const auto phase=twoPi*random*unisonPhase_[c];
            if(type_==domain::ModuleType::harmonic){harmonicSin_[c].fill(static_cast<float>(std::sin(phase)));harmonicCos_[c].fill(static_cast<float>(std::cos(phase)));}
            else {phases_[c]=std::remainder(phase,twoPi);modPhases_[c]=std::remainder(twoPi*random*unisonModPhase_[c],twoPi);}
        }
    }
    if(type_==domain::ModuleType::filter&&filterControlCountdown_--==0){filterControlCountdown_=15;const auto cutoff=std::min(clampFinite(p.cutoff,30,18000,1000),static_cast<float>(sampleRate_*.2)),q=clampFinite(p.q,.5f,8,.707f);if(cutoff!=cachedFilterCutoff_){filter_.setCutoffFrequency(cutoff);cachedFilterCutoff_=cutoff;}if(q!=cachedFilterQ_){filter_.setResonance(q);cachedFilterQ_=q;}filter_.setType(p.mode==1?juce::dsp::StateVariableTPTFilterType::bandpass:p.mode==2?juce::dsp::StateVariableTPTFilterType::highpass:juce::dsp::StateVariableTPTFilterType::lowpass);}
    if(type_==domain::ModuleType::harmonic)for(std::size_t n=0;n<16;++n)if(p.pans[n]!=cachedPans_[n]){cachedPans_[n]=p.pans[n];panLeft_[n]=panGain(p.pans[n],false);panRight_[n]=panGain(p.pans[n],true);}
    // D8 spectral shape (#121). All three controls reshape what is *rendered* and never touch the
    // stored arrays, so a saved spectrum survives a load/save round trip and every one of them stays
    // matrix-modulatable. `harmonicityMorph` pulls each ratio toward its nearest integer — the
    // control that turns the chemistry-derived bell into a tone with a fundamental; `oddEvenBalance`
    // attenuates whichever parity group is out of favour (attenuation only, so no partial is ever
    // boosted past its stored amplitude); `symmetry` tilts the amplitudes across the partial index,
    // 0 low-heavy and 1 high-heavy. Each is the literal identity at its catalog default —
    // std::lerp(r,·,0) is r and both gains are 1 - 0*x = 1 exactly — which is what keeps a pre-D8
    // patch rendering the same samples (the `patch-compatibility` ctest is the production proof).
    if(type_==domain::ModuleType::harmonic)
    {
        const auto balance=clampFinite(p.oddEvenBalance,-1,1,0),tilt=(clampFinite(p.symmetry,0,1,.5f)-.5f)*2.0f;
        const auto oddGain=1.0f+std::min(balance,0.0f),evenGain=1.0f-std::max(balance,0.0f);
        for(std::size_t n=0;n<16;++n)
        {
            const auto position=static_cast<float>(n)/15.0f;
            const auto symmetryGain=1.0f-std::abs(tilt)*(tilt>=0?1.0f-position:position);
            shapedAmplitudes_[n]=clampFinite(p.amplitudes[n],0,1,0)*(n%2==0?oddGain:evenGain)*symmetryGain;
        }
    }
    // One recursion increment per (copy, partial). `detuneMultiplier_` is exactly 1 for a single
    // copy, so the product is bit for bit the pre-D8 increment. The morphed ratio is what is cached,
    // so both the increment and the 0.45-rate culling below run on the post-morph spectrum.
    if(type_==domain::ModuleType::harmonic){const auto morph=clampFinite(p.harmonicityMorph,0,1,0);for(std::size_t n=0;n<16;++n){const auto stored=clampFinite(p.ratios[n],.5f,32,static_cast<float>(n+1));const auto ratio=std::lerp(stored,std::round(stored),morph);if(fundamental!=cachedHarmonicFundamental_||ratio!=cachedRatios_[n]||unisonDirty_){cachedRatios_[n]=ratio;for(int c=0;c<copies;++c){const auto delta=twoPi*fundamental*ratio*detuneMultiplier_[static_cast<std::size_t>(c)]/sampleRate_;harmonicDeltaSin_[static_cast<std::size_t>(c)][n]=static_cast<float>(std::sin(delta));harmonicDeltaCos_[static_cast<std::size_t>(c)][n]=static_cast<float>(std::cos(delta));}}}cachedHarmonicFundamental_=fundamental;unisonDirty_=false;}
    if(type_==domain::ModuleType::mixer&&p.pan!=cachedPan_){cachedPan_=p.pan;cachedPanLeft_=panGain(p.pan,false);cachedPanRight_=panGain(p.pan,true);}
    if(type_==domain::ModuleType::fm&&fundamental!=cachedFundamental_){cachedFundamental_=fundamental;const auto midi=69+12*std::log2(fundamental/440);cachedFmRolloff_=midi>84?std::max(0.0f,1-(midi-84)/43):1.f;}
    for(std::size_t i=0;i<count;++i){float l=inL[i],r=inR[i];
        if(type_==domain::ModuleType::harmonic){l=r=0;for(std::size_t c=0;c<static_cast<std::size_t>(copies);++c){auto& hs=harmonicSin_[c];auto& hc=harmonicCos_[c];const auto& ds=harmonicDeltaSin_[c];const auto& dc=harmonicDeltaCos_[c];float cl=0,cr=0;for(std::size_t n=0;n<16;++n){const auto hz=fundamental*cachedRatios_[n]*detuneMultiplier_[c];if(hz>sampleRate_*.45)continue;const auto s=hs[n]*shapedAmplitudes_[n];cl+=s*panLeft_[n];cr+=s*panRight_[n];const auto nextSin=hs[n]*dc[n]+hc[n]*ds[n];hc[n]=hc[n]*dc[n]-hs[n]*ds[n];hs[n]=nextSin;}l+=cl*unisonPanLeft_[c];r+=cr*unisonPanRight_[c];}if(--harmonicRenormalizeCountdown_==0){harmonicRenormalizeCountdown_=4096;for(std::size_t c=0;c<static_cast<std::size_t>(copies);++c)for(std::size_t n=0;n<16;++n){const auto magnitude=std::sqrt(harmonicSin_[c][n]*harmonicSin_[c][n]+harmonicCos_[c][n]*harmonicCos_[c][n]);if(magnitude>0){harmonicSin_[c][n]/=magnitude;harmonicCos_[c][n]/=magnitude;}}}}
        else if(type_==domain::ModuleType::fm){const auto cr=clampFinite(p.carrierRatio,.5f,4,1),mr=clampFinite(p.modulatorRatio,.25f,8,1);const auto index=clampFinite(p.index,0,6,0)*cachedFmRolloff_;l=r=0;for(std::size_t c=0;c<static_cast<std::size_t>(copies);++c){const auto detune=detuneMultiplier_[c];const auto s=fastSin(phases_[c]+index*fastSin(modPhases_[c]));l+=s*unisonPanLeft_[c];r+=s*unisonPanRight_[c];phases_[c]+=twoPi*fundamental*cr*detune/sampleRate_;modPhases_[c]+=twoPi*fundamental*mr*detune/sampleRate_;if(phases_[c]>std::numbers::pi_v<double>)phases_[c]-=twoPi;if(modPhases_[c]>std::numbers::pi_v<double>)modPhases_[c]-=twoPi;}}
        else if(type_==domain::ModuleType::noise){const auto burst=static_cast<std::size_t>(sampleRate_*clampFinite(p.burstMilliseconds,1,500,80)*.001);if(p.mode==1&&burstSamplesRemaining_==std::numeric_limits<std::size_t>::max())burstSamplesRemaining_=burst;auto s=(gate_&&(p.mode==0||burstSamplesRemaining_>0))?nextNoise():0.0f;if(p.mode==1&&burstSamplesRemaining_>0)--burstSamplesRemaining_;if(p.color==1){pink_[0]=.99765f*pink_[0]+.099046f*s;pink_[1]=.963f*pink_[1]+.2965164f*s;s=.35f*(pink_[0]+pink_[1]+s*.1848f);}l=r=s;}
        else if(type_==domain::ModuleType::resonator){const auto hz=fundamental*clampFinite(p.tuneRatio,.5f,4,1);if(p.mode==0){comb_->setDelay(std::clamp(static_cast<float>(sampleRate_/hz),1.0f,static_cast<float>(comb_->getMaximumDelayInSamples())));const auto fb=clampFinite(p.combFeedback,0,.97f,.4f),dl=comb_->popSample(0),dr=comb_->popSample(1);comb_->pushSample(0,l+dl*fb);comb_->pushSample(1,r+dr*fb);l=dl;r=dr;}else{float sl=0,sr=0;const auto q=clampFinite(p.modalQ,.5f,12,3);for(std::size_t m=0;m<4;++m){const auto cutoff=std::min(hz*clampFinite(p.modeRatios[m],.5f,4,1),static_cast<float>(sampleRate_*.4));modes_[m].setType(juce::dsp::StateVariableTPTFilterType::bandpass);if(cutoff!=cachedModeCutoffs_[m]){modes_[m].setCutoffFrequency(cutoff);cachedModeCutoffs_[m]=cutoff;}if(q!=cachedModalQ_)modes_[m].setResonance(q);const auto g=clampFinite(p.modeLevels[m],0,1,0);sl+=modes_[m].processSample(0,l)*g;sr+=modes_[m].processSample(1,r)*g;}cachedModalQ_=q;l=sl;r=sr;}}
        else if(type_==domain::ModuleType::filter){l=filter_.processSample(0,l);r=filter_.processSample(1,r);}
        else if(type_==domain::ModuleType::shaper){const auto d=clampFinite(p.drive,1,16,1),w=clampFinite(p.wet,0,1,1);l=std::lerp(l,fastTanh(l*d),w);r=std::lerp(r,fastTanh(r*d),w);}
        // D8 sub oscillator (#122): one directly evaluated sine or triangle — no wavetable — at the
        // pitch the block above resolved, so MIDI, masterTune, bend, `fine`, `keytrack`, `octave`
        // and `drift` all reach it exactly as they reach the other pitched sources.
        else if(type_==domain::ModuleType::sub){const auto phase=phases_[0];l=r=p.waveform==1?triangleWave(phase):fastSin(phase);phases_[0]=phase+twoPi*fundamental/sampleRate_;if(phases_[0]>std::numbers::pi_v<double>)phases_[0]-=twoPi;}
        else if(type_==domain::ModuleType::chorus||type_==domain::ModuleType::delay||type_==domain::ModuleType::reverb||type_==domain::ModuleType::width){} // pass-through skeleton until issues #123/#124
        else{const auto g=clampFinite(p.level,0,1,1);l*=g*cachedPanLeft_;r*=g*cachedPanRight_;}outL[i]=std::isfinite(l)?l*output:0;outR[i]=std::isfinite(r)?r*output:0;}
}
void ModulatorBank::prepare(double rate) noexcept {sampleRate_=std::max(1.0,rate);smoothingLength_=static_cast<std::uint64_t>(std::max(1.0,sampleRate_*.020));for(auto& e:envelopes_)e.setSampleRate(sampleRate_);}
void ModulatorBank::configure(const std::array<domain::Envelope,domain::envelopeCount>& es,const std::array<domain::Lfo,domain::lfoCount>& ls) noexcept {previousLfoSettings_=lfoSettings_;lfoSettings_=ls;smoothingSample_=0;for(std::size_t i=0;i<domain::envelopeCount;++i){juce::ADSR::Parameters p;p.attack=static_cast<float>(std::clamp(es[i].attack,.001,2.0));p.decay=static_cast<float>(std::clamp(es[i].decay,.01,4.0));p.sustain=static_cast<float>(std::clamp(es[i].sustain,0.0,1.0));p.release=static_cast<float>(std::clamp(es[i].release,.02,6.0));envelopes_[i].setParameters(p);}}
void ModulatorBank::reset() noexcept {lfoPhases_.fill(0);for(auto&e:envelopes_)e.reset();}
void ModulatorBank::noteOn() noexcept {lfoPhases_.fill(0);for(auto& e:envelopes_)e.noteOn();}void ModulatorBank::noteOff() noexcept {for(auto& e:envelopes_)e.noteOff();}
std::array<float,5> ModulatorBank::next() noexcept {std::array<float,5> r{};for(std::size_t i=0;i<domain::envelopeCount;++i){const auto sample=envelopes_[i].getNextSample();if(i<3)r[i]=sample;} // E4 runs with the bank; issue #125 makes it a matrix source
const float x=smoothingSample_>=smoothingLength_?1.f:static_cast<float>(smoothingSample_)/static_cast<float>(smoothingLength_);for(std::size_t i=0;i<2;++i){const auto ph=lfoPhases_[i];const auto wave=[ph](domain::LfoWaveform waveform){if(waveform==domain::LfoWaveform::sine)return fastSin(ph);const auto scaled=static_cast<float>(2*ph/std::numbers::pi_v<double>);return ph>std::numbers::pi_v<double>*.5?2-scaled:ph<-std::numbers::pi_v<double>*.5?-2-scaled:scaled;};r[3+i]=std::lerp(wave(previousLfoSettings_[i].waveform),wave(lfoSettings_[i].waveform),x);const auto rate=std::lerp(previousLfoSettings_[i].rate,lfoSettings_[i].rate,static_cast<double>(x));lfoPhases_[i]=ph+twoPi*std::clamp(rate,.05,12.0)/sampleRate_;if(lfoPhases_[i]>std::numbers::pi_v<double>)lfoPhases_[i]-=twoPi;}if(smoothingSample_<smoothingLength_)++smoothingSample_;return r;}
}
