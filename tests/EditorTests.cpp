#include "PluginEditor.hpp"
#include "PluginProcessor.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>

namespace { bool expect(bool c,const char*m){if(!c)std::cerr<<"editor test failed: "<<m<<'\n';return c;} }
int main()
{
 juce::ScopedJuceInitialiser_GUI gui;bool ok=true;IupacSynthProcessor processor;processor.prepareToPlay(48000,128);
 ok&=expect(processor.editPatch([](auto&p){p.nodes.clear();p.edges.clear();p.matrix.clear();}).empty(),"empty valid patch accepted");
 IupacSynthEditor editor(processor);editor.setVisible(true);editor.setSize(1000,700);editor.setSize(1800,1200);editor.setSize(1200,800);
 for(auto type:{"harmonic","fm","noise","resonator","filter","shaper","mixer"})ok&=expect(editor.addModule(type),"every catalog module can be authored");
 auto state=processor.snapshot();ok&=expect(state.editedPatch.nodes.size()==7,"seven module types are present");
 const auto oldRatio=state.editedPatch.nodes[0].parameters[1].values[15];ok&=expect(editor.setSelectedParameter("inharmonicity",0,.01),"harmonic convenience control is editable");state=processor.snapshot();ok&=expect(std::abs(state.editedPatch.nodes[0].parameters[1].values[15]-oldRatio)>1.0e-6,"convenience edit stores regenerated explicit array");
 ok&=expect(editor.addEdge(state.editedPatch.nodes[0].id,state.editedPatch.nodes[4].id,.8),"source to processor edge accepted");
 ok&=expect(editor.addEdge(state.editedPatch.nodes[4].id,"output",.7),"processor to output edge accepted");
 ok&=expect(editor.addMatrixRow(),"matrix route can be authored");
 const auto valid=iupac::domain::encodeStateJson(processor.snapshot());ok&=expect(!editor.addEdge(state.editedPatch.nodes[4].id,state.editedPatch.nodes[0].id,1),"edge entering a source is rejected");ok&=expect(iupac::domain::encodeStateJson(processor.snapshot())==valid,"invalid edit preserves complete valid state");
 const auto path=std::filesystem::temp_directory_path()/"iupac-editor-roundtrip.iupacpatch";ok&=expect(processor.saveStateFile(path).empty(),"editor state saves through shared codec");ok&=expect(processor.newDocument().empty()&&processor.loadStateFile(path).empty(),"file state loads through shared codec");ok&=expect(iupac::domain::encodeStateJson(processor.snapshot())==valid,"file round trip is exact");std::error_code ec;std::filesystem::remove(path,ec);
 juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;processor.keyboardState().noteOn(1,60,.8f);processor.processBlock(audio,midi);ok&=expect(processor.activeVoiceCount()>0,"audition keyboard reaches production MIDI path");ok&=expect(editor.setSelectedParameter("outputLevel",0,.5),"scalar editor action is accepted");processor.processBlock(audio,midi);ok&=expect(processor.activeVoiceCount()>0,"scalar editor action preserves held audition note");processor.keyboardState().allNotesOff(1);
 editor.setVisible(false);return ok?0:1;
}
