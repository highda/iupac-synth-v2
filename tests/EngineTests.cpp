#include "iupac/engine/Engine.hpp"
#include "iupac/engine/PatchCoordinator.hpp"
#include "MaximalPatch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numeric>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace iupac;
domain::Node defaults(std::string id,std::string_view type){auto*d=domain::findModule(type);domain::Node n{std::move(id),d->type,{}};for(auto&p:d->parameters)n.parameters.push_back({std::string(p.id),std::vector<double>(p.arraySize?p.arraySize:1,p.defaultValue)});if(type=="harmonic"){n.parameters[0].values[0]=1;for(std::size_t i=0;i<16;++i)n.parameters[1].values[i]=static_cast<double>(i+1);}return n;}
domain::Patch graphPatch(bool serial=true){domain::Patch p;p.noiseSeed=77;p.nodes={defaults("a","harmonic"),defaults("b","filter"),defaults("c","mixer"),defaults("unused","noise")};p.edges=serial?std::vector<domain::AudioEdge>{{"a","b",1},{"b","c",1},{"c","output",1}}:std::vector<domain::AudioEdge>{{"a","b",1},{"b","c",.5},{"a","c",.5},{"c","output",1}};p.matrix={{"m1",true,domain::ModulationSource::l1,"b","cutoff",.8},{"m2",true,domain::ModulationSource::velocity,"c","pan",.5},{"m3",true,domain::ModulationSource::macro1,"c","level",-.5}};p.macros[0]={"Shape",.2};return p;}
bool expect(bool v,const char*m){if(!v)std::cerr<<"engine test failed: "<<m<<'\n';return v;}
float energy(const std::vector<float>&v){return std::inner_product(v.begin(),v.end(),v.begin(),0.f);}
float targetValue(const engine::ModuleValues&v,engine::ParameterTarget t){switch(t){case engine::ParameterTarget::carrierRatio:return v.carrierRatio;case engine::ParameterTarget::modulatorRatio:return v.modulatorRatio;case engine::ParameterTarget::index:return v.index;case engine::ParameterTarget::burstMilliseconds:return v.burstMilliseconds;case engine::ParameterTarget::tuneRatio:return v.tuneRatio;case engine::ParameterTarget::combFeedback:return v.combFeedback;case engine::ParameterTarget::modalQ:return v.modalQ;case engine::ParameterTarget::cutoff:return v.cutoff;case engine::ParameterTarget::q:return v.q;case engine::ParameterTarget::drive:return v.drive;case engine::ParameterTarget::wet:return v.wet;case engine::ParameterTarget::level:return v.level;case engine::ParameterTarget::pan:return v.pan;case engine::ParameterTarget::outputLevel:return v.outputLevel;}return 0;}
void setTarget(engine::ModuleValues&v,engine::ParameterTarget t,float x){switch(t){case engine::ParameterTarget::carrierRatio:v.carrierRatio=x;break;case engine::ParameterTarget::modulatorRatio:v.modulatorRatio=x;break;case engine::ParameterTarget::index:v.index=x;break;case engine::ParameterTarget::burstMilliseconds:v.burstMilliseconds=x;break;case engine::ParameterTarget::tuneRatio:v.tuneRatio=x;break;case engine::ParameterTarget::combFeedback:v.combFeedback=x;break;case engine::ParameterTarget::modalQ:v.modalQ=x;break;case engine::ParameterTarget::cutoff:v.cutoff=x;break;case engine::ParameterTarget::q:v.q=x;break;case engine::ParameterTarget::drive:v.drive=x;break;case engine::ParameterTarget::wet:v.wet=x;break;case engine::ParameterTarget::level:v.level=x;break;case engine::ParameterTarget::pan:v.pan=x;break;case engine::ParameterTarget::outputLevel:v.outputLevel=x;break;}}
std::vector<float> render(const engine::CompiledPatch&p,std::size_t block,domain::HostControls controls={},double tempo=engine::fallbackTempoBpm,std::uint8_t velocity=100,std::size_t samples=2048){engine::Engine e;e.prepare(48000,block);e.setTempo(tempo);e.setPatch(p);e.setControls(controls);std::vector<float>l(samples),r(samples);std::array events{engine::MidiEvent{0,engine::MidiEventType::noteOn,1,60,velocity,8192},engine::MidiEvent{1500,engine::MidiEventType::noteOff,1,60,0,8192}};e.render(l,r,events);return l;}
float distance(const std::vector<float>&a,const std::vector<float>&b){float d=0;for(std::size_t i=0;i<std::min(a.size(),b.size());++i)d+=std::abs(a[i]-b[i]);return d;}
// D8 effects tail (#123). A source into a mixer, the mixer into the ordered effects nodes, the last
// effect into OUT. `order` names the effects types in the order they should be chained.
domain::Patch tailPatch(std::vector<std::string_view> order){domain::Patch p;p.noiseSeed=91;p.nodes={defaults("a","harmonic"),defaults("m","mixer")};p.edges={{"a","m",1}};
 std::string previous="m";for(std::size_t i=0;i<order.size();++i){auto id="x"+std::to_string(i);p.nodes.push_back(defaults(id,order[i]));p.edges.push_back({previous,id,1});previous=id;}
 p.edges.push_back({previous,"output",1});return p;}
domain::Node*nodeNamed(domain::Patch&p,std::string_view id){for(auto&n:p.nodes)if(n.id==id)return &n;return nullptr;}
void setParameter(domain::Patch&p,std::string_view id,std::string_view parameter,double value){if(auto*n=nodeNamed(p,id))for(auto&v:n->parameters)if(v.id==parameter)v.values[0]=value;}
}

bool runEngineTests(){bool ok=true;auto patch=graphPatch();auto compiled=engine::compilePatch(patch);ok&=expect(static_cast<bool>(compiled),"valid graph compiles");ok&=expect(compiled.patch.nodeCount==3,"disconnected node pruned");ok&=expect(compiled.patch.nodes[0].type==domain::ModuleType::harmonic&&compiled.patch.nodes[2].type==domain::ModuleType::mixer,"stable topological order");ok&=expect(compiled.patch.rowCount==3,"enabled live rows resolved");auto signal=render(compiled.patch,128);ok&=expect(energy(signal)>1e-4f,"compiled graph renders audio");ok&=expect(std::ranges::all_of(signal,[](float x){return std::isfinite(x)&&std::abs(x)<=.8912511f;}),"stereo safety guard bounds output");
 auto parallel=engine::compilePatch(graphPatch(false));auto changed=render(parallel.patch,128);float delta=0;for(std::size_t i=0;i<signal.size();++i)delta+=std::abs(signal[i]-changed[i]);ok&=expect(delta>1.f,"graph rewiring changes production render");
 auto allSources=graphPatch();allSources.matrix.clear();for(std::size_t i=0;i<=static_cast<std::size_t>(domain::ModulationSource::macro4);++i)allSources.matrix.push_back({"source-"+std::to_string(i),true,static_cast<domain::ModulationSource>(i),"b","cutoff",.1});auto sourceCompile=engine::compilePatch(allSources);ok&=expect(static_cast<bool>(sourceCompile)&&sourceCompile.patch.rowCount==13,"every matrix source resolves");
 auto noRows=graphPatch();noRows.matrix.clear();auto base=engine::compilePatch(noRows);domain::HostControls controls;controls.macros[0]=1;auto modulated=render(compiled.patch,128,controls),unmodulated=render(base.patch,128,controls);delta=0;for(std::size_t i=0;i<signal.size();++i)delta+=std::abs(modulated[i]-unmodulated[i]);ok&=expect(delta>.1f,"matrix rows change production render");
 for(std::size_t sourceIndex=0;sourceIndex<engine::ModulationInputs{}.size();++sourceIndex)for(std::size_t targetIndex=0;targetIndex<=static_cast<std::size_t>(engine::ParameterTarget::outputLevel);++targetIndex){const auto target=static_cast<engine::ParameterTarget>(targetIndex);engine::CompiledPatch sum;sum.rowCount=2;sum.rows[0]={static_cast<domain::ModulationSource>(sourceIndex),0,target,.5f,0,1,domain::ParameterScale::linear};sum.rows[1]={static_cast<domain::ModulationSource>(sourceIndex),0,target,-.5f,0,1,domain::ParameterScale::linear};engine::ModuleValues values;setTarget(values,target,.9f);engine::ModulationInputs inputs{};inputs[sourceIndex]=1;auto forward=engine::applyModulation(sum,0,values,inputs);std::swap(sum.rows[0],sum.rows[1]);auto reverse=engine::applyModulation(sum,0,values,inputs);ok&=expect(std::abs(targetValue(forward,target)-.9f)<1e-6f&&std::abs(targetValue(reverse,target)-.9f)<1e-6f,"matrix sums once independent of row order for every source and destination");}
 auto reference=render(compiled.patch,1);float maximumDifference=0;for(auto block:{64U,127U,512U,4096U}){auto candidate=render(compiled.patch,block);for(std::size_t i=0;i<reference.size();++i)maximumDifference=std::max(maximumDifference,std::abs(reference[i]-candidate[i]));}ok&=expect(maximumDifference<=1e-6f,"static render is block invariant at gate sizes");ok&=expect(reference==render(compiled.patch,1),"fresh engine render is deterministic");
 ok&=expect(std::abs(engine::filterCutoffCeiling(44100)-17640.f)<1e-6f,"filter cutoff inspection uses the output-rate ceiling");
 // --- D8 filter panel shortcuts (#126) -------------------------------------------------------
 // `keytrack` and `envAmount` are compiled into the same row array the explicit matrix fills, so
 // there is no second modulation path to test against: these assertions read the compiled rows the
 // production compiler emitted and then run the production summation over them.
 {
  auto shortcutPatch=graphPatch();shortcutPatch.matrix.clear();
  auto explicitRows=engine::compilePatch(shortcutPatch);
  ok&=expect(static_cast<bool>(explicitRows)&&explicitRows.patch.rowCount==0,"a filter at the transparent defaults compiles no implicit row");
  setParameter(shortcutPatch,"b","keytrack",.5);setParameter(shortcutPatch,"b","envAmount",-.25);
  auto withShortcuts=engine::compilePatch(shortcutPatch);
  std::size_t filterSlot=0;for(std::size_t n=0;n<withShortcuts.patch.nodeCount;++n)if(withShortcuts.patch.nodes[n].type==domain::ModuleType::filter)filterSlot=n;
  bool keyRow=false,envRow=false;
  for(std::size_t i=0;i<withShortcuts.patch.rowCount;++i){const auto&row=withShortcuts.patch.rows[i];
   if(row.node!=filterSlot||row.target!=engine::ParameterTarget::cutoff)continue;
   keyRow|=row.source==domain::ModulationSource::keyTracking&&std::abs(row.depth-.5f)<1e-6f;
   envRow|=row.source==domain::ModulationSource::e2&&std::abs(row.depth+.25f)<1e-6f;}
  ok&=expect(static_cast<bool>(withShortcuts)&&withShortcuts.patch.rowCount==2&&keyRow&&envRow,
             "keytrack and envAmount compile to implicit keyTracking->cutoff and E2->cutoff rows at their declared depth");
  // One clamp, not two. An explicit row of +0.9 and a shortcut of -0.6 sum to +0.3 before the
  // single normalize/denormalize; clamping each in turn would instead saturate at 1 and then fall
  // back to 0.4, so these two results are only equal if the summation clamps exactly once.
  auto summed=graphPatch();summed.matrix={{"m1",true,domain::ModulationSource::keyTracking,"b","cutoff",.9}};
  setParameter(summed,"b","keytrack",-.6);setParameter(summed,"b","cutoff",1000);
  auto summedCompiled=engine::compilePatch(summed);
  std::size_t slot=0;for(std::size_t n=0;n<summedCompiled.patch.nodeCount;++n)if(summedCompiled.patch.nodes[n].type==domain::ModuleType::filter)slot=n;
  engine::ModulationInputs keyOnly{};keyOnly[static_cast<std::size_t>(domain::ModulationSource::keyTracking)]=1;
  const auto bothRows=engine::applyModulation(summedCompiled.patch,slot,summedCompiled.patch.nodes[slot].values,keyOnly);
  auto single=graphPatch();single.matrix={{"m1",true,domain::ModulationSource::keyTracking,"b","cutoff",.3}};
  setParameter(single,"b","cutoff",1000);
  auto singleCompiled=engine::compilePatch(single);
  const auto oneRow=engine::applyModulation(singleCompiled.patch,slot,singleCompiled.patch.nodes[slot].values,keyOnly);
  const auto*cutoffPd=domain::findParameter(domain::moduleCatalog()[static_cast<std::size_t>(domain::ModuleType::filter)],"cutoff");
  const auto afterFirstClamp=cutoffPd->denormalize(std::clamp(cutoffPd->normalize(1000.)+.9,0.,1.));
  const auto twiceClamped=static_cast<float>(cutoffPd->denormalize(std::clamp(cutoffPd->normalize(afterFirstClamp)-.6,0.,1.)));
  ok&=expect(summedCompiled.patch.rowCount==2&&std::abs(bothRows.cutoff-oneRow.cutoff)<1.f&&std::abs(bothRows.cutoff-twiceClamped)>100.f,
             "an explicit row and the shortcut sum into one clamp, not two");
  // The shortcuts never overflow the compiled row array: both filter slots can carry both of them
  // on top of a full 40-row matrix.
  auto full=iupac::testing::maximalPatch();
  for(auto&n:full.nodes)if(n.type==domain::ModuleType::filter)for(auto&value:n.parameters){if(value.id=="keytrack")value.values[0]=.4;if(value.id=="envAmount")value.values[0]=.7;}
  auto fullCompiled=engine::compilePatch(full);
  ok&=expect(static_cast<bool>(fullCompiled)&&fullCompiled.patch.rowCount==domain::maximumMatrixRows+engine::implicitFilterRows,
             "both filter slots add both shortcuts on top of a full matrix without overflowing the compiled rows");
 }
 // --- D8 audio-rate modulation inputs (#127) -------------------------------------------------
 // Authored patches on the production path: a harmonic drives `fm.modIn`, the FM drives
 // `resonator.exciteIn`, and both typed edges are ordinary audio edges in the same DAG.
 {
  domain::Patch audioRate;audioRate.noiseSeed=31;
  audioRate.nodes={defaults("g1","harmonic"),defaults("f1","fm"),defaults("r1","resonator")};
  // The two typed edges sit alongside ordinary ones, so the render is audible at depth 0 and the
  // identity below is a real comparison rather than two silences.
  audioRate.edges={{"g1","f1",.8,domain::AudioPort::modIn},{"g1","r1",.8},{"f1","r1",.8,domain::AudioPort::exciteIn},{"f1","output",.5},{"r1","output",1}};
  setParameter(audioRate,"f1","index",3);setParameter(audioRate,"r1","combFeedback",.6);
  auto zeroDepth=engine::compilePatch(audioRate);
  ok&=expect(static_cast<bool>(zeroDepth)&&zeroDepth.patch.nodeCount==3&&zeroDepth.patch.edgeCount==5,
             "a patch cabled on both typed inputs compiles with every node and edge live");
  bool typedEdges=false;for(std::size_t i=0;i<zeroDepth.patch.edgeCount;++i)typedEdges|=zeroDepth.patch.edges[i].port!=domain::AudioPort::in;
  ok&=expect(typedEdges&&zeroDepth.patch.rowCount==0,"the typed inputs are audio edges in the DAG, not matrix rows");
  // A modulator that only reaches the OUT bus through a typed port is still live: the compiler's
  // reachability walk follows `modIn`/`exciteIn` exactly like the ordinary input.
  const auto cabledAtZeroDepth=render(zeroDepth.patch,128);
  // Depth 0 is the catalog default, so the same patch with the typed edges removed must render the
  // identical samples: an uncabled port and a cabled one at depth 0 are the same signal.
  ok&=expect(energy(cabledAtZeroDepth)>1e-4f,"the authored audio-rate patch is audible at depth 0");
  auto uncabled=audioRate;std::erase_if(uncabled.edges,[](const domain::AudioEdge&e){return e.port!=domain::AudioPort::in;});
  auto uncabledCompiled=engine::compilePatch(uncabled);
  const auto uncabledRender=render(uncabledCompiled.patch,128);
  ok&=expect(cabledAtZeroDepth==uncabledRender,"depth 0 on both typed inputs renders exactly the pre-D8 patch");
  // Depth above 0 is audible, on each input independently.
  auto modDepth=audioRate;setParameter(modDepth,"f1","modInDepth",.7);
  auto modCompiled=engine::compilePatch(modDepth);const auto modRender=render(modCompiled.patch,128);
  ok&=expect(distance(modRender,cabledAtZeroDepth)>1.f,"modInDepth above 0 changes the production render");
  auto exciteDepth=audioRate;setParameter(exciteDepth,"r1","exciteDepth",.7);
  auto exciteCompiled=engine::compilePatch(exciteDepth);const auto exciteRender=render(exciteCompiled.patch,128);
  ok&=expect(distance(exciteRender,cabledAtZeroDepth)>1.f,"exciteDepth above 0 changes the production render");
  ok&=expect(std::ranges::all_of(modRender,[](float x){return std::isfinite(x)&&std::abs(x)<=.8912511f;})
             &&std::ranges::all_of(exciteRender,[](float x){return std::isfinite(x)&&std::abs(x)<=.8912511f;}),
             "audio-rate modulation stays inside the output safety guard");
  // Both depths at maximum, on the worst-case chain, still terminate and stay bounded.
  auto worst=audioRate;setParameter(worst,"f1","modInDepth",1);setParameter(worst,"r1","exciteDepth",1);
  setParameter(worst,"f1","outputLevel",1);setParameter(worst,"g1","outputLevel",1);setParameter(worst,"r1","combFeedback",.97);
  auto worstCompiled=engine::compilePatch(worst);const auto worstRender=render(worstCompiled.patch,128,{},engine::fallbackTempoBpm,127,8192);
  ok&=expect(std::ranges::all_of(worstRender,[](float x){return std::isfinite(x)&&std::abs(x)<=.8912511f;}),"both typed inputs at full depth stay bounded");
  // The typed edge is not the ordinary input: an `exciteIn` edge alone leaves the resonator's own
  // `in` port empty, so at depth 0 the resonator has nothing to ring and the patch is silent.
  domain::Patch typedOnly;typedOnly.noiseSeed=31;typedOnly.nodes={defaults("g1","harmonic"),defaults("r1","resonator")};
  typedOnly.edges={{"g1","r1",1,domain::AudioPort::exciteIn},{"r1","output",1}};
  auto typedOnlyCompiled=engine::compilePatch(typedOnly);
  ok&=expect(energy(render(typedOnlyCompiled.patch,128))<1e-9f,"an exciteIn edge does not feed the ordinary IN port");
  auto typedOnlyOpen=typedOnly;setParameter(typedOnlyOpen,"r1","exciteDepth",1);
  ok&=expect(energy(render(engine::compilePatch(typedOnlyOpen).patch,128))>1e-6f,"the same edge is audible once exciteDepth opens it");
  // The maximal patch already cables both typed inputs at its 48th edge, so the worst case the
  // budget is measured on carries them.
  auto maximalTyped=iupac::testing::maximalPatch();
  setParameter(maximalTyped,"f1","modInDepth",1);setParameter(maximalTyped,"r2","exciteDepth",1);
  auto maximalTypedCompiled=engine::compilePatch(maximalTyped);
  const auto maximalTypedRender=render(maximalTypedCompiled.patch,128);
  ok&=expect(static_cast<bool>(maximalTypedCompiled)&&maximalTypedCompiled.patch.edgeCount==domain::maximumEdges
             &&std::ranges::all_of(maximalTypedRender,[](float x){return std::isfinite(x)&&std::abs(x)<=.8912511f;}),
             "the 48-edge maximal patch renders bounded audio with both typed inputs at full depth");
 }
 domain::Patch onsetPatch;onsetPatch.nodes={defaults("noise","noise")};onsetPatch.edges={{"noise","output",1}};auto onsetCompiled=engine::compilePatch(onsetPatch);engine::Engine onsetEngine;onsetEngine.prepare(48000,512);onsetEngine.setPatch(onsetCompiled.patch);std::array<float,512>onsetL{},onsetR{};std::array onsetNote{engine::MidiEvent{0,engine::MidiEventType::noteOn,1,60,127,8192}};onsetEngine.render(onsetL,onsetR,onsetNote);auto onset=std::ranges::find_if(onsetL,[](float x){return std::abs(x)>1e-12f;});ok&=expect(onset!=onsetL.end()&&onsetEngine.latencySamples()==4,"integer-compensated oversampling reports the realized production configuration");
 engine::Engine voices;voices.prepare(48000,64);voices.setPatch(compiled.patch);std::vector<engine::MidiEvent> many;for(int i=0;i<20;++i)many.push_back({0,engine::MidiEventType::noteOn,static_cast<std::uint8_t>(i%2+1),static_cast<std::uint8_t>(40+i),100,8192});std::array<float,64>l{},r{};voices.render(l,r,many);ok&=expect(voices.activeVoiceCount()==16,"voice scheduling remains bounded");std::array controllerEvents{engine::MidiEvent{0,engine::MidiEventType::pitchBend,1,0,0,16383},engine::MidiEvent{0,engine::MidiEventType::controlChange,1,1,127,8192},engine::MidiEvent{0,engine::MidiEventType::controlChange,1,64,127,8192},engine::MidiEvent{1,engine::MidiEventType::noteOff,1,40,0,8192},engine::MidiEvent{2,engine::MidiEventType::controlChange,1,64,0,8192},engine::MidiEvent{3,engine::MidiEventType::controlChange,1,120,0,8192}};voices.render(l,r,controllerEvents);for(int i=0;i<8;++i)voices.render(l,r);ok&=expect(voices.activeVoiceCount()<16,"controllers and bounded channel all-sound-off fade are handled");ok&=expect(voices.latencySamples()>0,"integer oversampling latency reported");
 std::vector<engine::MidiEvent> overflow(engine::maximumMidiEventsPerBlock+1,{0,engine::MidiEventType::controlChange,1,1,64,8192});voices.render(l,r,overflow);for(int i=0;i<8;++i)voices.render(l,r);ok&=expect(voices.midiOverflowCount()==1&&voices.activeVoiceCount()==0,"MIDI overflow is counted and silenced with a bounded fade");auto overflowBefore=voices.midiOverflowCount();std::span<float> empty;std::array zeroEvent{engine::MidiEvent{0,engine::MidiEventType::noteOn,1,60,100,8192}};voices.render(empty,empty,zeroEvent);ok&=expect(voices.midiOverflowCount()==overflowBefore&&voices.activeVoiceCount()==0,"zero host blocks do not consume MIDI or alter voices");
 std::array repeated{engine::MidiEvent{0,engine::MidiEventType::noteOn,1,60,100,8192},engine::MidiEvent{1,engine::MidiEventType::noteOn,1,60,80,8192},engine::MidiEvent{2,engine::MidiEventType::noteOff,1,60,0,8192},engine::MidiEvent{3,engine::MidiEventType::controlChange,1,64,127,8192},engine::MidiEvent{4,engine::MidiEventType::noteOff,1,60,0,8192}};voices.render(l,r,repeated);ok&=expect(voices.activeVoiceCount()==2,"repeated notes release oldest while sustain retains the second voice");std::array sustainUp{engine::MidiEvent{0,engine::MidiEventType::controlChange,1,64,0,8192}};voices.render(l,r,sustainUp);ok&=expect(voices.activeVoiceCount()==2,"sustain release enters bounded envelope release");voices.reset();voices.setPatch(compiled.patch);std::array oneNote{engine::MidiEvent{0,engine::MidiEventType::noteOn,1,60,100,8192}};voices.render(l,r,oneNote);auto first=l;voices.reset();voices.render(l,r,oneNote);ok&=expect(first==l,"fresh voice reuse clears DSP and modulator history deterministically");std::array invalid{engine::MidiEvent{0,engine::MidiEventType::noteOn,0,60,100,8192},engine::MidiEvent{0,engine::MidiEventType::noteOn,1,128,100,8192}};voices.reset();voices.render(l,r,invalid);ok&=expect(voices.activeVoiceCount()==0,"invalid MIDI fields are ignored safely");
 auto maximal=engine::compilePatch(iupac::testing::maximalPatch());ok&=expect(static_cast<bool>(maximal)&&maximal.patch.nodeCount==domain::maximumNodes&&maximal.patch.edgeCount==domain::maximumEdges&&maximal.patch.rowCount==domain::maximumMatrixRows,"maximal patch compiles with every node, edge and row live");ok&=expect(std::ranges::count_if(std::span(maximal.patch.edges.data(),maximal.patch.edgeCount),[](const engine::CompiledEdge&e){return e.toOutput;})==3&&maximal.patch.nodes[maximal.patch.nodeCount-1].type==domain::ModuleType::width,"maximal patch keeps all three output edges and ends at the width slot");
 ok&=expect(maximal.patch.tailStart==12&&std::ranges::all_of(std::span(maximal.patch.nodes.data()+maximal.patch.tailStart,maximal.patch.nodeCount-maximal.patch.tailStart),[](const engine::CompiledNode&n){return domain::moduleCatalog()[static_cast<std::size_t>(n.type)].effects;})&&std::ranges::none_of(std::span(maximal.patch.nodes.data(),maximal.patch.tailStart),[](const engine::CompiledNode&n){return domain::moduleCatalog()[static_cast<std::size_t>(n.type)].effects;}),"the compiler places the four effects slots last, in the global tail");auto maximalSignal=render(maximal.patch,128);ok&=expect(energy(maximalSignal)>1e-4f&&std::ranges::all_of(maximalSignal,[](float x){return std::isfinite(x)&&std::abs(x)<=.8912511f;}),"maximal patch renders bounded audio");
 domain::Patch silent;auto cs=engine::compilePatch(silent);ok&=expect(static_cast<bool>(cs)&&cs.patch.nodeCount==0,"intentional silent patch compiles");
 engine::PatchCoordinator coordinator;coordinator.prepare(48000,128);auto firstGeneration=coordinator.publish(compiled.patch,{});std::array<float,128>txL{},txR{};coordinator.render(txL,txR);ok&=expect(coordinator.status().accepted==firstGeneration&&coordinator.activeBanks()==2,"structural publication begins a bounded two-bank transition");for(int i=0;i<8;++i)coordinator.render(txL,txR);ok&=expect(!coordinator.status().transitioning&&coordinator.status().audible==firstGeneration,"structural transition reaches its audible generation in bounded time");
 std::array heldNote{engine::MidiEvent{0,engine::MidiEventType::noteOn,1,60,100,8192}};coordinator.render(txL,txR,heldNote);auto scalar=compiled.patch;scalar.nodes[0].values.outputLevel=.25f;auto scalarGeneration=coordinator.publish(scalar,{});coordinator.render(txL,txR);ok&=expect(coordinator.activeBanks()==1&&!coordinator.status().transitioning&&coordinator.status().audible==scalarGeneration,"scalar edits preserve held voices without a bank transition");
 auto structuralPatch=engine::compilePatch(graphPatch(false)).patch;auto structuralGeneration=coordinator.publish(structuralPatch,{});coordinator.render(txL,txR);ok&=expect(coordinator.status().accepted==structuralGeneration&&coordinator.activeBanks()==2,"topology edit uses exactly two prepared banks");for(int i=0;i<8;++i)coordinator.render(txL,txR);ok&=expect(coordinator.status().audible==structuralGeneration,"held-note structural replacement completes audibly");
 std::uint64_t newest=0;for(int i=0;i<1000;++i){auto p=scalar;p.nodes[0].values.outputLevel=static_cast<float>(i%100)/100;newest=coordinator.publish(p,{});}coordinator.render(txL,txR);ok&=expect(coordinator.retryPending(),"producer retries its retained latest request after saturation");coordinator.render(txL,txR);ok&=expect(coordinator.status().accepted==newest,"saturated bounded queue retains and retries the newest desired generation");auto stale=coordinator.currentGeneration();coordinator.invalidateAsyncAuthors();ok&=expect(!coordinator.publishIfCurrent(stale,compiled.patch,{}),"manual invalidation prevents stale asynchronous publication");
 {// #68: block-rate effective-parameter publication through the production coordinator path.
  auto lfoPatch=graphPatch();lfoPatch.matrix={{"m1",true,domain::ModulationSource::l1,"b","cutoff",1}};lfoPatch.lfos[0].rate=8;auto lfoCompiled=engine::compilePatch(lfoPatch);std::size_t filterSlot=0;for(std::size_t n=0;n<lfoCompiled.patch.nodeCount;++n)if(lfoCompiled.patch.nodes[n].type==domain::ModuleType::filter)filterSlot=n;const auto cutoffIndex=static_cast<std::size_t>(engine::ParameterTarget::cutoff);const auto*cutoffDescriptor=domain::findParameter(domain::moduleCatalog()[static_cast<std::size_t>(domain::ModuleType::filter)],"cutoff");const float baseCutoff=static_cast<float>(cutoffDescriptor->normalize(lfoCompiled.patch.nodes[filterSlot].values.cutoff));
  engine::PatchCoordinator effective;effective.prepare(48000,128);(void)effective.publish(lfoCompiled.patch,{});std::array<float,128>eL{},eR{};for(int i=0;i<12;++i)effective.render(eL,eR);auto idle=effective.effectiveValues();ok&=expect(idle.nodeCount==lfoCompiled.patch.nodeCount&&idle.nodeIds[filterSlot]==lfoCompiled.patch.nodes[filterSlot].idHash&&std::abs(idle.values[filterSlot][cutoffIndex]-baseCutoff)<1e-6f,"effective value equals the base with no active voice");ok&=expect(idle.values[filterSlot][static_cast<std::size_t>(engine::ParameterTarget::index)]==-1.f&&idle.values[domain::maximumNodes-1][cutoffIndex]==-1.f&&idle.nodeIds[domain::maximumNodes-1]==0,"ineligible parameters and empty slots publish -1");
  effective.render(eL,eR,heldNote);float lo=1,hi=0;bool inRange=true;for(int i=0;i<40;++i){effective.render(eL,eR);const float v=effective.effectiveValues().values[filterSlot][cutoffIndex];inRange&=v>=0.f&&v<=1.f;lo=std::min(lo,v);hi=std::max(hi,v);}ok&=expect(inRange&&lo<baseCutoff&&hi>baseCutoff&&hi-lo>.2f,"LFO->cutoff at depth 1 publishes an oscillating clamped normalized value around the base");
  auto velocityPatch=graphPatch();velocityPatch.matrix={{"m1",true,domain::ModulationSource::velocity,"b","cutoff",.5},{"m2",true,domain::ModulationSource::keyTracking,"b","cutoff",-.25}};auto velocityCompiled=engine::compilePatch(velocityPatch);engine::PatchCoordinator held;held.prepare(48000,128);(void)held.publish(velocityCompiled.patch,{});for(int i=0;i<12;++i)held.render(eL,eR);std::array velocityNote{engine::MidiEvent{0,engine::MidiEventType::noteOn,1,72,100,8192}};held.render(eL,eR,velocityNote);held.render(eL,eR);engine::ModulationInputs inputs{};inputs[static_cast<std::size_t>(domain::ModulationSource::velocity)]=100/127.f;inputs[static_cast<std::size_t>(domain::ModulationSource::keyTracking)]=(72-60)/36.f;const auto expected=engine::applyModulation(velocityCompiled.patch,filterSlot,velocityCompiled.patch.nodes[filterSlot].values,inputs);const float expectedNormalized=static_cast<float>(cutoffDescriptor->normalize(expected.cutoff));const float published=held.effectiveValues().values[filterSlot][cutoffIndex];ok&=expect(std::abs(published-expectedNormalized)<1e-5f&&std::abs(published-baseCutoff)>.05f,"published cutoff matches the production matrix summation for a held voice");
  auto disabledPatch=velocityPatch;for(auto&row:disabledPatch.matrix)row.enabled=false;auto disabledCompiled=engine::compilePatch(disabledPatch);(void)held.publish(disabledCompiled.patch,{});for(int i=0;i<12;++i)held.render(eL,eR);ok&=expect(held.activeVoiceCount()==1&&std::abs(held.effectiveValues().values[filterSlot][cutoffIndex]-baseCutoff)<1e-6f,"disabled rows publish the base while the voice stays held");
  std::array releaseNote{engine::MidiEvent{0,engine::MidiEventType::controlChange,1,120,0,8192}};held.render(eL,eR,releaseNote);for(int i=0;i<8;++i)held.render(eL,eR);ok&=expect(held.activeVoiceCount()==0&&std::abs(held.effectiveValues().values[filterSlot][cutoffIndex]-baseCutoff)<1e-6f,"effective value returns to the base after the last voice retires");}
 {// #123: the post-mixer global effects tail with the chorus and the tempo-synced stereo delay.
  const auto dry=render(engine::compilePatch(tailPatch({})).patch,128);
  // A wet-dry `mix` of 0 makes either effect a literal straight wire, which is the same signal a
  // patch with no effects node at all renders: the tail is inside the graph, not an always-on stage.
  auto bypassed=tailPatch({"chorus","delay"});setParameter(bypassed,"x0","mix",0);setParameter(bypassed,"x1","mix",0);
  ok&=expect(distance(dry,render(engine::compilePatch(bypassed).patch,128))==0.f,"an effects tail at mix 0 renders the identical samples as a patch with no effects node");
  auto wet=tailPatch({"chorus","delay"});setParameter(wet,"x0","mix",.6);setParameter(wet,"x1","mix",.6);setParameter(wet,"x1","timeMs",5);
  const auto wetSignal=render(engine::compilePatch(wet).patch,128);
  ok&=expect(distance(dry,wetSignal)>.5f&&std::ranges::all_of(wetSignal,[](float x){return std::isfinite(x)&&std::abs(x)<=.8912511f;}),"the chorus and delay are audible and stay inside the output guard");
  // Ordering is the compiled tail partition, so swapping the two effects is a different render.
  auto swapped=tailPatch({"delay","chorus"});setParameter(swapped,"x0","mix",.6);setParameter(swapped,"x0","timeMs",5);setParameter(swapped,"x1","mix",.6);
  ok&=expect(distance(wetSignal,render(engine::compilePatch(swapped).patch,128))>.5f,"the tail runs the effects in the compiled order, so chorus-then-delay differs from delay-then-chorus");
  // The tail input carries each voice's contribution after E1 and velocity, so a quieter note makes
  // a quieter tail.
  ok&=expect(distance(render(engine::compilePatch(wet).patch,128,{},engine::fallbackTempoBpm,40),wetSignal)>.1f,"velocity gates a voice's contribution to the tail");
  // Tempo sync. `1/8` is half a quarter-note beat, which at the 120 BPM fallback is exactly 250 ms,
  // and the same division at 60 BPM is exactly 500 ms — both identical to the free time that says so.
  auto synced=tailPatch({"delay"});setParameter(synced,"x0","mix",.6);setParameter(synced,"x0","syncMode",1);setParameter(synced,"x0","syncDivision",4);
  auto free250=tailPatch({"delay"});setParameter(free250,"x0","mix",.6);setParameter(free250,"x0","timeMs",250);
  auto free500=tailPatch({"delay"});setParameter(free500,"x0","mix",.6);setParameter(free500,"x0","timeMs",500);
  const auto syncedCompiled=engine::compilePatch(synced).patch;
  // One second, so a 250 ms and a 500 ms repeat are both inside the window.
  const auto second=[&](const engine::CompiledPatch&c,double tempo){return render(c,128,{},tempo,100,48000);};
  ok&=expect(distance(second(syncedCompiled,engine::fallbackTempoBpm),second(engine::compilePatch(free250).patch,engine::fallbackTempoBpm))==0.f,"1/8 at the 120 BPM fallback is the 250 ms free delay exactly");
  ok&=expect(distance(second(syncedCompiled,60.0),second(engine::compilePatch(free500).patch,engine::fallbackTempoBpm))==0.f,"1/8 at 60 BPM is the 500 ms free delay exactly");
  ok&=expect(distance(second(syncedCompiled,engine::fallbackTempoBpm),second(syncedCompiled,60.0))>.1f,"the host tempo changes what a synced delay renders");
  ok&=expect(distance(second(syncedCompiled,engine::fallbackTempoBpm),second(syncedCompiled,0.0))==0.f,"an absent or out-of-range tempo falls back to 120 BPM");
  // A macro is the one modulation source a stage that runs once for the whole mix can read.
  auto macroTail=tailPatch({"delay"});setParameter(macroTail,"x0","mix",.6);setParameter(macroTail,"x0","timeMs",5);macroTail.matrix={{"t1",true,domain::ModulationSource::macro1,"x0","mix",-.6}};
  const auto macroCompiled=engine::compilePatch(macroTail).patch;domain::HostControls macroUp;macroUp.macros[0]=1;
  ok&=expect(distance(render(macroCompiled,128),render(macroCompiled,128,macroUp))>.1f,"a macro row reaches an effects parameter in the global tail");
 }
 {// #124: the algorithmic reverb and the width module complete the four-slot effects region.
  const auto dry=render(engine::compilePatch(tailPatch({})).patch,128);
  // Both are the literal straight wire at the settings that say so: the reverb at `mix` 0 and the
  // width module at its default `width` 1, which is what "transparent at the defaults" means.
  auto bypassed=tailPatch({"reverb","width"});setParameter(bypassed,"x0","mix",0);
  ok&=expect(distance(dry,render(engine::compilePatch(bypassed).patch,128))==0.f,"a reverb at mix 0 and a width module at width 1 render the identical samples as a patch with no effects node");
  auto wetReverb=tailPatch({"reverb"});setParameter(wetReverb,"x0","mix",.6);
  const auto reverbSignal=render(engine::compilePatch(wetReverb).patch,128);
  ok&=expect(distance(dry,reverbSignal)>.5f&&std::ranges::all_of(reverbSignal,[](float x){return std::isfinite(x)&&std::abs(x)<=.8912511f;}),"the reverb is audible and stays inside the output guard");
  // Every reverb control has to move the render, or it is a knob wired to nothing. These are read
  // over a second of audio: decay and damping only separate once there is a tail to hear.
  const auto reverbSecond=render(engine::compilePatch(wetReverb).patch,128,{},engine::fallbackTempoBpm,100,48000);
  for(const auto&[id,value]:std::vector<std::pair<std::string_view,double>>{{"size",1.0},{"decaySeconds",12.0},{"damping",1.0},{"preDelayMs",150.0},{"width",0.0}})
  {auto moved=wetReverb;setParameter(moved,"x0",id,value);
   const auto shifted=render(engine::compilePatch(moved).patch,128,{},engine::fallbackTempoBpm,100,48000);
   if(!expect(distance(reverbSecond,shifted)>1e-3f,"a reverb control moves the render"))std::cerr<<"  inert reverb control: "<<id<<'\n';}
  // `width` is a whole-signal transform: it needs stereo content to act on, so the source is panned
  // across the image and the two channels are compared rather than the left one alone.
  auto stereo=tailPatch({"width"});for(std::size_t i=0;i<16;++i)if(auto*n=nodeNamed(stereo,"a"))for(auto&v:n->parameters)if(v.id=="partialPans")v.values[i]=i%2==0?-1.0:1.0;
  const auto narrow=[&](double amount,double bassMono){auto q=stereo;setParameter(q,"x0","width",amount);setParameter(q,"x0","bassMonoHz",bassMono);return engine::compilePatch(q).patch;};
  const auto transparent=render(narrow(1.0,120.0),128);
  ok&=expect(distance(render(narrow(1.0,500.0),128),transparent)==0.f,"at width 1 the module is transparent whatever bassMonoHz says");
  ok&=expect(distance(render(narrow(0.0,120.0),128),transparent)>.1f&&distance(render(narrow(2.0,120.0),128),transparent)>.1f,"width 0 and width 2 both change the render");
  ok&=expect(distance(render(narrow(2.0,20.0),128),render(narrow(2.0,500.0),128))>1e-3f,"bassMonoHz decides how much of the side signal the widening reaches");
  // The whole four-unit region in one patch, in the contract's order, is a single compiled tail.
  auto full=tailPatch({"chorus","delay","reverb","width"});
  setParameter(full,"x0","mix",.4);setParameter(full,"x1","mix",.4);setParameter(full,"x1","timeMs",7);setParameter(full,"x2","mix",.4);setParameter(full,"x3","width",1.6);
  const auto fullCompiled=engine::compilePatch(full);
  ok&=expect(static_cast<bool>(fullCompiled)&&fullCompiled.patch.nodeCount==6&&fullCompiled.patch.tailStart==2,"the full chorus -> delay -> reverb -> width tail compiles as one four-node region");
  const auto fullSignal=render(fullCompiled.patch,128);
  ok&=expect(energy(fullSignal)>1e-6f&&std::ranges::all_of(fullSignal,[](float x){return std::isfinite(x)&&std::abs(x)<=.8912511f;}),"the full four-unit tail renders bounded audio");
  // Bounded tail. The longest decay at a fully wet mix, rendered well past the note: the network is
  // a contraction (orthonormal mixing, every line gain strictly below 1), so the tail has to be
  // decaying by the end, and no denormal-stalled or runaway sample may appear anywhere in it.
  auto runaway=tailPatch({"reverb"});setParameter(runaway,"x0","mix",1);setParameter(runaway,"x0","decaySeconds",20);
  setParameter(runaway,"x0","size",1);setParameter(runaway,"x0","damping",0);setParameter(runaway,"x0","preDelayMs",200);
  const auto tail=render(engine::compilePatch(runaway).patch,128,{},engine::fallbackTempoBpm,100,96000);
  ok&=expect(std::ranges::all_of(tail,[](float x){return std::isfinite(x)&&std::abs(x)<=.8912511f;}),"decaySeconds 20 at mix 1 stays finite and inside the output guard");
  const auto window=[&](std::size_t from,std::size_t to){return energy(std::vector<float>(tail.begin()+static_cast<long>(from),tail.begin()+static_cast<long>(to)));};
  ok&=expect(window(40000,56000)>0.f&&window(80000,96000)<window(40000,56000),"the longest reverb tail decays instead of running away");
 }
 {// #125: a discrete LFO waveform change is a crossfaded transition, not a step.
  // The published effective cutoff is the production matrix result, so an uncrossfaded switch from
  // `sine` (phase 0, near zero) to `square` (+1 immediately) would show up here as one block-sized jump.
  auto shapePatch=graphPatch();shapePatch.matrix={{"m1",true,domain::ModulationSource::l1,"b","cutoff",1}};shapePatch.lfos[0].rate=1;shapePatch.lfos[0].waveform=domain::LfoWaveform::sine;
  auto sineCompiled=engine::compilePatch(shapePatch);std::size_t filterSlot=0;for(std::size_t n=0;n<sineCompiled.patch.nodeCount;++n)if(sineCompiled.patch.nodes[n].type==domain::ModuleType::filter)filterSlot=n;
  const auto cutoffIndex=static_cast<std::size_t>(engine::ParameterTarget::cutoff);
  engine::PatchCoordinator shapes;shapes.prepare(48000,128);(void)shapes.publish(sineCompiled.patch,{});std::array<float,128>sL{},sR{};for(int i=0;i<12;++i)shapes.render(sL,sR);
  std::array shapeNote{engine::MidiEvent{0,engine::MidiEventType::noteOn,1,60,100,8192}};shapes.render(sL,sR,shapeNote);shapes.render(sL,sR);
  const float beforeSwitch=shapes.effectiveValues().values[filterSlot][cutoffIndex];
  auto squarePatch=shapePatch;squarePatch.lfos[0].waveform=domain::LfoWaveform::square;(void)shapes.publish(engine::compilePatch(squarePatch).patch,{});
  float previous=beforeSwitch,largestStep=0,last=beforeSwitch;for(int i=0;i<12;++i){shapes.render(sL,sR);const float v=shapes.effectiveValues().values[filterSlot][cutoffIndex];largestStep=std::max(largestStep,std::abs(v-previous));previous=v;last=v;}
  ok&=expect(shapes.activeVoiceCount()==1&&last>beforeSwitch+.2f,"the waveform change reaches the held voice");
  ok&=expect(largestStep<std::abs(last-beforeSwitch)*.75f,"a discrete waveform change crossfades instead of stepping");
 }
 {// #126: a filter `mode` or shaper `curve` change is a crossfaded transition under the same rules.
  // Neither is a matrix destination, so the evidence is the rendered audio itself: swapping a 12 dB
  // lowpass for the 24 dB ladder under a held note changes the signal, and an uncrossfaded swap
  // would show up as one sample-to-sample step far larger than the waveform's own slope.
  domain::Patch switchPatch;switchPatch.noiseSeed=31;
  switchPatch.nodes={defaults("a","harmonic"),defaults("s","shaper"),defaults("b","filter"),defaults("c","mixer")};
  switchPatch.edges={{"a","s",1},{"s","b",1},{"b","c",1},{"c","output",1}};
  setParameter(switchPatch,"b","cutoff",700);setParameter(switchPatch,"b","q",4);setParameter(switchPatch,"s","drive",6);setParameter(switchPatch,"s","wet",.8);
  engine::PatchCoordinator modes;modes.prepare(48000,128);(void)modes.publish(engine::compilePatch(switchPatch).patch,{});
  std::array<float,128>mL{},mR{};for(int i=0;i<12;++i)modes.render(mL,mR);
  std::array modeNote{engine::MidiEvent{0,engine::MidiEventType::noteOn,1,60,100,8192}};modes.render(mL,mR,modeNote);
  auto largestSlope=[&](int blocks){float step=0,previous=0;for(int b=0;b<blocks;++b){modes.render(mL,mR);for(float x:mL){step=std::max(step,std::abs(x-previous));previous=x;}}return step;};
  const float steadySlope=largestSlope(16);
  float before=0;for(float x:mL)before=std::max(before,std::abs(x));
  // Both discrete controls move at once: the filter to `ladder24` and the shaper from `tanh` to
  // `fold`, which is the largest transfer-function jump the catalog offers.
  auto ladderPatch=switchPatch;setParameter(ladderPatch,"b","mode",3);setParameter(ladderPatch,"b","drive",8);setParameter(ladderPatch,"s","curve",2);
  (void)modes.publish(engine::compilePatch(ladderPatch).patch,{});
  const float switchSlope=largestSlope(16);
  float after=0;for(float x:mL)after=std::max(after,std::abs(x));
  ok&=expect(modes.activeVoiceCount()==1&&std::abs(after-before)>1e-4f,"the mode and curve change reaches the held voice");
  ok&=expect(switchSlope<steadySlope*3.f,"a discrete filter mode and shaper curve change crossfades instead of clicking");
 }

 return ok;}
