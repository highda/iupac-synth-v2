#include "iupac/engine/Engine.hpp"
#include "iupac/engine/Wavetables.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
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
// D12 classic oscillator (#166). `t` is the phase in [0, 1) and `dt` the phase step per sample.
// polyBLEP is the two-sample polynomial residual of a band-limited unit step, subtracted around
// each discontinuity; polyBLAMP is its integral, the residual of a band-limited corner, and is
// what keeps the triangle's two slope changes from aliasing. At the 2x internal rate both leave
// the audible band clean well past the top of the keyboard.
float polyBlep(double t,double dt) noexcept
{
    if(t<dt){const auto x=t/dt;return static_cast<float>(x+x-x*x-1.0);}
    if(t>1.0-dt){const auto x=(t-1.0)/dt;return static_cast<float>(x*x+x+x+1.0);}
    return 0.0f;
}
float polyBlamp(double t,double dt) noexcept
{
    double x;
    if(t<dt)x=t/dt;else if(t>1.0-dt)x=(1.0-t)/dt;else return 0.0f;
    const auto y=1.0-x;return static_cast<float>(y*y*y/6.0);
}
double wrapUnit(double t) noexcept {return t>=1.0?t-1.0:t;}
float fastTanh(float value)noexcept{if(value>=5)return 1;if(value<=-5)return-1;return juce::dsp::FastMathApproximations::tanh(value);}
// D9 delegates the chorus modulation shape and the delay's sync arithmetic (#123).
// The chorus is `voices` taps on one prepared stereo line, each swept by the same sine at a phase
// offset of a full turn divided by the tap count, so the taps never bunch; the sweep runs from
// `chorusBaseMilliseconds` to `chorusBaseMilliseconds + chorusSweepMilliseconds` at `depth` 1, the
// classic 6-12 ms chorus window rather than a flanger's sub-millisecond one.
constexpr float chorusBaseMilliseconds = 6.0f, chorusSweepMilliseconds = 6.0f;
// The delay's `spread` skews the two channels' times by up to this fraction in opposite directions,
// which is a stereo offset rather than a ping-pong: no cross-channel feedback path is introduced.
constexpr float delaySpreadRange = 0.35f;
// `damping` is a one-pole lowpass inside the feedback path. The coefficient is exactly 1 at damping
// 0 — the repeat is then the unfiltered delayed sample — and falls to 0.05 at damping 1.
constexpr float delayDampingRange = 0.95f;
// `syncDivision` in catalog order: 1/1, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, as a count of quarter-note
// beats. A triplet is two thirds of the plain division above it.
constexpr std::array<double, 7> syncDivisionBeats{4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25};
// D9 delegates the shaper curve equations (#126). Every one of the five is bounded by construction
// on [-1, 1] whatever `drive` does to its input, which is what keeps the output finite at drive 16:
// tanh and the asymmetric variant saturate, the hard clip clamps, and the fold and sine wrap.
// Curve 0 is the pre-D8 `fastTanh` expression character for character, so `tanh` stays bit-unchanged.
// The asymmetric curve runs the negative half at a lower drive, so it is continuous through zero
// (both halves are 0 there) but has a different slope on each side, which is what makes even
// harmonics; 0.5 is a clearly audible bias without turning into a half-wave rectifier.
constexpr float asymmetricNegativeDrive = 0.5f;
// Triangle wavefolder over a period of 4: 0 at 0, +1 at 1, back through 0 at 2, -1 at 3.
float foldWave(float t) noexcept
{
    const auto p=std::fmod(std::fabs(t)+1.0f,4.0f);
    return std::copysign(p<2.0f?p-1.0f:3.0f-p,t);
}
float shapeSample(int curve,float x,float drive) noexcept
{
    const auto t=x*drive;
    switch(curve)
    {
        case 1: return std::clamp(t,-1.0f,1.0f);
        case 2: return foldWave(t);
        case 3: return fastSin(static_cast<double>(t)*std::numbers::pi_v<double>*.5);
        case 4: return fastTanh(t>=0?t:t*asymmetricNegativeDrive);
        default: return fastTanh(t);
    }
}
}
float ModuleProcessor::clampFinite(float v,float lo,float hi,float fallback) noexcept { return std::isfinite(v)?std::clamp(v,lo,hi):fallback; }
// D8 reverb (#124). One sample of the fixed network: pre-delay, two Schroeder allpass diffusers,
// then a four-line FDN whose read-back is damped, gain-staged for the requested RT60 and mixed by a
// normalized Hadamard matrix before being written back. The matrix is orthonormal and every line
// gain is strictly below 1, so the loop is a contraction for every reachable control setting —
// which is what makes `decaySeconds` 20 at `mix` 1 a long tail rather than a runaway one.
void ReverbNetwork::prepare(const juce::dsp::ProcessSpec&spec)
{
    sampleRate_=std::max(1.0,spec.sampleRate);
    preDelay_.setMaximumDelayInSamples(static_cast<int>(maximumReverbPreDelaySeconds*sampleRate_)+4);preDelay_.prepare(spec);
    preDelayCeiling_=static_cast<float>(preDelay_.getMaximumDelayInSamples()-2);
    for(std::size_t i=0;i<diffusers_.size();++i){diffusers_[i].setMaximumDelayInSamples(static_cast<int>(reverbDiffuserSeconds[i]*sampleRate_)+4);diffusers_[i].prepare(spec);diffuserSamples_[i]=std::max(1.0f,static_cast<float>(reverbDiffuserSeconds[i]*sampleRate_));}
    for(std::size_t i=0;i<reverbLineCount;++i){lines_[i].setMaximumDelayInSamples(static_cast<int>(reverbLineSeconds[i]*sampleRate_)+4);lines_[i].prepare(spec);}
    reset();
}
void ReverbNetwork::reset() noexcept
{
    preDelay_.reset();for(auto&d:diffusers_)d.reset();for(auto&l:lines_)l.reset();
    damperLeft_.fill(0);damperRight_.fill(0);
    cachedSize_=cachedDecay_=std::numeric_limits<float>::quiet_NaN();
}
// `size` scales every line length together, so the network keeps its ratios and only changes scale;
// each line's feedback gain is then the RT60 gain for *its own* length, which is what keeps the
// decay time the control says it is at any size.
void ReverbNetwork::updateGeometry(float size,float decaySeconds) noexcept
{
    cachedSize_=size;cachedDecay_=decaySeconds;
    const auto scale=std::lerp(reverbSmallestSizeScale,1.0f,size);
    for(std::size_t i=0;i<reverbLineCount;++i)
    {
        const auto seconds=static_cast<float>(reverbLineSeconds[i])*scale;
        lineSamples_[i]=std::max(1.0f,seconds*static_cast<float>(sampleRate_));
        lineGain_[i]=std::pow(10.0f,-3.0f*seconds/decaySeconds);
    }
}
void ReverbNetwork::process(float inL,float inR,float size,float decaySeconds,float damping,float preDelayMilliseconds,float width,float&wetLeft,float&wetRight) noexcept
{
    if(size!=cachedSize_||decaySeconds!=cachedDecay_)updateGeometry(size,decaySeconds);
    preDelay_.pushSample(0,inL);preDelay_.pushSample(1,inR);
    const auto preSamples=std::clamp(static_cast<float>(preDelayMilliseconds*.001*sampleRate_),1.0f,preDelayCeiling_);
    float dl=preDelay_.popSample(0,preSamples),dr=preDelay_.popSample(1,preSamples);
    for(std::size_t k=0;k<diffusers_.size();++k)
    {
        const auto delayedL=diffusers_[k].popSample(0,diffuserSamples_[k]),delayedR=diffusers_[k].popSample(1,diffuserSamples_[k]);
        const auto vl=dl+reverbDiffusion*delayedL,vr=dr+reverbDiffusion*delayedR;
        diffusers_[k].pushSample(0,vl);diffusers_[k].pushSample(1,vr);
        dl=delayedL-reverbDiffusion*vl;dr=delayedR-reverbDiffusion*vr;
    }
    std::array<float,reverbLineCount> readL{},readR{};
    for(std::size_t i=0;i<reverbLineCount;++i)
    {
        readL[i]=lines_[i].popSample(0,lineSamples_[i]);
        readR[i]=lines_[i].popSample(1,std::max(1.0f,lineSamples_[i]*reverbRightSkew));
    }
    // Damp inside the loop, so each pass through the network is darker than the one before it.
    const auto coefficient=1.0f-reverbDampingRange*damping;
    std::array<float,reverbLineCount> fbL{},fbR{};
    for(std::size_t i=0;i<reverbLineCount;++i)
    {
        damperLeft_[i]+=coefficient*(readL[i]*lineGain_[i]-damperLeft_[i]);
        damperRight_[i]+=coefficient*(readR[i]*lineGain_[i]-damperRight_[i]);
        if(std::abs(damperLeft_[i])<denormalFloor)damperLeft_[i]=0;
        if(std::abs(damperRight_[i])<denormalFloor)damperRight_[i]=0;
        fbL[i]=damperLeft_[i];fbR[i]=damperRight_[i];
    }
    // Normalized 4x4 Hadamard: orthonormal, so it redistributes energy between the lines without
    // adding any. The 0.5 is 1/sqrt(4).
    const auto mix4=[](const std::array<float,reverbLineCount>&v,std::array<float,reverbLineCount>&out){
        out[0]=.5f*(v[0]+v[1]+v[2]+v[3]);out[1]=.5f*(v[0]-v[1]+v[2]-v[3]);
        out[2]=.5f*(v[0]+v[1]-v[2]-v[3]);out[3]=.5f*(v[0]-v[1]-v[2]+v[3]);};
    std::array<float,reverbLineCount> mixedL{},mixedR{};mix4(fbL,mixedL);mix4(fbR,mixedR);
    float sumL=0,sumR=0;
    for(std::size_t i=0;i<reverbLineCount;++i)
    {
        lines_[i].pushSample(0,std::isfinite(mixedL[i])?dl+mixedL[i]:dl);
        lines_[i].pushSample(1,std::isfinite(mixedR[i])?dr+mixedR[i]:dr);
        sumL+=readL[i];sumR+=readR[i];
    }
    sumL*=.5f;sumR*=.5f;
    // `width` collapses the wet image to mono at 0 and leaves it untouched at its default 1.
    const auto mid=(sumL+sumR)*.5f,side=(sumL-sumR)*.5f*width;
    wetLeft=mid+side;wetRight=mid-side;
}
// D9 delegates the ladder formulation (#126). Four TPT one-poles in series give the 24 dB slope and
// the feedback around them gives the resonance; the loop is closed through fastTanh, so the stage
// input is in [-1, 1] by construction and each one-pole is a unit-DC-gain contraction — the output
// therefore cannot leave [-1, 1] at any reachable `q`, `cutoff` or `drive`, and maximum resonance
// self-oscillates rather than blowing up. The drive term also compensates the bass a ladder loses
// as its feedback rises, inside the same tanh, so it buys no extra headroom.
float ModuleProcessor::ladderSample(std::size_t channel,float input) noexcept
{
    auto* s=&ladderState_[channel*4];
    float v=fastTanh(input*(1.0f+ladderFeedback_*.5f)-ladderFeedback_*ladderLast_[channel]);
    for(std::size_t k=0;k<4;++k){const auto d=(v-s[k])*ladderG_;const auto y=d+s[k];s[k]=y+d;v=y;}
    ladderLast_[channel]=v;
    return v;
}
void ModuleProcessor::prepare(double rate,std::size_t block)
{
    sampleRate_=std::max(1.0,rate); juce::dsp::ProcessSpec spec{sampleRate_,static_cast<juce::uint32>(std::min(block,maximumModuleBlockSize)),2};
    if(type_==domain::ModuleType::resonator&&!comb_){ownedComb_=std::make_unique<CombDelay>(110000);ownedComb_->prepare(spec);comb_=ownedComb_.get();}
    // The engine's global tail hands its two prepared lines in before a patch is published, so this
    // branch only fires for a chorus/delay processor prepared on its own (the module unit tests).
    if((type_==domain::ModuleType::chorus||type_==domain::ModuleType::delay)&&!effect_)
    {
        ownedEffect_=std::make_unique<CombDelay>(static_cast<int>((type_==domain::ModuleType::chorus?maximumChorusDelaySeconds:maximumDelaySeconds)*sampleRate_)+4);
        ownedEffect_->prepare(spec);effect_=ownedEffect_.get();
    }
    // Same story for the reverb network: the tail hands its own in, so this only fires standalone.
    if(type_==domain::ModuleType::reverb&&!reverb_){ownedReverb_=std::make_unique<ReverbNetwork>();ownedReverb_->prepare(spec);reverb_=ownedReverb_.get();}
    filter_.prepare(spec);for(auto& m:modes_)m.prepare(spec);reset();
}
void ModuleProcessor::reset() noexcept {phases_.fill(0);modPhases_.fill(0);for(auto& c:harmonicSin_)c.fill(0);for(auto& c:harmonicCos_)c.fill(1);cachedRatios_.fill(-1);cachedHarmonicFundamental_=-1;cachedRawAmplitudes_.fill(std::numeric_limits<float>::quiet_NaN());cachedRawRatios_.fill(std::numeric_limits<float>::quiet_NaN());cachedOddEvenBalance_=cachedSymmetry_=cachedHarmonicityMorph_=std::numeric_limits<float>::quiet_NaN();harmonicGainsDirty_=true;harmonicRenormalizeCountdown_=4096;pink_.fill(0);burstSamplesRemaining_=0;gate_=false;filterControlCountdown_=0;cachedPan_=cachedFundamental_=cachedFilterCutoff_=cachedFilterQ_=cachedModalQ_=std::numeric_limits<float>::quiet_NaN();cachedModeCutoffs_.fill(-1);cachedPans_.fill(std::numeric_limits<float>::quiet_NaN());
    unisonPhase_.fill(0);unisonModPhase_.fill(0);cachedDetuneCents_=cachedUnisonSpread_=std::numeric_limits<float>::quiet_NaN();cachedUnisonVoices_=0;unisonDirty_=true;pendingPhaseOffset_=false;
    driftPhase_=driftLevelPhase_=0;driftRate_=.11;driftLevelRate_=.07;
    effectDampLeft_=effectDampRight_=0;if(ownedReverb_)ownedReverb_->reset();
    ladderState_.fill(0);ladderLast_.fill(0);cachedFilterDrive_=std::numeric_limits<float>::quiet_NaN();
    if(comb_)comb_->reset();if(effect_)effect_->reset();filter_.reset();for(auto& m:modes_)m.reset();}
void ModuleProcessor::noteOn(int note,int channel,std::uint32_t seed,std::uint32_t nodeHash) noexcept
{
    phases_.fill(0);modPhases_.fill(0);for(auto& c:harmonicSin_)c.fill(0);for(auto& c:harmonicCos_)c.fill(1);pink_.fill(0);gate_=true;note_=note;randomState_=seed^(nodeHash*0x9e3779b9u)^(static_cast<std::uint32_t>(note)<<16u)^(static_cast<std::uint32_t>(channel)*0x85ebca6bu);if(!randomState_)randomState_=1;burstSamplesRemaining_=std::numeric_limits<std::size_t>::max();
    // D8: the unison start phases and the two drift wanders are drawn here, once, from the
    // patch-seeded stream this voice already owns — never from the wall clock, so V2 reproducibility
    // holds. Only the three unison/drift-capable source types draw, so the noise stream is untouched
    // and a pre-D8 noise node still renders the identical sequence. The draws happen whatever
    // `unisonVoices` is, so the stream does not depend on the copy count either.
    if(type_==domain::ModuleType::harmonic||type_==domain::ModuleType::fm||type_==domain::ModuleType::sub||type_==domain::ModuleType::osc||type_==domain::ModuleType::wavetable)
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
// (#144) The per-sample dispatch. `type_` is set once by setType() and never changes inside a
// render, so the whole body below is compiled once per catalog type with the type as a compile-time
// constant: every `type_ == domain::ModuleType::X` test in it becomes `true` or `false` and folds,
// together with the branch it guards. Nothing about the arithmetic, its order or its operands
// changes — the same source produces each instantiation — so every render is bit for bit the one
// the single-function version produced; the authored panel digests are the production proof.
void ModuleProcessor::process(const ModuleValues& p,float fundamental,std::span<const float> inL,std::span<const float> inR,std::span<float> outL,std::span<float> outR,std::span<const float> modL,std::span<const float> modR) noexcept
{
    using domain::ModuleType;
    switch(type_)
    {
        case ModuleType::harmonic: return processTyped<ModuleType::harmonic>(p,fundamental,inL,inR,outL,outR,modL,modR);
        case ModuleType::fm:       return processTyped<ModuleType::fm>(p,fundamental,inL,inR,outL,outR,modL,modR);
        case ModuleType::noise:    return processTyped<ModuleType::noise>(p,fundamental,inL,inR,outL,outR,modL,modR);
        case ModuleType::sub:      return processTyped<ModuleType::sub>(p,fundamental,inL,inR,outL,outR,modL,modR);
        case ModuleType::resonator:return processTyped<ModuleType::resonator>(p,fundamental,inL,inR,outL,outR,modL,modR);
        case ModuleType::filter:   return processTyped<ModuleType::filter>(p,fundamental,inL,inR,outL,outR,modL,modR);
        case ModuleType::shaper:   return processTyped<ModuleType::shaper>(p,fundamental,inL,inR,outL,outR,modL,modR);
        case ModuleType::mixer:    return processTyped<ModuleType::mixer>(p,fundamental,inL,inR,outL,outR,modL,modR);
        case ModuleType::chorus:   return processTyped<ModuleType::chorus>(p,fundamental,inL,inR,outL,outR,modL,modR);
        case ModuleType::delay:    return processTyped<ModuleType::delay>(p,fundamental,inL,inR,outL,outR,modL,modR);
        case ModuleType::reverb:   return processTyped<ModuleType::reverb>(p,fundamental,inL,inR,outL,outR,modL,modR);
        case ModuleType::width:    return processTyped<ModuleType::width>(p,fundamental,inL,inR,outL,outR,modL,modR);
        case ModuleType::osc:      return processTyped<ModuleType::osc>(p,fundamental,inL,inR,outL,outR,modL,modR);
        case ModuleType::wavetable:return processTyped<ModuleType::wavetable>(p,fundamental,inL,inR,outL,outR,modL,modR);
    }
}
template<domain::ModuleType kType>
void ModuleProcessor::processTyped(const ModuleValues& p,float fundamental,std::span<const float> inL,std::span<const float> inR,std::span<float> outL,std::span<float> outR,std::span<const float> modL,std::span<const float> modR) noexcept
{
    const auto count=std::min({inL.size(),inR.size(),outL.size(),outR.size(),maximumModuleBlockSize});
    // D8 audio-rate modulation inputs (#127). An uncabled `modIn`/`exciteIn` arrives as empty spans
    // and this is false, so the two branches below never run and the node costs exactly what it did
    // before D8. The signal is clamped before it reaches either of them, which is what bounds the
    // phase offset the FM branch adds (`fastSin` wraps by subtraction, so an unbounded phase would
    // be an unbounded loop) and keeps the resonator's excitation finite.
    const bool audioRateInput=modL.size()>=count&&modR.size()>=count;fundamental=clampFinite(fundamental,1,static_cast<float>(sampleRate_*.45),440);auto output=clampFinite(p.outputLevel,0,1,0);
    const bool phaseSource=kType==domain::ModuleType::osc||kType==domain::ModuleType::wavetable;
    const bool unisonSource=kType==domain::ModuleType::harmonic||kType==domain::ModuleType::fm||phaseSource;
    const bool pitchedSource=unisonSource||kType==domain::ModuleType::sub;
    const auto requestedCopies=unisonSource?std::clamp(p.unisonVoices,1,static_cast<int>(maximumUnisonVoices)):1;
    // (#144) Everything a pitched source derives from its controls rather than from the sample in
    // front of it — the transposition multiplier, the per-copy detune ratios and pan gains, and the
    // 112 per-(copy, partial) recursion deltas with their sine and cosine each — is guarded by an
    // exact test of the inputs it was last built from, never by a counter. A throttle was tried
    // here and reverted: at a 16-sample period it is invisible on a static patch but it re-times
    // the derivation of a moving one, and it moved the `family-bell`, `family-pad` and
    // `modulation-stage-curves` authored digests. The flag stays so the guards below read as one
    // condition; it is unconditionally true.
    const bool sourceControlUpdate=true;
    // The modal resonator's four band-pass sections are coefficient updates with a tangent each, but
    // `tuneRatio`, `modalQ` and `modeRatios` are matrix-modulatable, so they are resolved on every
    // call and cached against their own inputs. They carry no counter for the same reason the
    // pitched-source derivations above carry none: a counter re-times a modulated render.
    const bool resonatorControlUpdate=kType==domain::ModuleType::resonator;
    // D8 pitch block (#122). `octave`, `coarse` and `fine` transpose the source away from the voice
    // fundamental and `keytrack` scales how much of the note it follows, all as one semitone offset.
    // At the catalog defaults (0 / 0 / 0 / 1) that offset is literally 0 and the branch below does
    // not run, so a pre-D8 patch keeps its exact samples. masterTune and bend are already inside
    // `fundamental`, so a transposed or partly keytracked source still follows both — and so does
    // the sub oscillator, whose own -1/-2 octave is just this same offset.
    if(pitchedSource)
    {
        if(sourceControlUpdate&&(p.octave!=cachedPitchOctave_||p.coarse!=cachedPitchCoarse_||p.fine!=cachedPitchFine_||p.keytrack!=cachedPitchKeytrack_||note_!=cachedPitchNote_))
        {
            cachedPitchOctave_=p.octave;cachedPitchCoarse_=p.coarse;cachedPitchFine_=p.fine;cachedPitchKeytrack_=p.keytrack;cachedPitchNote_=note_;
            const auto semitones=12.0f*static_cast<float>(std::clamp(p.octave,-3,3))+static_cast<float>(std::clamp(p.coarse,-12,12))
                                +clampFinite(p.fine,-100,100,0)*.01f
                                +(clampFinite(p.keytrack,0,1,1)-1.0f)*static_cast<float>(note_-keytrackReferenceNote);
            cachedPitchMultiplier_=semitones==0.0f?1.0f:std::exp2(semitones/12.0f);
        }
        if(cachedPitchMultiplier_!=1.0f)fundamental=clampFinite(fundamental*cachedPitchMultiplier_,1,static_cast<float>(sampleRate_*.45),440);
        if(sourceControlUpdate)updateUnison(requestedCopies,p.detuneCents,p.unisonSpread);
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
    // The copy count the render loops run at is the one `updateUnison` last prepared gains for, so
    // a throttled `unisonVoices` can never read a detune ratio or a pan gain that was never built.
    const auto copies=pitchedSource?static_cast<int>(cachedUnisonVoices_):1;
    if(pendingPhaseOffset_&&unisonSource)
    {
        // Deferred to the first block after note-on because `phaseRandom` is a patch value, not a
        // note property; it is not matrix-eligible, so its base value is stable for the note.
        pendingPhaseOffset_=false;const auto random=clampFinite(p.phaseRandom,0,1,0);
        if(random>0)for(std::size_t c=0;c<maximumUnisonVoices;++c)
        {
            const auto phase=twoPi*random*unisonPhase_[c];
            if(kType==domain::ModuleType::harmonic){harmonicSin_[c].fill(static_cast<float>(std::sin(phase)));harmonicCos_[c].fill(static_cast<float>(std::cos(phase)));}
            // The D12 sources keep their phase as a fraction of a cycle in [0, 1).
            else if(phaseSource)phases_[c]=std::clamp(static_cast<double>(random*unisonPhase_[c]),0.0,0.999999);
            else {phases_[c]=std::remainder(phase,twoPi);modPhases_[c]=std::remainder(twoPi*random*unisonModPhase_[c],twoPi);}
        }
    }
    // The cutoff cap is min(18000 Hz, 0.4 x output rate) in every mode: `sampleRate_` here is the 2x
    // internal rate, so 0.2 of it is 0.4 of the output rate the contract names. `ladder24` reads the
    // same capped value through its own coefficient and `notch` reads it through the TPT bandpass,
    // so no mode can push a pole past that ceiling however hard `drive` is pushed.
    if(kType==domain::ModuleType::filter&&filterControlCountdown_--==0){filterControlCountdown_=15;const auto cutoff=std::min(clampFinite(p.cutoff,30,18000,1000),static_cast<float>(sampleRate_*.2)),q=clampFinite(p.q,.5f,8,.707f);if(cutoff!=cachedFilterCutoff_){filter_.setCutoffFrequency(cutoff);cachedFilterCutoff_=cutoff;}if(q!=cachedFilterQ_){filter_.setResonance(q);cachedFilterQ_=q;}filter_.setType(p.mode==1||p.mode==4?juce::dsp::StateVariableTPTFilterType::bandpass:p.mode==2?juce::dsp::StateVariableTPTFilterType::highpass:juce::dsp::StateVariableTPTFilterType::lowpass);
        // The notch is the SVF identity x = highpass + R2 * bandpass + lowpass rearranged: the two
        // outer bands are x - R2 * bandpass, so one prepared filter serves it with no extra state.
        filterR2_=1.0f/q;
        // `ladder24` coefficients, resolved here rather than per sample: the TPT one-pole gain and a
        // feedback amount that reaches self-oscillation at the top of the declared `q` range.
        const auto g=static_cast<float>(std::tan(std::numbers::pi_v<double>*cutoff/sampleRate_));
        ladderG_=g/(1.0f+g);ladderFeedback_=(q-.5f)/7.5f*4.0f;
        // D8 in-filter `drive` (#126). Exactly 1 is the untouched signal — the branch below is
        // skipped outright — and above it the input is saturated and renormalized so a full-scale
        // input still leaves the saturator at full scale rather than simply getting louder.
        const auto drive=clampFinite(p.drive,1,16,1);
        if(drive!=cachedFilterDrive_){cachedFilterDrive_=drive;driveNormalizer_=drive>1.0f?1.0f/fastTanh(drive):1.0f;}}
    // The width module's crossover is the same prepared TPT filter the filter module uses: a node
    // has exactly one type, so reusing it is the whole storage cost of the C1 class the catalog
    // declares for `bassMonoHz`. setCutoffFrequency() is a tangent, so it is throttled exactly as
    // the filter module's is rather than run on every one of the tail's per-sample calls.
    if(kType==domain::ModuleType::width&&filterControlCountdown_--==0){filterControlCountdown_=15;const auto hz=std::min(clampFinite(p.bassMonoHz,20,500,120),static_cast<float>(sampleRate_*.45));if(hz!=cachedFilterCutoff_){filter_.setType(juce::dsp::StateVariableTPTFilterType::lowpass);filter_.setResonance(.707f);filter_.setCutoffFrequency(hz);cachedFilterCutoff_=hz;}}
    // (#144) `partialPans` feeds nothing but the two pan-gain arrays, so the whole 16-partial walk
    // is skipped outright while the array has not moved. The bitwise test is what makes that exact:
    // equal bits in mean the same two gains out, and it also settles the NaN case, which the
    // element-wise `!=` below would otherwise re-derive on every one of the engine's per-sample calls.
    if(kType==domain::ModuleType::harmonic&&std::memcmp(p.pans.data(),cachedPans_.data(),sizeof cachedPans_)!=0)
    {
        for(std::size_t n=0;n<16;++n)if(std::memcmp(&p.pans[n],&cachedPans_[n],sizeof(float))!=0){cachedPans_[n]=p.pans[n];panLeft_[n]=panGain(p.pans[n],false);panRight_[n]=panGain(p.pans[n],true);}
        harmonicGainsDirty_=true;
    }
    // D8 spectral shape (#121). All three controls reshape what is *rendered* and never touch the
    // stored arrays, so a saved spectrum survives a load/save round trip and every one of them stays
    // matrix-modulatable. `harmonicityMorph` pulls each ratio toward its nearest integer — the
    // control that turns the chemistry-derived bell into a tone with a fundamental; `oddEvenBalance`
    // attenuates whichever parity group is out of favour (attenuation only, so no partial is ever
    // boosted past its stored amplitude); `symmetry` tilts the amplitudes across the partial index,
    // 0 low-heavy and 1 high-heavy. Each is the literal identity at its catalog default —
    // std::lerp(r,·,0) is r and both gains are 1 - 0*x = 1 exactly — which is what keeps a pre-D8
    // patch rendering the same samples (the `patch-compatibility` ctest is the production proof).
    if(kType==domain::ModuleType::harmonic
       &&(std::memcmp(p.amplitudes.data(),cachedRawAmplitudes_.data(),sizeof cachedRawAmplitudes_)!=0
          ||std::memcmp(&p.oddEvenBalance,&cachedOddEvenBalance_,sizeof(float))!=0
          ||std::memcmp(&p.symmetry,&cachedSymmetry_,sizeof(float))!=0))
    {
        cachedRawAmplitudes_=p.amplitudes;cachedOddEvenBalance_=p.oddEvenBalance;cachedSymmetry_=p.symmetry;
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
    if(kType==domain::ModuleType::harmonic&&sourceControlUpdate
       &&(unisonDirty_||fundamental!=cachedHarmonicFundamental_
          ||std::memcmp(p.ratios.data(),cachedRawRatios_.data(),sizeof cachedRawRatios_)!=0
          ||std::memcmp(&p.harmonicityMorph,&cachedHarmonicityMorph_,sizeof(float))!=0))
    {cachedRawRatios_=p.ratios;cachedHarmonicityMorph_=p.harmonicityMorph;harmonicGainsDirty_=true;const auto morph=clampFinite(p.harmonicityMorph,0,1,0);for(std::size_t n=0;n<16;++n){const auto stored=clampFinite(p.ratios[n],.5f,32,static_cast<float>(n+1));const auto ratio=std::lerp(stored,std::round(stored),morph);if(fundamental!=cachedHarmonicFundamental_||ratio!=cachedRatios_[n]||unisonDirty_){cachedRatios_[n]=ratio;for(int c=0;c<copies;++c){const auto delta=twoPi*fundamental*ratio*detuneMultiplier_[static_cast<std::size_t>(c)]/sampleRate_;harmonicDeltaSin_[static_cast<std::size_t>(c)][n]=static_cast<float>(std::sin(delta));harmonicDeltaCos_[static_cast<std::size_t>(c)][n]=static_cast<float>(std::cos(delta));}}}cachedHarmonicFundamental_=fundamental;unisonDirty_=false;}
    // (#144) The 0.45-rate cull and the pan gain are folded into one per-copy array. Nothing it
    // reads can move inside a sample, so recomputing it per sample bought nothing; the render
    // arithmetic is unchanged, because an audible partial still multiplies by exactly the pan gain
    // it did and a culled one multiplies by a literal zero in place of the loop's `continue`.
    if(kType==domain::ModuleType::harmonic&&harmonicGainsDirty_)
    {
        harmonicGainsDirty_=false;
        for(std::size_t c=0;c<maximumUnisonVoices;++c)for(std::size_t n=0;n<16;++n)
        {
            const bool audible=!(fundamental*cachedRatios_[n]*detuneMultiplier_[c]>sampleRate_*.45);
            harmonicGainLeft_[c][n]=audible?panLeft_[n]:0.0f;harmonicGainRight_[c][n]=audible?panRight_[n]:0.0f;
        }
    }
    if(kType==domain::ModuleType::mixer&&p.pan!=cachedPan_){cachedPan_=p.pan;cachedPanLeft_=panGain(p.pan,false);cachedPanRight_=panGain(p.pan,true);}
    if(kType==domain::ModuleType::fm&&sourceControlUpdate&&fundamental!=cachedFundamental_){cachedFundamental_=fundamental;const auto midi=69+12*std::log2(fundamental/440);cachedFmRolloff_=midi>84?std::max(0.0f,1-(midi-84)/43):1.f;}
    for(std::size_t i=0;i<count;++i){float l=inL[i],r=inR[i];
        if(kType==domain::ModuleType::harmonic){l=r=0;for(std::size_t c=0;c<static_cast<std::size_t>(copies);++c){auto& hs=harmonicSin_[c];auto& hc=harmonicCos_[c];const auto& ds=harmonicDeltaSin_[c];const auto& dc=harmonicDeltaCos_[c];const auto& gl=harmonicGainLeft_[c];const auto& gr=harmonicGainRight_[c];float cl=0,cr=0;for(std::size_t n=0;n<16;++n){const auto s=hs[n]*shapedAmplitudes_[n];cl+=s*gl[n];cr+=s*gr[n];const auto nextSin=hs[n]*dc[n]+hc[n]*ds[n];hc[n]=hc[n]*dc[n]-hs[n]*ds[n];hs[n]=nextSin;}l+=cl*unisonPanLeft_[c];r+=cr*unisonPanRight_[c];}if(--harmonicRenormalizeCountdown_==0){harmonicRenormalizeCountdown_=4096;for(std::size_t c=0;c<static_cast<std::size_t>(copies);++c)for(std::size_t n=0;n<16;++n){const auto magnitude=std::sqrt(harmonicSin_[c][n]*harmonicSin_[c][n]+harmonicCos_[c][n]*harmonicCos_[c][n]);if(magnitude>0){harmonicSin_[c][n]/=magnitude;harmonicCos_[c][n]/=magnitude;}}}}
        // D8 `fm.modIn` (#127): the cabled signal is added to the modulator operator's phase, scaled
        // by `modInDepth`, where full depth on a full-scale signal is one whole cycle. It is read as
        // an offset every sample and never accumulated into `modPhases_`, so it is phase modulation
        // and cannot integrate into a pitch drift. `modInDepth` 0 is the catalog default and makes
        // the offset literally 0.0, so the carrier expression is bit for bit the pre-D8 one.
        else if(kType==domain::ModuleType::fm){const auto cr=clampFinite(p.carrierRatio,.5f,4,1),mr=clampFinite(p.modulatorRatio,.25f,8,1);const auto index=clampFinite(p.index,0,6,0)*cachedFmRolloff_;
            const auto modDepth=clampFinite(p.modInDepth,0,1,0);
            const double modOffset=audioRateInput&&modDepth>0?static_cast<double>(modDepth*clampFinite((modL[i]+modR[i])*.5f,-4,4,0))*twoPi:0.0;
            l=r=0;for(std::size_t c=0;c<static_cast<std::size_t>(copies);++c){const auto detune=detuneMultiplier_[c];const auto s=fastSin(phases_[c]+index*fastSin(modPhases_[c]+modOffset));l+=s*unisonPanLeft_[c];r+=s*unisonPanRight_[c];phases_[c]+=twoPi*fundamental*cr*detune/sampleRate_;modPhases_[c]+=twoPi*fundamental*mr*detune/sampleRate_;if(phases_[c]>std::numbers::pi_v<double>)phases_[c]-=twoPi;if(modPhases_[c]>std::numbers::pi_v<double>)modPhases_[c]-=twoPi;}}
        else if(kType==domain::ModuleType::noise){const auto burst=static_cast<std::size_t>(sampleRate_*clampFinite(p.burstMilliseconds,1,500,80)*.001);if(p.mode==1&&burstSamplesRemaining_==std::numeric_limits<std::size_t>::max())burstSamplesRemaining_=burst;auto s=(gate_&&(p.mode==0||burstSamplesRemaining_>0))?nextNoise():0.0f;if(p.mode==1&&burstSamplesRemaining_>0)--burstSamplesRemaining_;if(p.color==1){pink_[0]=.99765f*pink_[0]+.099046f*s;pink_[1]=.963f*pink_[1]+.2965164f*s;s=.35f*(pink_[0]+pink_[1]+s*.1848f);}l=r=s;}
        // D8 `resonator.exciteIn` (#127): the cabled signal is added to the excitation the resonator
        // is about to run, scaled by `exciteDepth`. At the default depth of 0 nothing is added, so
        // the comb and the modal bank see exactly the samples they saw before D8.
        else if(kType==domain::ModuleType::resonator){const auto hz=fundamental*clampFinite(p.tuneRatio,.5f,4,1);
            if(audioRateInput){const auto exciteDepth=clampFinite(p.exciteDepth,0,1,0);if(exciteDepth>0){l+=clampFinite(modL[i],-4,4,0)*exciteDepth;r+=clampFinite(modR[i],-4,4,0)*exciteDepth;}}if(p.mode==0){comb_->setDelay(std::clamp(static_cast<float>(sampleRate_/hz),1.0f,static_cast<float>(comb_->getMaximumDelayInSamples())));const auto fb=clampFinite(p.combFeedback,0,.97f,.4f),dl=comb_->popSample(0),dr=comb_->popSample(1);comb_->pushSample(0,l+dl*fb);comb_->pushSample(1,r+dr*fb);l=dl;r=dr;}else{float sl=0,sr=0;const auto q=clampFinite(p.modalQ,.5f,12,3);
            if(resonatorControlUpdate){for(std::size_t m=0;m<4;++m){const auto cutoff=std::min(hz*clampFinite(p.modeRatios[m],.5f,4,1),static_cast<float>(sampleRate_*.4));modes_[m].setType(juce::dsp::StateVariableTPTFilterType::bandpass);if(cutoff!=cachedModeCutoffs_[m]){modes_[m].setCutoffFrequency(cutoff);cachedModeCutoffs_[m]=cutoff;}if(q!=cachedModalQ_)modes_[m].setResonance(q);}cachedModalQ_=q;}
            for(std::size_t m=0;m<4;++m){const auto g=clampFinite(p.modeLevels[m],0,1,0);sl+=modes_[m].processSample(0,l)*g;sr+=modes_[m].processSample(1,r)*g;}l=sl;r=sr;}}
        else if(kType==domain::ModuleType::filter){
            if(cachedFilterDrive_>1.0f){l=fastTanh(l*cachedFilterDrive_)*driveNormalizer_;r=fastTanh(r*cachedFilterDrive_)*driveNormalizer_;}
            if(p.mode==3){l=ladderSample(0,l);r=ladderSample(1,r);}
            else if(p.mode==4){const auto bl=filter_.processSample(0,l),br=filter_.processSample(1,r);l-=filterR2_*bl;r-=filterR2_*br;}
            else {l=filter_.processSample(0,l);r=filter_.processSample(1,r);}}
        // D8 shaper curves (#126). Curve 0 is the expression this line always carried, so `tanh`
        // renders the pre-D8 samples exactly; the other four are the bounded alternatives above.
        else if(kType==domain::ModuleType::shaper){const auto d=clampFinite(p.drive,1,16,1),w=clampFinite(p.wet,0,1,1);
            if(p.curve==0){l=std::lerp(l,fastTanh(l*d),w);r=std::lerp(r,fastTanh(r*d),w);}
            else {l=std::lerp(l,shapeSample(p.curve,l,d),w);r=std::lerp(r,shapeSample(p.curve,r,d),w);}}
        // D8 sub oscillator (#122): one directly evaluated sine or triangle — no wavetable — at the
        // pitch the block above resolved, so MIDI, masterTune, bend, `fine`, `keytrack`, `octave`
        // and `drift` all reach it exactly as they reach the other pitched sources.
        else if(kType==domain::ModuleType::sub){const auto phase=phases_[0];l=r=p.waveform==1?triangleWave(phase):fastSin(phase);phases_[0]=phase+twoPi*fundamental/sampleRate_;if(phases_[0]>std::numbers::pi_v<double>)phases_[0]-=twoPi;}
        // D8 chorus (#123). `voices` taps read one prepared stereo line at swept delays and are
        // panned across the image with the same equal-power law unison uses, so a wider tap count
        // does not grow the level. `feedback` returns the panned wet sum to the line and `mix` is
        // the ordinary dry/wet. The whole effect lives in the global tail, so this state is one set
        // per bank, not one per voice.
        // One test covers the whole effects region and the branches inside it are only reached by a
        // node that is actually in the tail: `process()` runs once per sample, so a mixer at the end
        // of this chain must not pay a comparison per effect type (#122 measured what that costs).
        // D12 classic oscillator (#166). One band-limited shape per unison copy: the sine is evaluated
        // directly, the saw and the pulse carry a polyBLEP at each step and the triangle a polyBLAMP
        // at each corner. `pulseWidth` shapes the square only; its DC term (2w - 1) is removed so a
        // narrow pulse never leans on the V5 DC bound, and it is matrix-modulatable, which is PWM.
        else if(kType==domain::ModuleType::osc)
        {
            const auto wave=std::clamp(p.waveform,0,3);const auto width=static_cast<double>(clampFinite(p.pulseWidth,.05f,.95f,.5f));
            l=r=0;
            for(std::size_t c=0;c<static_cast<std::size_t>(copies);++c)
            {
                const auto t=phases_[c];const auto dt=std::min(.45,static_cast<double>(fundamental)*detuneMultiplier_[c]/sampleRate_);
                float s;
                switch(wave)
                {
                    case 0: s=-fastSin(twoPi*(t-.5));break;
                    case 1: s=static_cast<float>(1.0-4.0*std::abs(t-.5))+static_cast<float>(8.0*dt)*(polyBlamp(t,dt)-polyBlamp(wrapUnit(t+.5),dt));break;
                    case 2: s=static_cast<float>(2.0*t-1.0)-polyBlep(t,dt);break;
                    default:s=(t<width?1.0f:-1.0f)+polyBlep(t,dt)-polyBlep(wrapUnit(t+1.0-width),dt)-static_cast<float>(2.0*width-1.0);break;
                }
                l+=s*unisonPanLeft_[c];r+=s*unisonPanRight_[c];
                phases_[c]=t+dt;if(phases_[c]>=1.0)phases_[c]-=1.0;
            }
        }
        // D12 wavetable source (#166). Each copy reads two adjacent frames of the selected table at
        // the octave band its own pitch allows and crossfades them by `position`. The band is chosen
        // from the highest copy's frequency, so every stored harmonic sits below 0.45 of the internal
        // rate; a band switch only ever adds or removes content above the audible range.
        else if(kType==domain::ModuleType::wavetable)
        {
            l=r=0;
            const auto table=static_cast<std::size_t>(std::clamp(p.table,0,static_cast<int>(wavetableCount)-1));
            // An unbuilt table is silence, never a build: the compiler builds every table a patch names.
            if(const auto* wave=wavetableIfBuilt(table))
            {
                const auto top=static_cast<double>(fundamental)*detuneMultiplier_[static_cast<std::size_t>(copies-1)];
                if(fundamental!=cachedFundamental_||unisonDirty_)
                {
                    cachedFundamental_=fundamental;unisonDirty_=false;
                    const auto allowed=sampleRate_*.45/std::max(1.0,top);
                    int level=0;while(static_cast<double>(wavetableLevelHarmonics(static_cast<std::size_t>(level)))>allowed&&level<static_cast<int>(wavetableLevels)-1)++level;
                    wavetableLevel_=level;
                }
                const auto level=static_cast<std::size_t>(wavetableLevel_);const auto size=wavetableLevelSize(level);
                const auto scaled=clampFinite(p.position,0,1,0)*static_cast<float>(wavetableFrames-1);
                const auto first=std::min(static_cast<std::size_t>(scaled),wavetableFrames-2);const auto blend=scaled-static_cast<float>(first);
                const auto* a=wave->frame(first,level);const auto* b=wave->frame(first+1,level);
                for(std::size_t c=0;c<static_cast<std::size_t>(copies);++c)
                {
                    const auto t=phases_[c];const auto dt=std::min(.45,static_cast<double>(fundamental)*detuneMultiplier_[c]/sampleRate_);
                    const auto x=t*static_cast<double>(size);const auto i=std::min(static_cast<std::size_t>(x),size-1);const auto f=static_cast<float>(x-static_cast<double>(i));
                    const auto sa=a[i]+(a[i+1]-a[i])*f,sb=b[i]+(b[i+1]-b[i])*f;const auto s=sa+(sb-sa)*blend;
                    l+=s*unisonPanLeft_[c];r+=s*unisonPanRight_[c];
                    phases_[c]=t+dt;if(phases_[c]>=1.0)phases_[c]-=1.0;
                }
            }
        }
        else if(kType>=domain::ModuleType::chorus&&kType<=domain::ModuleType::width)
        {
        if(kType==domain::ModuleType::chorus&&effect_)
        {
            const auto taps=std::clamp(p.voices,2,4);
            const auto depth=clampFinite(p.depth,0,1,.3f),feedback=clampFinite(p.feedback,0,.9f,0),mix=clampFinite(p.mix,0,1,.3f);
            const auto gain=std::numbers::sqrt2_v<float>/std::sqrt(static_cast<float>(taps));
            const auto ceiling=static_cast<float>(effect_->getMaximumDelayInSamples()-2);
            float wetL=0,wetR=0;
            for(int t=0;t<taps;++t)
            {
                const auto milliseconds=chorusBaseMilliseconds+depth*chorusSweepMilliseconds*(.5f+.5f*fastSin(phases_[0]+twoPi*t/taps));
                const auto delaySamples=std::clamp(static_cast<float>(milliseconds*.001*sampleRate_),1.0f,ceiling);
                const auto position=2.0f*static_cast<float>(t)/static_cast<float>(taps-1)-1.0f;
                // The last tap advances the read pointer for the sample; the earlier ones only read.
                const bool advance=t+1==taps;
                wetL+=effect_->popSample(0,delaySamples,advance)*gain*panGain(position,false);
                wetR+=effect_->popSample(1,delaySamples,advance)*gain*panGain(position,true);
            }
            phases_[0]+=twoPi*clampFinite(p.rate,.01f,8,.5f)/sampleRate_;if(phases_[0]>std::numbers::pi_v<double>)phases_[0]-=twoPi;
            effect_->pushSample(0,l+wetL*feedback);effect_->pushSample(1,r+wetR*feedback);
            l=std::lerp(l,wetL,mix);r=std::lerp(r,wetR,mix);
        }
        // D8 stereo delay (#123). `syncMode` picks between the free `timeMs` and `syncDivision`
        // against the host tempo (`fallbackTempoBpm` when the host has no playhead); `spread` skews
        // the two channels' times apart; `damping` is a one-pole inside the feedback path, so each
        // repeat is darker than the one before it.
        else if(kType==domain::ModuleType::delay&&effect_)
        {
            const auto mix=clampFinite(p.mix,0,1,.3f),feedback=clampFinite(p.feedback,0,.95f,.35f);
            const auto damping=clampFinite(p.damping,0,1,.4f),spread=clampFinite(p.spread,-1,1,0);
            const auto seconds=p.syncMode==1?syncDivisionBeats[static_cast<std::size_t>(std::clamp(p.syncDivision,0,6))]*60.0/tempo_
                                            :clampFinite(p.timeMs,1,2000,375)*.001;
            const auto ceiling=static_cast<float>(effect_->getMaximumDelayInSamples()-2);
            const auto base=std::clamp(seconds,.001,2.0)*sampleRate_;
            const auto leftSamples=std::clamp(static_cast<float>(base*(1.0-delaySpreadRange*spread)),1.0f,ceiling);
            const auto rightSamples=std::clamp(static_cast<float>(base*(1.0+delaySpreadRange*spread)),1.0f,ceiling);
            const auto wetL=effect_->popSample(0,leftSamples),wetR=effect_->popSample(1,rightSamples);
            const auto coefficient=1.0f-delayDampingRange*damping;
            effectDampLeft_+=coefficient*(wetL-effectDampLeft_);effectDampRight_+=coefficient*(wetR-effectDampRight_);
            effect_->pushSample(0,l+effectDampLeft_*feedback);effect_->pushSample(1,r+effectDampRight_*feedback);
            l=std::lerp(l,wetL,mix);r=std::lerp(r,wetR,mix);
        }
        // D8 reverb (#124). The network is prepared once for the whole tail; here it is one sample
        // in and a dry/wet. `mix` 0 is std::lerp(l, ., 0) — the literal dry sample, so a reverb
        // node parked at mix 0 is a straight wire exactly as the chorus and the delay are.
        else if(kType==domain::ModuleType::reverb&&reverb_)
        {
            const auto mix=clampFinite(p.mix,0,1,.25f);
            float wetL=0,wetR=0;
            reverb_->process(l,r,clampFinite(p.size,0,1,.5f),clampFinite(p.decaySeconds,.1f,20,2),
                             clampFinite(p.damping,0,1,.5f),clampFinite(p.preDelayMs,0,200,20),
                             clampFinite(p.width,0,1,1),wetL,wetR);
            l=std::lerp(l,wetL,mix);r=std::lerp(r,wetR,mix);
        }
        // D8 stereo width (#124). Mid/side arithmetic around one TPT crossover, and the whole
        // signal — there is no `mix`, because a partly applied width transform is just a different
        // width. The crossover splits the side signal into a low band and its exact complement
        // (`side - low`, so the two sum back to `side` with no filter-bank error): narrowing scales
        // all of the side, widening adds only the band above `bassMonoHz`, which is what keeps the
        // bass mono as the image opens. `width` at its default 1 is the literal identity — the
        // branch leaves `l` and `r` untouched — while the crossover still runs, so its state never
        // goes stale under a macro sweeping the control.
        else if(kType==domain::ModuleType::width)
        {
            const auto amount=clampFinite(p.width,0,2,1);
            const auto mid=(l+r)*.5f,side=(l-r)*.5f;
            const auto low=filter_.processSample(0,side);
            if(amount!=1.0f)
            {
                const auto shaped=side*std::min(amount,1.0f)+(side-low)*std::max(amount-1.0f,0.0f);
                l=mid+shaped;r=mid-shaped;
            }
        }
        }
        else{const auto g=clampFinite(p.level,0,1,1);l*=g*cachedPanLeft_;r*=g*cachedPanRight_;}outL[i]=std::isfinite(l)?l*output:0;outR[i]=std::isfinite(r)?r*output:0;}
}
void ModulatorBank::prepare(double rate) noexcept {sampleRate_=std::max(1.0,rate);smoothingLength_=static_cast<std::uint64_t>(std::max(1.0,sampleRate_*.020));for(auto& e:envelopes_)e.setSampleRate(sampleRate_);updateDerived();}
namespace {
// Stage bend. `curve` 0 is the straight line the JUCE ADSR already draws; the exponential form
// below keeps both endpoints exact (warp(0)=0, warp(1)=1) and stays monotone for every strength,
// so a bent stage has the same duration, the same start and the same end as the flat one.
constexpr float stageCurveStrength=4.0f;
float stageWarp(float progress,float strength,float normalizer) noexcept
{
    progress=std::clamp(progress,0.f,1.f);
    return (1.0f-std::exp(-strength*progress))*normalizer;
}
float stageNormalizer(float strength) noexcept {const auto d=1.0f-std::exp(-strength);return d==0.f?1.0f:1.0f/d;}
// One deterministic draw from an LFO's own patch-seeded xorshift stream, in [-1, 1].
float nextLfoRandom(std::uint32_t& state) noexcept
{
    state^=state<<13;state^=state>>17;state^=state<<5;
    return static_cast<float>(static_cast<double>(state)/static_cast<double>(std::numeric_limits<std::uint32_t>::max()))*2.0f-1.0f;
}
std::uint32_t lfoSeed(std::uint32_t patchSeed,std::size_t index) noexcept
{
    const auto mixed=patchSeed*2654435761u+static_cast<std::uint32_t>(index+1)*2246822519u+1u;
    return mixed==0?1u:mixed;
}
}
void ModulatorBank::configure(const std::array<domain::Envelope,domain::envelopeCount>& es,const std::array<domain::Lfo,domain::lfoCount>& ls,std::uint32_t patchSeed) noexcept
{
    previousLfoSettings_=lfoSettings_;lfoSettings_=ls;smoothingSample_=0;patchSeed_=patchSeed;
    for(std::size_t i=0;i<domain::envelopeCount;++i)
    {
        juce::ADSR::Parameters p;p.attack=static_cast<float>(std::clamp(es[i].attack,.001,2.0));p.decay=static_cast<float>(std::clamp(es[i].decay,.01,4.0));p.sustain=static_cast<float>(std::clamp(es[i].sustain,0.0,1.0));p.release=static_cast<float>(std::clamp(es[i].release,.02,6.0));envelopes_[i].setParameters(p);
        auto& shaper=shapers_[i];shaper.sustain=p.sustain;
        // The exp() normalizers are resolved here, not per sample: a curve of 0 stores a strength of
        // exactly 0 and the shaper's fast path never evaluates exp() at all.
        const auto curve=[](double value){return static_cast<float>(std::clamp(value,-1.0,1.0))*stageCurveStrength;};
        shaper.attackWarp=curve(es[i].attackCurve);shaper.decayWarp=curve(es[i].decayCurve);shaper.releaseWarp=curve(es[i].releaseCurve);
        shaper.attackNorm=stageNormalizer(shaper.attackWarp);shaper.decayNorm=stageNormalizer(shaper.decayWarp);shaper.releaseNorm=stageNormalizer(shaper.releaseWarp);
    }
    curvesActive_=std::ranges::any_of(shapers_,[](const StageShaper& shaper){return shaper.attackWarp!=0.f||shaper.decayWarp!=0.f||shaper.releaseWarp!=0.f;});
    updateDerived();
}
void ModulatorBank::setTempo(double beatsPerMinute) noexcept {tempo_=beatsPerMinute>=20.0&&beatsPerMinute<=999.0?beatsPerMinute:fallbackTempoBpm;updateDerived();}
// Resolved once per publication or tempo change, never per sample: the modulator loop runs for
// every voice on every sample, so a sync division lookup or a fade length has no business there.
void ModulatorBank::updateDerived() noexcept
{
    for(std::size_t i=0;i<domain::lfoCount;++i)
    {
        rateHz_[i]=lfoRate(lfoSettings_[i]);previousRateHz_[i]=lfoRate(previousLfoSettings_[i]);
        const auto fadeMs=std::clamp(lfoSettings_[i].fadeMs,0.0,5000.0);
        fadeSamples_[i]=fadeMs>0.0?std::max(1.0,fadeMs*.001*sampleRate_):0.0;
    }
}
void ModulatorBank::reset() noexcept {lfoPhases_.fill(0);sampleSinceNoteOn_=0;for(auto&e:envelopes_)e.reset();for(auto&s:shapers_){s.lastValue=s.lastShaped=s.releaseFrom=s.releaseShaped=0;s.attacking=s.releasing=false;}
    for(std::size_t i=0;i<domain::lfoCount;++i){lfoRandom_[i]=lfoSeed(patchSeed_,i);lfoPreviousHold_[i]=0;lfoHold_[i]=nextLfoRandom(lfoRandom_[i]);}}
void ModulatorBank::noteOn() noexcept {lfoPhases_.fill(0);sampleSinceNoteOn_=0;for(auto& e:envelopes_)e.noteOn();for(auto&s:shapers_){s.attacking=true;s.releasing=false;s.lastValue=s.lastShaped=0;}
    // The random shapes restart from the patch seed on every note-on, never from wall-clock state,
    // so two renders of the same patch and the same MIDI produce the same stream (V2).
    for(std::size_t i=0;i<domain::lfoCount;++i){lfoRandom_[i]=lfoSeed(patchSeed_,i);lfoPreviousHold_[i]=0;lfoHold_[i]=nextLfoRandom(lfoRandom_[i]);}}
void ModulatorBank::noteOff() noexcept {for(std::size_t i=0;i<domain::envelopeCount;++i){envelopes_[i].noteOff();auto& s=shapers_[i];if(!s.releasing){s.releasing=true;s.attacking=false;s.releaseFrom=s.lastValue;s.releaseShaped=s.lastShaped;}}}
float ModulatorBank::shapeEnvelope(std::size_t index,float value) noexcept
{
    auto& s=shapers_[index];float shaped=value;
    if(s.releasing)
    {
        // The release always ends at zero and starts from wherever the envelope was, so a flat
        // release with a flat attack before it returns the ADSR sample itself, untouched.
        const float scale=s.releaseFrom>0.f?s.releaseShaped/s.releaseFrom:1.0f;
        shaped=s.releaseWarp==0.f?value*scale
             :s.releaseShaped*(1.0f-stageWarp(s.releaseFrom>0.f?1.0f-value/s.releaseFrom:1.0f,s.releaseWarp,s.releaseNorm));
    }
    else
    {
        if(s.attacking){if(value>=1.0f)s.attacking=false;else shaped=s.attackWarp==0.f?value:stageWarp(value,s.attackWarp,s.attackNorm);}
        if(!s.attacking)
        {
            // Decay and sustain: the ADSR runs from 1 down to the sustain level, so the stage
            // progress is what the curve bends, and the sustain level itself is an endpoint.
            if(s.decayWarp==0.f)shaped=value;
            else{const float progress=s.sustain<1.0f?std::clamp((1.0f-value)/(1.0f-s.sustain),0.f,1.0f):1.0f;shaped=1.0f+(s.sustain-1.0f)*stageWarp(progress,s.decayWarp,s.decayNorm);}
        }
    }
    s.lastValue=value;s.lastShaped=shaped;return shaped;
}
float ModulatorBank::lfoRate(const domain::Lfo& lfo) const noexcept
{
    if(lfo.syncMode!=domain::LfoSyncMode::sync)return static_cast<float>(lfo.rate);
    const auto beats=domain::lfoSyncBeats[std::min<std::size_t>(static_cast<std::size_t>(lfo.syncDivision),domain::lfoSyncDivisionCount-1)];
    return static_cast<float>(tempo_/(60.0*beats));
}
std::array<float,6> ModulatorBank::next() noexcept
{
    std::array<float,6> r{};
    // E1..E3, L1..L2, then E4: the persisted source order, which `e4` appends to. With every stage
    // curve at 0 the shaper is skipped outright, so the default patch pays one predictable branch.
    if(curvesActive_)for(std::size_t i=0;i<domain::envelopeCount;++i)r[i<3?i:5]=shapeEnvelope(i,envelopes_[i].getNextSample());
    else for(std::size_t i=0;i<domain::envelopeCount;++i)r[i<3?i:5]=envelopes_[i].getNextSample();
    const float x=smoothingSample_>=smoothingLength_?1.f:static_cast<float>(smoothingSample_)/static_cast<float>(smoothingLength_);
    for(std::size_t i=0;i<2;++i)
    {
        const auto ph=lfoPhases_[i];
        const auto wave=[&](domain::LfoWaveform waveform)->float{
            switch(waveform)
            {
                case domain::LfoWaveform::sine:return fastSin(ph);
                case domain::LfoWaveform::triangle:{const auto scaled=static_cast<float>(2*ph/std::numbers::pi_v<double>);return ph>std::numbers::pi_v<double>*.5?2-scaled:ph<-std::numbers::pi_v<double>*.5?-2-scaled:scaled;}
                case domain::LfoWaveform::saw:return static_cast<float>(ph/std::numbers::pi_v<double>);
                case domain::LfoWaveform::square:return ph<0?-1.f:1.f;
                case domain::LfoWaveform::sampleHold:return lfoHold_[i];
                case domain::LfoWaveform::randomSmooth:{const auto t=static_cast<float>((ph+std::numbers::pi_v<double>)/twoPi);const auto smooth=t*t*(3.0f-2.0f*t);return std::lerp(lfoPreviousHold_[i],lfoHold_[i],smooth);}
            }
            return 0;};
        // Outside a transaction's crossfade only the live shape and the live rate are evaluated;
        // the previous settings cost nothing once `x` has reached 1.
        const bool blending=x<1.f;
        float value=blending?std::lerp(wave(previousLfoSettings_[i].waveform),wave(lfoSettings_[i].waveform),x):wave(lfoSettings_[i].waveform);
        // `fadeMs` ramps the LFO's depth up from zero after note-on. 0 ms is no multiplication at all.
        if(fadeSamples_[i]>0.0&&static_cast<double>(sampleSinceNoteOn_)<fadeSamples_[i])value*=static_cast<float>(static_cast<double>(sampleSinceNoteOn_)/fadeSamples_[i]);
        r[3+i]=value;
        const auto rate=blending?std::lerp(previousRateHz_[i],rateHz_[i],x):rateHz_[i];
        lfoPhases_[i]=ph+twoPi*std::clamp(static_cast<double>(rate),.05,12.0)/sampleRate_;
        // A wrap is one cycle: both random shapes take exactly one new draw there, so the stream a
        // patch produces depends only on its seed and on how many cycles have elapsed.
        if(lfoPhases_[i]>std::numbers::pi_v<double>){lfoPhases_[i]-=twoPi;lfoPreviousHold_[i]=lfoHold_[i];lfoHold_[i]=nextLfoRandom(lfoRandom_[i]);}
    }
    if(smoothingSample_<smoothingLength_)++smoothingSample_;
    ++sampleSinceNoteOn_;
    return r;
}
}
