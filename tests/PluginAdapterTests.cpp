#include "PluginProcessor.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <thread>

namespace { bool expect(bool c,const char*m){if(!c)std::cerr<<"plugin adapter test failed: "<<m<<'\n';return c;} }
int main(int argc,char**argv)
{
#if IUPAC_ENABLE_CHEMISTRY
 if(argc==2)::setenv("IUPAC_CHEMISTRY_HELPER",argv[1],1);
#endif
 bool ok=true;IupacSynthProcessor processor;
 ok&=expect(processor.getLatencySamples()==4,"fixed oversampling latency is reported before prepare");
 constexpr std::array ids{"macro1","macro2","macro3","macro4","outputGain","width","masterTune","bypass"};
 ok&=expect(processor.getParameters().size()==8,"exactly eight fixed host parameters");for(std::size_t i=0;i<ids.size();++i){auto*p=dynamic_cast<juce::AudioProcessorParameterWithID*>(processor.getParameters()[static_cast<int>(i)]);ok&=expect(p&&p->paramID==ids[i],"stable host parameter ordering and IDs");}
 processor.prepareToPlay(48000,128);juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8(100)),0);for(int i=0;i<10;++i)processor.processBlock(audio,midi);ok&=expect(std::any_of(audio.getReadPointer(0),audio.getReadPointer(0)+128,[](float x){return std::abs(x)>1e-6f;}),"MIDI reaches production engine");
 auto state=processor.snapshot();state.controls={{{.125,.25,.5,.75}},-12,.75,3,true};juce::var provenance(new juce::DynamicObject());provenance.getDynamicObject()->setProperty("source","test");state.provenance=provenance;ok&=expect(processor.loadState(state).empty(),"valid exact state accepted");state=processor.snapshot();
 juce::MemoryBlock saved;processor.getStateInformation(saved);IupacSynthProcessor restored;restored.setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));ok&=expect(iupac::domain::encodeStateJson(restored.snapshot())==iupac::domain::encodeStateJson(state),"project state restores patch controls and provenance exactly");
 const auto before=iupac::domain::encodeStateJson(restored.snapshot());const char malformed[]="{bad";restored.setStateInformation(malformed,4);ok&=expect(iupac::domain::encodeStateJson(restored.snapshot())==before,"malformed state preserves last valid document");std::string huge(iupac::domain::maximumDocumentBytes+1,' ');restored.setStateInformation(huge.data(),static_cast<int>(huge.size()));ok&=expect(iupac::domain::encodeStateJson(restored.snapshot())==before,"oversized state preserves last valid document");
 auto future=iupac::domain::encodeStateJson(state);const auto version=future.find("\"stateVersion\": 1");ok&=expect(version!=std::string::npos,"encoded state version located");future.replace(version,17,"\"stateVersion\": 2");restored.setStateInformation(future.data(),static_cast<int>(future.size()));ok&=expect(iupac::domain::encodeStateJson(restored.snapshot())==before,"future state preserves last valid document");
 ok&=expect(restored.resetPatchEdits().empty(),"reset edits accepts stored base");auto reset=restored.snapshot();ok&=expect(iupac::domain::encodePatchJson(reset.basePatch)==iupac::domain::encodePatchJson(reset.editedPatch),"reset edits restores exact base patch");restored.resetControls();reset=restored.snapshot();const auto near=[](double a,double b){return std::abs(a-b)<1e-5;};ok&=expect(near(reset.controls.macros[0],reset.editedPatch.macros[0].defaultValue)&&near(reset.controls.outputGain,-6)&&near(reset.controls.width,.5)&&near(reset.controls.masterTune,0)&&!reset.controls.bypass,"reset controls restores patch macro and global defaults");
 ok&=expect(restored.newDocument().empty(),"New creates valid authored state");auto fresh=restored.snapshot();ok&=expect(!fresh.provenance&&iupac::domain::encodePatchJson(fresh.basePatch)==iupac::domain::encodePatchJson(fresh.editedPatch),"New clears provenance and starts with matching base/edit");
#if IUPAC_ENABLE_CHEMISTRY
 ok&=expect(restored.loadState(state).empty(),"chemistry setup state loads");const auto controlsBefore=restored.snapshot().controls;restored.applyChemistry(iupac::chemistry::InputMode::smiles,"CCO");for(int i=0;i<200&&restored.chemistryStatus().busy;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));auto generated=restored.snapshot();
 ok&=expect(generated.provenance&&iupac::domain::encodePatchJson(generated.basePatch)==iupac::domain::encodePatchJson(generated.editedPatch),"successful Apply commits generated base and editable patch with provenance");ok&=expect(generated.controls.macros==controlsBefore.macros&&near(generated.controls.outputGain,controlsBefore.outputGain),"Apply retains host controls");
 const auto generatedJson=iupac::domain::encodeStateJson(generated);restored.applyChemistry(iupac::chemistry::InputMode::smiles,"reject");for(int i=0;i<200&&restored.chemistryStatus().busy;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));ok&=expect(iupac::domain::encodeStateJson(restored.snapshot())==generatedJson,"failed Apply preserves current sound and state");
 restored.applyChemistry(iupac::chemistry::InputMode::smiles,"slow");ok&=expect(restored.newDocument().empty(),"manual New invalidates pending chemistry");std::this_thread::sleep_for(std::chrono::milliseconds(100));ok&=expect(!restored.snapshot().provenance,"stale chemistry cannot overwrite newer document");
#endif
 return ok?0:1;
}
