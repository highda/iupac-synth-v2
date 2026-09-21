#include "iupac/engine/Engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <utility>

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
    // --- D8 spectral shape: harmonicityMorph, oddEvenBalance, symmetry (#121) ------------------
    {
        // An inharmonic spectrum, the kind the chemistry mapper actually produces: nothing lands on
        // an integer, so every control below has something to do.
        engine::ModuleValues bell;for(std::size_t i=0;i<16;++i){bell.ratios[i]=static_cast<float>(i+1)*1.31f+0.17f;bell.amplitudes[i]=1.0f/static_cast<float>(i+1);}
        const auto reference=renderSource(domain::ModuleType::harmonic,bell,60,123,4);
        // Catalog defaults are the literal identity, so a pre-D8 spectrum keeps its exact samples.
        auto defaults=bell;defaults.harmonicityMorph=0;defaults.oddEvenBalance=0;defaults.symmetry=.5f;
        const auto unchanged=renderSource(domain::ModuleType::harmonic,defaults,60,123,4);
        ok&=expect(unchanged[0]==reference[0]&&unchanged[1]==reference[1],"spectral-shape defaults render the pre-D8 spectrum bit for bit");
        // Each control moves the render on its own, and none of them touches the stored arrays.
        for(const auto& [name,edit]:std::initializer_list<std::pair<const char*,engine::ModuleValues>>{
                {"harmonicityMorph",[&]{auto v=bell;v.harmonicityMorph=1;return v;}()},
                {"oddEvenBalance",[&]{auto v=bell;v.oddEvenBalance=1;return v;}()},
                {"symmetry",[&]{auto v=bell;v.symmetry=1;return v;}()}})
        {
            const auto shaped=renderSource(domain::ModuleType::harmonic,edit,60,123,4);
            ok&=expect(finiteActive(shaped[0]),name);
            ok&=expect(shaped[0]!=reference[0],name);
        }
        // At full morph every ratio is its nearest integer, so morphing the bell renders exactly
        // what the already-integer spectrum renders at morph 0 — the audible point of the control.
        auto morphed=bell;morphed.harmonicityMorph=1;
        auto integers=bell;for(std::size_t i=0;i<16;++i)integers.ratios[i]=std::round(bell.ratios[i]);
        ok&=expect(renderSource(domain::ModuleType::harmonic,morphed,60,123,4)[0]==renderSource(domain::ModuleType::harmonic,integers,60,123,4)[0],
                   "full harmonicity morph lands on the nearest integer series");
        // Culling above 0.45 of the internal rate runs on the *morphed* ratio, not the stored one.
        // At a 2 kHz fundamental and 96 kHz internally the limit is 43.2 kHz: ratio 21.55 sits just
        // under it and sounds, its nearest integer 22 sits above it and is dropped.
        auto renderAt=[&](const engine::ModuleValues& values,float fundamental){
            engine::ModuleProcessor m(domain::ModuleType::harmonic);m.prepare(96000,512);m.noteOn(60,1,123,456);
            std::array<float,512> zl{},zr{},ol{},orr{};m.process(values,fundamental,zl,zr,ol,orr);return ol;};
        engine::ModuleValues edge;edge.amplitudes[0]=1;edge.ratios[0]=21.55f;
        ok&=expect(finiteActive(renderAt(edge,2000.0f)),"a partial just under the cull limit still sounds");
        auto over=edge;over.harmonicityMorph=1;
        ok&=expect(std::ranges::all_of(renderAt(over,2000.0f),[](float x){return x==0;}),
                   "the morph pushes a partial past the cull limit and it is dropped");
        // oddEvenBalance attenuates only the group out of favour, so +1 silences the even partials
        // and -1 the odd ones, and neither ever boosts a partial past its stored amplitude.
        engine::ModuleValues odd;odd.amplitudes[0]=1;odd.ratios[0]=1;odd.ratios[1]=2;
        engine::ModuleValues even;even.amplitudes[1]=1;even.ratios[0]=1;even.ratios[1]=2;
        auto favourOdd=[](engine::ModuleValues v){v.oddEvenBalance=1;return v;};
        auto favourEven=[](engine::ModuleValues v){v.oddEvenBalance=-1;return v;};
        ok&=expect(renderSource(domain::ModuleType::harmonic,favourOdd(odd),60,123)[0]==renderSource(domain::ModuleType::harmonic,odd,60,123)[0],
                   "oddEvenBalance +1 leaves the odd partials untouched");
        ok&=expect(std::ranges::all_of(renderSource(domain::ModuleType::harmonic,favourOdd(even),60,123)[0],[](float x){return x==0;}),
                   "oddEvenBalance +1 silences the even partials");
        ok&=expect(std::ranges::all_of(renderSource(domain::ModuleType::harmonic,favourEven(odd),60,123)[0],[](float x){return x==0;}),
                   "oddEvenBalance -1 silences the odd partials");
        ok&=expect(renderSource(domain::ModuleType::harmonic,favourEven(even),60,123)[0]==renderSource(domain::ModuleType::harmonic,even,60,123)[0],
                   "oddEvenBalance -1 leaves the even partials untouched");
        // symmetry tilts across the partial index: 1 keeps the top partial and drops the first, 0
        // the reverse, and both endpoints are attenuation only.
        engine::ModuleValues top;top.amplitudes[15]=1;for(std::size_t i=0;i<16;++i)top.ratios[i]=static_cast<float>(i+1);
        engine::ModuleValues bottom;bottom.amplitudes[0]=1;for(std::size_t i=0;i<16;++i)bottom.ratios[i]=static_cast<float>(i+1);
        auto at=[](engine::ModuleValues v,float s){v.symmetry=s;return v;};
        ok&=expect(std::ranges::all_of(renderSource(domain::ModuleType::harmonic,at(bottom,1),60,123)[0],[](float x){return x==0;}),"symmetry 1 drops the first partial");
        ok&=expect(renderSource(domain::ModuleType::harmonic,at(top,1),60,123)[0]==renderSource(domain::ModuleType::harmonic,top,60,123)[0],"symmetry 1 keeps the top partial");
        ok&=expect(std::ranges::all_of(renderSource(domain::ModuleType::harmonic,at(top,0),60,123)[0],[](float x){return x==0;}),"symmetry 0 drops the top partial");
        ok&=expect(renderSource(domain::ModuleType::harmonic,at(bottom,0),60,123)[0]==renderSource(domain::ModuleType::harmonic,bottom,60,123)[0],"symmetry 0 keeps the first partial");
        // Out-of-range values are clamped, never propagated as NaN.
        auto wild=bell;wild.harmonicityMorph=9;wild.oddEvenBalance=-9;wild.symmetry=std::numeric_limits<float>::quiet_NaN();
        ok&=expect(std::ranges::all_of(renderSource(domain::ModuleType::harmonic,wild,60,123,4)[0],[](float x){return std::isfinite(x);}),"spectral shape clamps its controls");
    }
    // --- D8 pitch block and the sub oscillator (#122) ------------------------------------------
    {
        // The engine hands every module the voice fundamental, so a pitch test has to move the note
        // and the frequency together exactly as Engine::Impl does.
        auto renderNote=[&](domain::ModuleType type,const engine::ModuleValues& values,int note,int blocks=1){
            engine::ModuleProcessor m(type);m.prepare(96000,512);m.noteOn(note,1,123,456);
            std::array<float,512> zl{},zr{},ol{},orr{};std::array<std::array<float,512>,2> last{};
            for(int b=0;b<blocks;++b){m.process(values,440.0f*std::exp2(static_cast<float>(note-69)/12.0f),zl,zr,ol,orr);last={ol,orr};}
            return last;};
        for(auto type:{domain::ModuleType::harmonic,domain::ModuleType::fm})
        {
            engine::ModuleValues base;base.amplitudes[0]=1;for(std::size_t i=0;i<16;++i)base.ratios[i]=static_cast<float>(i+1);
            const auto reference=renderNote(type,base,60,4);
            // Catalog defaults are the literal identity: the offset is exactly zero semitones.
            auto defaults=base;defaults.octave=0;defaults.coarse=0;defaults.fine=0;defaults.keytrack=1;
            ok&=expect(renderNote(type,defaults,60,4)==reference,"pitch-block defaults render the pre-D8 source bit for bit");
            // The three transpositions are the same offset expressed three ways: one octave up is
            // twelve semitones up is what the note an octave higher renders.
            auto up=base;up.octave=1;
            auto twelve=base;twelve.coarse=12;
            const auto octaveUp=renderNote(type,up,60,4);
            ok&=expect(octaveUp==renderNote(type,twelve,60,4),"octave +1 and coarse +12 are the same transposition");
            ok&=expect(octaveUp!=reference,"octave transposes the source away from the note");
            auto hundredCents=base;hundredCents.fine=100;
            auto oneSemitone=base;oneSemitone.coarse=1;
            ok&=expect(renderNote(type,hundredCents,60,4)==renderNote(type,oneSemitone,60,4),"fine 100 cents is one coarse semitone");
            // `keytrack` 0 pins the source to the C4 reference: the same samples at every note, which
            // is exactly a source an octave down when the note itself is an octave up.
            auto pinned=base;pinned.keytrack=0;
            ok&=expect(renderNote(type,pinned,72,4)==renderNote(type,pinned,60,4),"keytrack 0 renders the same pitch whatever the note");
            auto down=base;down.octave=-1;
            ok&=expect(renderNote(type,pinned,72,4)==renderNote(type,down,72,4),"keytrack 0 at C5 is the C5 voice transposed an octave down");
            ok&=expect(renderNote(type,base,72,4)!=renderNote(type,base,60,4),"keytrack 1 follows the keyboard");
            // Out-of-range values are clamped, never propagated.
            auto wild=base;wild.octave=99;wild.coarse=-99;wild.fine=std::numeric_limits<float>::quiet_NaN();wild.keytrack=-5;
            ok&=expect(std::ranges::all_of(renderNote(type,wild,60,4)[0],[](float x){return std::isfinite(x);}),"the pitch block clamps its controls");
        }
        // The sub oscillator itself. A low internal rate makes a whole number of cycles fit one
        // block, so the rendered frequency can be read off the zero crossings rather than assumed.
        auto subCrossings=[&](int waveform,int octave,int note,float tuneRatio=1.0f){
            engine::ModuleProcessor m(domain::ModuleType::sub);m.prepare(8000,512);m.noteOn(note,1,7,11);
            engine::ModuleValues v;v.outputLevel=1;v.waveform=waveform;v.octave=octave;
            std::array<float,512> zl{},zr{},ol{},orr{};m.process(v,440.0f*std::exp2(static_cast<float>(note-69)/12.0f)*tuneRatio,zl,zr,ol,orr);
            int crossings=0;for(std::size_t i=1;i<ol.size();++i)if((ol[i-1]<0)!=(ol[i]<0))++crossings;
            return std::pair{crossings,ol};};
        // C5 is 523.25 Hz; 512 samples at 8 kHz is 64 ms, so one octave down is 33 zero crossings
        // and two octaves down is 16. That is the contract "one or two octaves below the fundamental".
        const auto [oneDown,sine]=subCrossings(0,-1,72);
        const auto [twoDown,deeper]=subCrossings(0,-2,72);
        ok&=expect(std::abs(oneDown-33)<=2,"the sub renders one octave below the voice fundamental");
        ok&=expect(std::abs(twoDown-16)<=2,"octave -2 renders two octaves below the voice fundamental");
        ok&=expect(finiteActive(sine)&&finiteActive(deeper),"the sub renders a finite signal");
        // masterTune and bend arrive as the fundamental itself, so the sub follows them like every
        // other pitched source: an octave of bend doubles the rendered frequency.
        ok&=expect(std::abs(subCrossings(0,-1,72,2.0f).first-2*oneDown)<=2,"the sub follows a transposed fundamental");
        // The triangle is a different shape at the same frequency, and neither is a wavetable read.
        const auto [triangleCrossings,triangle]=subCrossings(1,-1,72);
        ok&=expect(triangleCrossings==oneDown,"the triangle runs at the sine's frequency");
        ok&=expect(triangle!=sine&&finiteActive(triangle),"the triangle waveform is a different shape");
        // The sub takes the same pitch block: a note an octave up at octave -2 is the same pitch.
        auto renderSub=[&](int octave,float fine,float keytrack,int note){
            engine::ModuleProcessor m(domain::ModuleType::sub);m.prepare(96000,512);m.noteOn(note,1,7,11);
            engine::ModuleValues v;v.outputLevel=1;v.octave=octave;v.fine=fine;v.keytrack=keytrack;
            std::array<float,512> zl{},zr{},ol{},orr{};for(int b=0;b<4;++b)m.process(v,440.0f*std::exp2(static_cast<float>(note-69)/12.0f),zl,zr,ol,orr);return ol;};
        ok&=expect(renderSub(-2,0,1,84)==renderSub(-1,0,1,72),"the sub octave is measured from the note it is playing");
        ok&=expect(renderSub(-1,0,0,72)==renderSub(-1,0,0,84),"keytrack 0 pins the sub to one pitch");
        ok&=expect(renderSub(-1,50,1,72)!=renderSub(-1,0,1,72),"fine detunes the sub");
    }
    engine::ModulatorBank mods;mods.prepare(48000);std::array<domain::Envelope,domain::envelopeCount> es{};std::array<domain::Lfo,domain::lfoCount> ls{};ls[0].waveform=domain::LfoWaveform::triangle;mods.configure(es,ls);mods.noteOn();auto a=mods.next();ok&=expect(a[0]>=0&&a[3]>=-1&&a[3]<=1,"ADSR and LFO run");mods.noteOff();
    return ok;
}
