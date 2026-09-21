// Native editor smoke and gesture tests (VERIFICATION V0 editor-gesture requirement, V9 native editor smoke).
// Every gesture below is a synthesized juce::MouseEvent delivered to the production component, and every assertion
// reads the processor's document through the shared codec. `--screenshot <dir>` writes the documentation screenshots.
#include "PluginEditor.hpp"
#include "PluginProcessor.hpp"
#if IUPAC_ENABLE_CHEMISTRY
#include "ChemistryPopup.hpp"
#endif
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <cstdlib>
namespace
{
bool expect(bool c,const char*m){if(!c)std::cerr<<"editor test failed: "<<m<<'\n';return c;}
using namespace iupac;
juce::MouseEvent event(juce::Component&c,juce::Point<float>position,juce::ModifierKeys mods=juce::ModifierKeys::leftButtonModifier,juce::Point<float>downPosition={},int clicks=1,bool dragged=false)
{
 return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),position,mods,0.0f,0.0f,0.0f,0.0f,0.0f,&c,&c,juce::Time::getCurrentTime(),dragged?downPosition:position,juce::Time::getCurrentTime(),clicks,dragged);
}
juce::Point<float>centreOf(juce::Component&c){return c.getLocalBounds().toFloat().getCentre();}
// Press on `from`, drag to `to` (any component), release: JUCE delivers drag/up events to the pressed component.
void drag(juce::Component&from,juce::Component&to,juce::Point<float>toPosition)
{
 const auto start=centreOf(from);const auto target=from.getLocalPoint(&to,toPosition);from.mouseDown(event(from,start));from.mouseDrag(event(from,start+(target-start)*0.5f,juce::ModifierKeys::leftButtonModifier,start,1,true));from.mouseDrag(event(from,target,juce::ModifierKeys::leftButtonModifier,start,1,true));from.mouseUp(event(from,target,juce::ModifierKeys::leftButtonModifier,start,1,true));
}
const domain::AudioEdge*edge(const domain::Patch&p,std::string_view s,std::string_view d,domain::AudioPort port=domain::AudioPort::in){for(const auto&e:p.edges)if(e.source==s&&e.destination==d&&e.port==port)return&e;return nullptr;}
const domain::Node*node(const domain::Patch&p,std::string_view id){for(const auto&n:p.nodes)if(n.id==id)return&n;return nullptr;}
const std::vector<double>&values(const domain::Patch&p,std::string_view id,std::string_view parameter){static const std::vector<double>none;const auto*n=node(p,id);if(!n)return none;for(const auto&x:n->parameters)if(x.id==parameter)return x.values;return none;}
// Runs due message-thread timers (editor ≤30 Hz refresh, processor publish retry) without a dispatch loop; JUCE's timer
// thread re-arms at most every 300 ms while its message stays unhandled, so wait longer than that before calling.
void tick(){juce::Thread::sleep(350);juce::Timer::callPendingTimersSynchronously();}
bool anyNoteOn(juce::MidiKeyboardState&k){for(int n=0;n<128;++n)if(k.isNoteOnForChannels(0xffff,n))return true;return false;}
// Slot indices come from the generated table, never from a literal: `Layout.hpp` is regenerated
// whenever the eligible-edge graph widens (#127 moved SUB to the head of the source column), and a
// literal would silently retarget a case at the next regeneration instead of failing.
constexpr std::size_t slotOf(ui::SlotKind kind,int instance=0){return ui::findSlot(kind,instance).value();}
constexpr std::size_t src1=slotOf(ui::SlotKind::source,0),src2=slotOf(ui::SlotKind::source,1),src3=slotOf(ui::SlotKind::source,2);
constexpr std::size_t subSlot=slotOf(ui::SlotKind::sub),res1=slotOf(ui::SlotKind::resonator,0),res2=slotOf(ui::SlotKind::resonator,1);
constexpr std::size_t filt1=slotOf(ui::SlotKind::filter,0),shape1=slotOf(ui::SlotKind::shaper,0),mix1=slotOf(ui::SlotKind::mixer,0);
constexpr std::size_t chorusSlot=slotOf(ui::SlotKind::chorus),delaySlot=slotOf(ui::SlotKind::delay),reverbSlot=slotOf(ui::SlotKind::reverb),widthSlot=slotOf(ui::SlotKind::width);
void buildDemo(IupacSynthEditor&e)
{
 e.activateSlot("harmonic",src1);e.activateSlot("fm",src2);e.activateSlot("noise",src3);e.activateSlot("resonator",res1);e.activateSlot("filter",filt1);e.activateSlot("shaper",shape1);e.activateSlot("mixer",mix1);
 e.connect("src1","filt1",.8);e.connect("src2","res1",.7);e.connect("res1","mix1",.9);e.connect("filt1","shape1",.6);e.connect("shape1","mix1",.5);e.connect("src3","mix1",.3);e.connect("mix1","output",.8);e.connect("src1","res1",.4);
 e.addLane();e.setLane(0,{"route1",true,domain::ModulationSource::l1,"filt1","cutoff",.6});e.addLane();e.setLane(1,{"route2",true,domain::ModulationSource::e2,"src2","index",-.4});e.addLane();e.setLane(2,{"route3",true,domain::ModulationSource::macro1,"mix1","pan",.8});
 e.setParameter("res1","mode",0,1);e.setParameter("src1","tilt",0,-.6);
}
int screenshots(const std::filesystem::path&dir)
{
 std::error_code ec;std::filesystem::create_directories(dir,ec);IupacSynthProcessor processor;processor.prepareToPlay(48000,128);(void)processor.editPatch([](auto&p){p.nodes.clear();p.edges.clear();p.matrix.clear();});
 IupacSynthEditor editor(processor);editor.setVisible(true);editor.setSize(1200,800);buildDemo(editor);editor.lanes().select(0);
 auto write=[&](juce::Component&c,const char*name){auto image=c.createComponentSnapshot(c.getLocalBounds(),false,2.0f);juce::File file((dir/name).string());file.deleteFile();juce::FileOutputStream out(file);juce::PNGImageFormat png;if(!out.openedOk()||!png.writeImageToStream(image,out)){std::cerr<<"could not write "<<name<<'\n';return false;}std::cout<<"wrote "<<file.getFullPathName()<<'\n';return true;};
 bool ok=write(editor,"editor-default.png");ok&=write(editor.field(),"editor-field.png");editor.setSize(1000,700);ok&=write(editor,"editor-minimum.png");
 editor.setSize(2000,1400);ok&=write(editor,"editor-doubled.png");editor.setSize(1200,800);// #89: text scales with the frame
#if IUPAC_ENABLE_CHEMISTRY
 ChemistryPopup popup(processor);popup.setLookAndFeel(&editor.getLookAndFeel());popup.setVisible(true);ok&=write(popup,"editor-chemistry-popup.png");popup.setLookAndFeel(nullptr);
#endif
 editor.setVisible(false);return ok?0:1;
}
}
#if IUPAC_ENABLE_CHEMISTRY
// Find a production component by the accessibility name the popup already sets on it, so
// these tests drive the same widgets the user does rather than test-only handles.
template<typename Component> Component* named(juce::Component& parent, juce::StringRef name)
{
 for(auto*child:parent.getChildren()){if(child->getName()==name)if(auto*typed=dynamic_cast<Component*>(child))return typed;
  if(auto*found=named<Component>(*child,name))return found;}
 return nullptr;
}
juce::TextButton* labelled(juce::Component& parent, juce::StringRef text)
{
 for(auto*child:parent.getChildren()){if(auto*button=dynamic_cast<juce::TextButton*>(child))if(button->getButtonText()==text)return button;
  if(auto*found=labelled(*child,text))return found;}
 return nullptr;
}
// Press a production button the way a user does: enter, press, release. Button narrows
// these to protected overrides, so they are called through the Component interface.
void click(juce::Button& button)
{
 juce::Component& component=button;const auto at=centreOf(component);
 component.mouseEnter(event(component,at));component.mouseDown(event(component,at));component.mouseUp(event(component,at));
}
#endif
int main(int argc,char**argv)
{
 juce::ScopedJuceInitialiser_GUI gui;if(argc==3&&std::string_view(argv[1])=="--screenshot")return screenshots(argv[2]);
#if IUPAC_ENABLE_CHEMISTRY
 // argv[1] is the fake helper: point the production locator at it before the processor is
 // built, so the popup exercises the real spawn/poll path against a deliberately slow helper.
 if(argc==2)::setenv("IUPAC_CHEMISTRY_HELPER",argv[1],1);
#endif
 bool ok=true;IupacSynthProcessor processor;processor.prepareToPlay(48000,128);
 ok&=expect(processor.editPatch([](auto&p){p.nodes.clear();p.edges.clear();p.matrix.clear();}).empty(),"empty valid patch accepted");
 auto editor=std::make_unique<IupacSynthEditor>(processor);editor->setVisible(true);
 for(auto size:{std::pair{1000,700},std::pair{1800,1200},std::pair{1200,800}}){editor->setSize(size.first,size.second);ok&=expect(editor->getWidth()==size.first&&editor->field().getWidth()==size.first,"resize sweep keeps the field at window width");}
 // #89: captions and control text are reference-frame heights scaled by the live editor, not frozen 8-11 px clamps.
 {
  auto&look=dynamic_cast<ui::EditorLookAndFeel&>(editor->getLookAndFeel());
  editor->setSize(1000,700);
  const float captionAtOne=ui::scaledText(editor->field(),9.0f),menuAtOne=look.getPopupMenuFont().getHeight();
  ok&=expect(std::abs(look.scale()-1.0f)<1.0e-4f,"the reference window is text scale 1.0");
  ok&=expect(std::abs(captionAtOne-9.0f)<0.01f,"a 9 px caption is 9 px at scale 1.0");
  editor->setSize(2000,1400);
  const float captionAtTwo=ui::scaledText(editor->field(),9.0f),menuAtTwo=look.getPopupMenuFont().getHeight();
  ok&=expect(std::abs(look.scale()-2.0f)<1.0e-4f,"twice the reference window is text scale 2.0");
  ok&=expect(std::abs(captionAtTwo-18.0f)<0.01f,"the same caption is 18 px at scale 2.0");
  ok&=expect(menuAtTwo>=menuAtOne*1.99f,"menu and control text scale with the editor instead of clamping at 11 px");
  editor->setSize(600,420);
  ok&=expect(ui::scaledText(editor->field(),9.0f)>=ui::minimumTextHeight,"text never falls below the legibility floor");
  editor->setSize(1200,800);
 }
 ok&=expect(editor->textFieldCount()==0,"default screen contains no text-entry field");
 // Slot activation in place: each catalog type lands in a typed slot and keeps its canonical id.
 ok&=expect(editor->activateSlot("harmonic",src1)&&editor->activateSlot("fm",src2)&&editor->activateSlot("noise",src3)&&editor->activateSlot("sub",subSlot)&&editor->activateSlot("resonator",res1)&&editor->activateSlot("filter",filt1)&&editor->activateSlot("shaper",shape1)&&editor->activateSlot("mixer",mix1)&&editor->activateSlot("chorus",chorusSlot)&&editor->activateSlot("delay",delaySlot)&&editor->activateSlot("reverb",reverbSlot)&&editor->activateSlot("width",widthSlot),"every catalog module can be activated in its slot");
 ok&=expect(!editor->activateSlot("filter",src1)&&!editor->activateSlot("harmonic",src1)&&!editor->activateSlot("reverb",chorusSlot),"wrong-kind and occupied slots reject activation");
 auto patch=processor.snapshot().editedPatch;ok&=expect(patch.nodes.size()==domain::moduleTypeCount&&node(patch,"src1")&&node(patch,"src3")&&node(patch,"sub1")&&node(patch,"res1")&&node(patch,"filt1")&&node(patch,"shape1")&&node(patch,"mix1")&&node(patch,"chorus1")&&node(patch,"delay1")&&node(patch,"reverb1")&&node(patch,"width1"),"all twelve module types are present with slot ids");
 const auto&map=editor->field().slotMap();ok&=expect(map.node[0]>=0&&map.node[1]>=0&&map.node[2]>=0&&map.node[3]>=0&&map.node[4]>=0&&map.node[5]<0&&map.node[6]>=0&&map.node[7]<0&&map.node[10]>=0&&map.node[11]<0&&map.node[12]>=0&&map.node[15]>=0,"slot map mirrors the patch");
 // Gesture: drag OUT→IN creates an edge through the production editPatch path.
 auto&field=editor->field();drag(*field.outputPort(src1),*field.inputPort(filt1),centreOf(*field.inputPort(filt1)));patch=processor.snapshot().editedPatch;ok&=expect(edge(patch,"src1","filt1")!=nullptr,"synthesized OUT→IN drag creates an audio edge");
 drag(*field.outputPort(filt1),*field.inputPort(ui::outputSlot),centreOf(*field.inputPort(ui::outputSlot)));patch=processor.snapshot().editedPatch;ok&=expect(edge(patch,"filt1","output")!=nullptr,"drag into the OUT bus creates the output edge");
 ok&=expect(field.cables().cables().size()==2,"one cable is routed per edge");
 // D8 audio-rate modulation inputs (#127): the second anchor takes the same drag gesture, produces
 // an ordinary audio edge on the typed port, and stays inert wherever that edge would be illegal.
 {
  auto*fmMod=field.modulationPort(src2);auto*resExcite=field.modulationPort(res1);auto*harmonicMod=field.modulationPort(src1);
  if(expect(fmMod!=nullptr&&resExcite!=nullptr&&harmonicMod!=nullptr,"source and resonator slots carry a second IN anchor"))
  {
   drag(*field.outputPort(src1),*fmMod,centreOf(*fmMod));patch=processor.snapshot().editedPatch;
   ok&=expect(edge(patch,"src1","src2",domain::AudioPort::modIn)!=nullptr&&edge(patch,"src1","src2")==nullptr,
              "the same drag gesture onto the second anchor cables fm.modIn, not the ordinary input");
   drag(*field.outputPort(src2),*resExcite,centreOf(*resExcite));patch=processor.snapshot().editedPatch;
   ok&=expect(edge(patch,"src2","res1",domain::AudioPort::exciteIn)!=nullptr,"the second anchor on a resonator cables exciteIn");
   ok&=expect(field.cables().cables().size()==4,"each typed edge routes its own cable");
   // Illegal targets stay inert during the drag: a harmonic declares no typed port, the effects
   // tail may not drive a per-voice input, and a typed edge may not close a cycle.
   field.beginConnect(src1);
   ok&=expect(!field.edgeLegal("src1","src1",domain::AudioPort::modIn)&&!field.edgeLegal("src1","src3",domain::AudioPort::modIn),
              "a slot whose type declares no typed port is not a legal audio-rate target");
   field.endDrag({-50.0f,-50.0f});
   ok&=expect(!field.edgeLegal("chorus1","src2",domain::AudioPort::modIn)&&!field.edgeLegal("chorus1","res1",domain::AudioPort::exciteIn),
              "an effects-tail node into a typed per-voice input is rejected as a global-tail violation");
   ok&=expect(!field.edgeLegal("res1","src2",domain::AudioPort::modIn),"a typed edge that would close a cycle is illegal");
   const auto typed=domain::encodeStateJson(processor.snapshot());
   drag(*field.outputPort(res1),*harmonicMod,centreOf(*harmonicMod));
   ok&=expect(domain::encodeStateJson(processor.snapshot())==typed,"dropping on an inert second anchor changes nothing");
   // Dragging the typed cable off its anchor removes exactly that edge and leaves the rest.
   drag(*resExcite,field,{4.0f,4.0f});patch=processor.snapshot().editedPatch;
   ok&=expect(edge(patch,"src2","res1",domain::AudioPort::exciteIn)==nullptr&&edge(patch,"src1","src2",domain::AudioPort::modIn)!=nullptr,
              "drag-off from the second anchor removes exactly that typed edge");
   drag(*fmMod,field,{4.0f,4.0f});patch=processor.snapshot().editedPatch;
   ok&=expect(edge(patch,"src1","src2",domain::AudioPort::modIn)==nullptr&&field.cables().cables().size()==2,"the field returns to its two ordinary cables");
  }
 }
 // Illegal targets are inert: dropping on a source IN (none exists) or creating a cycle changes nothing.
 const auto before=domain::encodeStateJson(processor.snapshot());drag(*field.outputPort(filt1),*field.inputPort(filt1),centreOf(*field.inputPort(filt1)));ok&=expect(domain::encodeStateJson(processor.snapshot())==before,"self-loop drop is inert");
 field.beginConnect(filt1);ok&=expect(field.inputPort(filt1)->isVisible()&&!field.edgeLegal("filt1","filt1")&&!field.edgeLegal("filt1","src1"),"would-cycle and source targets are illegal during a drag");field.endDrag({-50.0f,-50.0f});ok&=expect(domain::encodeStateJson(processor.snapshot())==before,"dropping nowhere during a connect drag changes nothing");
 // Gesture: dragging the IN end off the port removes the edge.
 drag(*field.inputPort(filt1),field,{4.0f,4.0f});patch=processor.snapshot().editedPatch;ok&=expect(edge(patch,"src1","filt1")==nullptr&&edge(patch,"filt1","output")!=nullptr,"drag-off from the IN port removes exactly that edge");
 ok&=expect(editor->connect("src1","filt1",.8),"programmatic connect restores the edge");ok&=expect(!editor->connect("filt1","src1",1),"edge entering a source is rejected");
 ok&=expect(editor->setEdgeGain("src1","filt1",.25)&&std::abs(edge(processor.snapshot().editedPatch,"src1","filt1")->gain-.25)<1e-9,"cable gain edits the edge");
 // Regression (#87): the gain knob is reconciled in place, so a whole drag gesture runs on one component instead of
 // dying with the knob that the refresh it triggered destroyed.
 if(auto*gain=field.cables().gainKnob("src1","filt1");expect(gain!=nullptr,"each cable exposes a gain knob"))
 {
  const double was=edge(processor.snapshot().editedPatch,"src1","filt1")->gain;const auto c=centreOf(*gain);
  gain->mouseDown(event(*gain,c));
  gain->mouseDrag(event(*gain,c.translated(0,-20),juce::ModifierKeys::leftButtonModifier,c,1,true));
  ok&=expect(field.cables().gainKnob("src1","filt1")==gain&&gain->getParentComponent()==&field.cables(),"the dragged gain knob survives the document refresh it triggers");
  gain->mouseDrag(event(*gain,c.translated(0,-60),juce::ModifierKeys::leftButtonModifier,c,1,true));
  gain->mouseUp(event(*gain,c.translated(0,-60),juce::ModifierKeys::leftButtonModifier,c,1,true));
  const double now=edge(processor.snapshot().editedPatch,"src1","filt1")->gain;
  ok&=expect(now-was>0.02&&field.cables().gainKnob("src1","filt1")==gain,"one continuous drag moves the cable gain by more than one step");
  ok&=expect(std::abs(gain->value()-now)<1e-9,"knob and stored edge gain agree after the gesture");
  ok&=expect(editor->setParameter("filt1","q",0,.5)&&field.cables().gainKnob("src1","filt1")==gain,"an ordinary parameter edit does not rebuild the cable gain knobs");
  ok&=expect(editor->disconnect("src1","filt1")&&field.cables().gainKnob("src1","filt1")==nullptr,"a topology change removes the knob of the removed cable");
  ok&=expect(editor->connect("src1","filt1",.25)&&field.cables().gainKnob("src1","filt1")!=nullptr,"reconnecting restores the edge and its knob");
 }
 // Regression (#88): the gain knob is a fixed-size overlay centred on the router's anchor, so at every window
 // size its rectangle must lie inside the cable layer. Cables routed through the corridors above and below the
 // field had their knob clipped by the field edge, and above the field that edge is the macro strip.
 {
  ok&=expect(editor->connect("src3","mix1",.3)&&editor->connect("res1","mix1",.9)&&editor->connect("src2","shape1",.5)&&editor->connect("shape1","res1",.4),"corridor-routed cables can be connected");
  for(auto size:{std::pair{1000,700},std::pair{1800,1200},std::pair{1200,800}})
  {
   editor->setSize(size.first,size.second);auto&layer=field.cables();int above=0,below=0;
   ok&=expect((double)field.getHeight()/ui::referenceHeight>=ui::minimumFieldVerticalScale-1e-9,"the field is never drawn below the vertical scale the corridor reservation assumes");
   for(const auto&d:layer.cables())
   {
    const auto*knob=layer.gainKnob(d.edge.source,d.edge.destination);
    if(knob==nullptr){ok&=expect(false,"every routed cable has a gain knob");continue;}
    if(d.cable.knobAnchor.y<ui::fieldTop())++above;else if(d.cable.knobAnchor.y>ui::fieldBottom())++below;
    ok&=expect(layer.getLocalBounds().contains(knob->getBounds()),"gain knob is fully inside the cable layer at every window size");
   }
   std::cout<<"editor: "<<size.first<<"x"<<size.second<<" routes "<<layer.cables().size()<<" cables, "<<above<<" knobs in the top corridor and "<<below<<" in the return channel\n";
   ok&=expect(above>=4&&below>=1,"the sweep covers corridor-routed knobs above and below the field");
  }
  ok&=expect(editor->disconnect("shape1","res1")&&editor->disconnect("src2","shape1")&&editor->disconnect("res1","mix1")&&editor->disconnect("src3","mix1"),"corridor cables can be removed again");
 }
 // Control kit gestures on production controls.
 auto*cutoff=field.slot(filt1).control("cutoff");ok&=expect(cutoff!=nullptr,"filter slot exposes a cutoff knob");
 if(cutoff){const double was=cutoff->value();const auto c=centreOf(*cutoff);cutoff->mouseDown(event(*cutoff,c));cutoff->mouseDrag(event(*cutoff,c.translated(0,-60),juce::ModifierKeys::leftButtonModifier,c,1,true));cutoff->mouseUp(event(*cutoff,c.translated(0,-60),juce::ModifierKeys::leftButtonModifier,c,1,true));const auto now=values(processor.snapshot().editedPatch,"filt1","cutoff")[0];ok&=expect(now>was,"vertical knob drag raises the stored cutoff");}
 auto*amplitudes=field.slot(src1).forest("partialAmplitudes");ok&=expect(amplitudes!=nullptr,"harmonic slot exposes the amplitude forest");
 if(amplitudes){const float w=(float)amplitudes->getWidth(),h=(float)amplitudes->getHeight();amplitudes->mouseDown(event(*amplitudes,{1.0f,2.0f}));amplitudes->mouseDrag(event(*amplitudes,{w-1.0f,h-2.0f},juce::ModifierKeys::leftButtonModifier,{1.0f,2.0f},1,true));amplitudes->mouseUp(event(*amplitudes,{w-1.0f,h-2.0f},juce::ModifierKeys::leftButtonModifier,{1.0f,2.0f},1,true));
  const auto v=values(processor.snapshot().editedPatch,"src1","partialAmplitudes");bool monotone=v.size()==16;for(std::size_t i=1;i<v.size()&&monotone;++i)monotone=v[i]<=v[i-1]+1e-9;ok&=expect(monotone&&v[0]>.9&&v[15]<.1,"one press-drag paints every crossed partial with interpolated values");}
 const auto oldRatio=values(processor.snapshot().editedPatch,"src1","partialRatios")[15];ok&=expect(editor->setParameter("src1","inharmonicity",0,.01),"harmonic convenience control is editable");ok&=expect(std::abs(values(processor.snapshot().editedPatch,"src1","partialRatios")[15]-oldRatio)>1e-6,"convenience edit stores the regenerated explicit array");
 // D8 spectral shape (#121): the three render-time controls are on the harmonic slot, laid out (not
 // merely constructed) and — unlike the convenience control above — they leave the stored arrays alone.
 {
  const auto ratiosBefore=values(processor.snapshot().editedPatch,"src1","partialRatios"),amplitudesBefore=values(processor.snapshot().editedPatch,"src1","partialAmplitudes");
  for(const auto*id:{"harmonicityMorph","oddEvenBalance","symmetry"})
  {
   auto*c=field.slot(src1).control(id);
   if(!expect(c!=nullptr,"harmonic slot exposes the spectral-shape control")){ok=false;continue;}
   ok&=expect(c->isVisible()&&!c->getBounds().isEmpty(),"spectral-shape control is laid out on the slot");
   ok&=expect(editor->setParameter("src1",id,0,.25),"spectral-shape control is editable");
   ok&=expect(std::abs(values(processor.snapshot().editedPatch,"src1",id)[0]-.25)<1e-9,"spectral-shape edit reaches the patch");
  }
  ok&=expect(values(processor.snapshot().editedPatch,"src1","partialRatios")==ratiosBefore&&values(processor.snapshot().editedPatch,"src1","partialAmplitudes")==amplitudesBefore,
             "spectral-shape edits never rewrite the stored spectrum");
 }
 // D8 pitch block and the sub slot (#122). The four pitch controls are a strip on both general
 // pitched sources, and the sub slot — fixed in the source column, never "added" to a list — carries
 // its waveform toggle and its own pitch controls.
 {
  for(const auto*slotId:{"src1","src2"})
  {
   auto&slot=field.slot(std::string_view(slotId)=="src1"?src1:src2); // src1 is the harmonic slot, src2 the FM one
   for(const auto*id:{"octave","coarse","fine","keytrack"})
   {
    auto*c=slot.control(id);
    if(!expect(c!=nullptr,"pitched source slot exposes the pitch control")){ok=false;continue;}
    ok&=expect(c->isVisible()&&!c->getBounds().isEmpty(),"pitch control is laid out on the slot");
   }
   ok&=expect(editor->setParameter(slotId,"coarse",0,7)&&values(processor.snapshot().editedPatch,slotId,"coarse")[0]==7.0,"coarse edit reaches the patch");
   ok&=expect(editor->setParameter(slotId,"keytrack",0,.25)&&std::abs(values(processor.snapshot().editedPatch,slotId,"keytrack")[0]-.25)<1e-9,"keytrack edit reaches the patch");
  }
  auto&sub=field.slot(subSlot);
  ok&=expect(sub.active()&&ui::slotTable[subSlot].kind==ui::SlotKind::sub,"the sub slot is active in place in the source column");
  ok&=expect(sub.toggle("waveform")!=nullptr,"the sub slot exposes its waveform toggle");
  for(const auto*id:{"octave","fine","keytrack","drift","outputLevel"})
  {
   auto*c=sub.control(id);
   if(!expect(c!=nullptr,"the sub slot exposes its control")){ok=false;continue;}
   ok&=expect(c->isVisible()&&!c->getBounds().isEmpty(),"sub control is laid out on the slot");
  }
  ok&=expect(editor->setParameter("sub1","octave",0,-2)&&values(processor.snapshot().editedPatch,"sub1","octave")[0]==-2.0,"sub octave edit reaches the patch");
  ok&=expect(editor->deactivateSlot(subSlot)&&!field.slot(subSlot).active(),"the sub slot deactivates in place");
  ok&=expect(editor->activateSlot("sub",subSlot)&&field.slot(subSlot).active()&&node(processor.snapshot().editedPatch,"sub1"),"the sub slot toggles active again at the same position");
 }
 auto*mode=field.slot(res1).toggle("mode");if(expect(mode!=nullptr,"resonator slot exposes the mode toggle")){mode->mouseDown(event(*mode,{(float)mode->getWidth()-2.0f,2.0f}));ok&=expect(values(processor.snapshot().editedPatch,"res1","mode")[0]==1.0,"segment toggle click stores the enum");}
 ok&=expect(editor->setEnvelope(0,{.05,.2,.5,.8})&&std::abs(processor.snapshot().editedPatch.envelopes[0].sustain-.5)<1e-9,"envelope curve edits store the ADSR");
 // D8 modulator settings (#125): the fourth envelope has a panel of its own, a stage curve is a
 // grip on that panel, and the LFO panels carry the appended shapes, the fade and the sync pair.
 ok&=expect(editor->setEnvelope(3,{.05,.2,.5,.8,.6,-.4,.9})&&std::abs(processor.snapshot().editedPatch.envelopes[3].attackCurve-.6)<1e-9&&std::abs(processor.snapshot().editedPatch.envelopes[3].releaseCurve-.9)<1e-9,"E4 and the stage curves reach the patch");
 {
  auto&fourth=editor->envelopeCurve(3);ok&=expect(fourth.isVisible()&&!fourth.getBounds().isEmpty(),"E4 has a laid-out panel");
  // Dragging the attack-curve grip bends only that stage: the attack time and the sustain stay put.
  const auto before=processor.snapshot().editedPatch.envelopes[3];
  fourth.setEnvelope({before.attack,before.decay,before.sustain,before.release,0,0,0},true);
  auto*grip=&fourth;const auto height=(float)grip->getHeight();
  grip->mouseDown(event(*grip,{(float)grip->getWidth()*.14f,height*.5f}));grip->mouseDrag(event(*grip,{(float)grip->getWidth()*.14f,height*.9f}));grip->mouseUp(event(*grip,{(float)grip->getWidth()*.14f,height*.9f}));
  const auto after=processor.snapshot().editedPatch.envelopes[3];
  ok&=expect(after.attackCurve!=0.0&&after.attack==before.attack&&after.sustain==before.sustain&&after.release==before.release,"a curve grip bends the stage without moving its endpoints");
 }
 {
  auto lfo=processor.snapshot().editedPatch.lfos[0];lfo.waveform=domain::LfoWaveform::sampleHold;lfo.fadeMs=750;lfo.syncMode=domain::LfoSyncMode::sync;lfo.syncDivision=domain::LfoSyncDivision::eighth;
  ok&=expect(editor->setLfo(0,lfo),"LFO settings can be edited");
  const auto stored=processor.snapshot().editedPatch.lfos[0];
  ok&=expect(stored.waveform==domain::LfoWaveform::sampleHold&&stored.fadeMs==750&&stored.syncMode==domain::LfoSyncMode::sync&&stored.syncDivision==domain::LfoSyncDivision::eighth,"the appended LFO settings reach the patch");
 }
 ok&=expect(ui::modulationSourceName(domain::ModulationSource::e4)=="E4","the editor names the fourth envelope source");
 // Lanes.
 ok&=expect(editor->addLane()&&editor->lanes().laneCount()==1,"lane can be added");
 // Every source the domain declares is offered, so `e4`, `velocity` and `keyTracking` are all selectable.
 ok&=expect(editor->lanes().lane(0).sourcePicker().getNumItems()==(int)domain::modulationSourceCount,"the lane offers every modulation source");
 ok&=expect(editor->setLane(0,{"",true,domain::ModulationSource::e4,"filt1","cutoff",0.5})&&processor.snapshot().editedPatch.matrix[0].source==domain::ModulationSource::e4,"E4 is selectable as a matrix source");
 ok&=expect(editor->setLane(0,{"",true,domain::ModulationSource::keyTracking,"filt1","cutoff",0.5})&&processor.snapshot().editedPatch.matrix[0].source==domain::ModulationSource::keyTracking,"key tracking is selectable as a matrix source");
 ok&=expect(editor->setLane(0,{"",true,domain::ModulationSource::velocity,"filt1","cutoff",0.5})&&processor.snapshot().editedPatch.matrix[0].source==domain::ModulationSource::velocity,"velocity is selectable as a matrix source");
 ok&=expect(editor->setLane(0,{"",true,domain::ModulationSource::l1,"filt1","cutoff",1.0}),"lane can be edited");
 patch=processor.snapshot().editedPatch;ok&=expect(patch.matrix.size()==1&&patch.matrix[0].source==domain::ModulationSource::l1&&patch.matrix[0].destinationParameter=="cutoff","lane edits reach the matrix");ok&=expect(editor->lanes().lane(0).sourcePicker().getSelectedId()==4&&editor->lanes().lane(0).depthBar().value()==1.0,"lane view mirrors the row");
 // Effective rings: with an L1→cutoff lane the knob carries an accent value from the engine snapshot; without, none.
 juce::AudioBuffer<float>audio(2,128);juce::MidiBuffer midi;auto block=[&]{midi.clear();processor.processBlock(audio,midi);};for(int i=0;i<8;++i)block();tick();for(int i=0;i<16;++i)block();processor.keyboardState().noteOn(1,60,.8f);for(int i=0;i<16;++i)block();ok&=expect(processor.activeVoiceCount()>0,"audition keyboard reaches production MIDI path");
field.setEffective(processor.effectiveValues());ok&=expect(cutoff&&cutoff->effective().has_value(),"targeted knob shows the effective value");ok&=expect(field.slot(filt1).control("q")&&!field.slot(filt1).control("q")->effective().has_value(),"untargeted knob shows no ring");
 ok&=expect(editor->setLane(0,{"",false,domain::ModulationSource::l1,"filt1","cutoff",1.0}),"lane can be disabled");for(int i=0;i<8;++i)block();tick();for(int i=0;i<16;++i)block();field.setEffective(processor.effectiveValues());ok&=expect(cutoff&&!cutoff->effective().has_value(),"disabled lane removes the ring");
 block();ok&=expect(processor.activeVoiceCount()>0,"lane edit preserves held audition note");
 // Precision entry never sends audition MIDI.
 processor.keyboardState().allNotesOff(1);for(int i=0;i<40;++i)block();
 if(cutoff){cutoff->mouseDoubleClick(event(*cutoff,centreOf(*cutoff),juce::ModifierKeys::leftButtonModifier,{},2));ok&=expect(editor->entryOpen(),"double-click opens precision entry");auto&entry=editor->precisionEntry();for(auto ch:{'6','0','0'})entry.keyPressed(juce::KeyPress(ch,{},(juce::juce_wchar)ch));entry.keyPressed(juce::KeyPress('a',{},'a'));
  ok&=expect(!anyNoteOn(processor.keyboardState()),"typing in precision entry sends no audition MIDI");entry.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));ok&=expect(!editor->entryOpen()&&std::abs(values(processor.snapshot().editedPatch,"filt1","cutoff")[0]-600.0)<1e-6,"Enter commits the typed cutoff");
  cutoff->mouseDoubleClick(event(*cutoff,centreOf(*cutoff),juce::ModifierKeys::leftButtonModifier,{},2));editor->precisionEntry().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));ok&=expect(!editor->entryOpen()&&std::abs(values(processor.snapshot().editedPatch,"filt1","cutoff")[0]-600.0)<1e-6,"Escape cancels precision entry");}
 // Deactivating a connected slot removes its node, cables and lanes in one transaction.
 ok&=expect(editor->deactivateSlot(filt1),"connected slot can be deactivated");patch=processor.snapshot().editedPatch;ok&=expect(!node(patch,"filt1")&&patch.edges.empty()&&patch.matrix.empty()&&!field.slot(filt1).active(),"deactivation removes node, edges and rows");
 ok&=expect(editor->activateSlot("filter",filt1)&&editor->connect("src1","filt1",.8)&&editor->connect("filt1","output",.7),"slot reactivates in place");
 // Exact round trip through the shared codec.
 const auto valid=domain::encodeStateJson(processor.snapshot());const auto path=std::filesystem::temp_directory_path()/"iupac-editor-roundtrip.iupacpatch";ok&=expect(processor.saveStateFile(path).empty(),"editor state saves through shared codec");ok&=expect(processor.newDocument().empty()&&processor.loadStateFile(path).empty(),"file state loads through shared codec");ok&=expect(domain::encodeStateJson(processor.snapshot())==valid,"file round trip is exact");std::error_code ec;std::filesystem::remove(path,ec);
 editor->refresh();ok&=expect(field.slot(src1).active()&&field.slot(filt1).active()&&field.cables().cables().size()==2,"loaded document re-populates slots and cables");
 // Open/close while notes are held.
 processor.keyboardState().noteOn(1,64,.9f);block();ok&=expect(processor.activeVoiceCount()>0,"note is held before closing the editor");editor->setVisible(false);editor.reset();block();ok&=expect(processor.activeVoiceCount()>0,"closing the editor keeps the held note");
 editor=std::make_unique<IupacSynthEditor>(processor);editor->setVisible(true);block();ok&=expect(processor.activeVoiceCount()>0&&editor->field().slot(src1).active(),"reopened editor shows the document and keeps the note");processor.keyboardState().allNotesOff(1);
#if IUPAC_ENABLE_CHEMISTRY
 {ChemistryPopup popup(processor);popup.setVisible(true);ok&=expect(popup.getWidth()>0,"chemistry popup constructs in the extension build");}
 // #99/#101: the launcher must produce a popup the user can see and dismiss, and it must be an in-editor overlay —
 // a child of the editor, with no desktop peer and no always-on-top — so it can never float above other applications
 // and always travels with the host window. Found through JUCE's own modal stack rather than an editor accessor;
 // blocking is read through the production predicate `isCurrentlyBlockedByAnotherModalComponent()`, the same one
 // Component::internalMouseDown consults.
 {
  // A host always gives the editor a window. Without a peer nothing in the editor is `isShowing()`, and `isShowing()`
  // is precisely what JUCE's modal dismissal reads (#99), so the guard has to run against a hosted editor.
  editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
  auto*launcher=labelled(*editor,"chemistry");
  ok&=expect(launcher!=nullptr,"the editor exposes the chemistry launcher");
  if(launcher)
  {
   ok&=expect(!launcher->isCurrentlyBlockedByAnotherModalComponent(),"the editor accepts clicks before the overlay opens");
   click(*launcher);
   auto*overlay=dynamic_cast<ChemistryOverlay*>(juce::Component::getCurrentlyModalComponent(0));
   ok&=expect(overlay!=nullptr,"the launcher makes the chemistry overlay the current modal component");
   if(overlay)
   {
    // #101: the three properties that made the old dialog a system-wide floating window.
    ok&=expect(!overlay->isOnDesktop(),"the chemistry overlay owns no desktop peer");
    ok&=expect(!overlay->isAlwaysOnTop(),"the chemistry overlay is not always-on-top");
    ok&=expect(overlay->getParentComponent()==editor.get(),"the chemistry overlay is a child of the editor");
    ok&=expect(overlay->isVisible()&&overlay->isShowing(),"the chemistry overlay is visible and showing");
    ok&=expect(editor->getLocalBounds().contains(overlay->getBounds()),"the overlay lies inside the editor");
    ok&=expect(overlay->getBounds().contains(overlay->contentBounds())&&!overlay->contentBounds().isEmpty(),"the popup lies inside the overlay with non-zero bounds");
    ok&=expect(launcher->isCurrentlyBlockedByAnotherModalComponent(),"the open overlay blocks the launcher-adjacent editor control");
    ok&=expect(!editor->keyboard().isCurrentlyBlockedByAnotherModalComponent(),"the audition keyboard still receives events while the overlay is open");
    ok&=expect(overlay->getBottom()<=editor->keyboard().getY(),"the overlay stops above the audition keyboard it leaves clickable");
    // #101: resizing the editor keeps the popup inside it rather than leaving a detached window behind.
    editor->setSize(1000,700);
    ok&=expect(editor->getLocalBounds().contains(overlay->getBounds())&&overlay->getBottom()<=editor->keyboard().getY(),"the overlay follows an editor resize");
    ok&=expect(overlay->getBounds().contains(overlay->contentBounds()),"the popup stays inside the overlay after a resize");
    editor->setSize(1200,800);
    juce::Component::SafePointer<ChemistryOverlay>first(overlay);
    overlay->dismiss();
    // Hiding cancels the modal item at once; the editor drops the overlay on a later message, so the assertions
    // below deliberately do not depend on that having happened yet.
    ok&=expect(juce::Component::getCurrentlyModalComponent(0)==nullptr,"closing the overlay leaves nothing modal");
    ok&=expect(!overlay->isShowing(),"the closed overlay is no longer showing");
    ok&=expect(!launcher->isCurrentlyBlockedByAnotherModalComponent(),"the editor accepts the click again after the overlay closes");
    // Re-opening must give a fresh overlay: the editor drops its pointer to the dismissed one instead of bringing
    // that hidden corpse to front, which is the state a stale pointer would leave the user stuck in (#99).
    click(*launcher);
    auto*reopened=dynamic_cast<ChemistryOverlay*>(juce::Component::getCurrentlyModalComponent(0));
    // The dismissed overlay must really be gone: a freshly allocated one can land on the same address, so identity is
    // read through a SafePointer rather than by comparing raw pointers.
    ok&=expect(first==nullptr,"the dismissed overlay is destroyed rather than revived");
    ok&=expect(reopened!=nullptr,"the launcher opens a fresh overlay after the first was dismissed");
    ok&=expect(reopened!=nullptr&&!reopened->isOnDesktop()&&!reopened->isAlwaysOnTop()&&reopened->isShowing(),"the reopened overlay is a showing editor child, not a desktop window");
    if(reopened)reopened->dismiss();
    ok&=expect(juce::Component::getCurrentlyModalComponent(0)==nullptr,"the reopened overlay dismisses the same way");
   }
  }
  editor->removeFromDesktop();
 }
 // #97: a slow helper must not become a frozen editor. Apply hands the work to the
 // coordinator's worker and returns; the popup stays open, says which stage it is in,
 // keeps its input, and Cancel is the way out.
 if(argc==2)
 {
  ChemistryPopup popup(processor);popup.setVisible(true);
  auto*input=named<juce::TextEditor>(popup,"Molecular input");auto*status=named<juce::Label>(popup,"Chemistry status");
  auto*apply=labelled(popup,"apply");auto*cancel=labelled(popup,"cancel");
  ok&=expect(input!=nullptr&&status!=nullptr&&apply!=nullptr&&cancel!=nullptr,"the popup exposes its molecular input, status and buttons");
  if(input&&status&&apply&&cancel)
  {
   input->setText("slow");
   const auto pressed=std::chrono::steady_clock::now();
   click(*apply);
   const auto waited=std::chrono::steady_clock::now()-pressed;
   // One frame at the editor's 30 Hz refresh is 33 ms; the helper fixture sleeps 5 s.
   ok&=expect(waited<std::chrono::milliseconds(33),"Apply never waits for the helper on the message thread");
   ok&=expect(popup.isVisible(),"the popup stays open while its request runs");
   ok&=expect(!apply->isEnabled(),"Apply is unavailable while its own request is in flight");
   ok&=expect(input->getText()=="slow","the pending popup keeps the input that is being analysed");
   ok&=expect(processor.chemistryStatus().busy,"the processor reports the request as busy");
   ok&=expect(status->getText().isNotEmpty()&&status->getText()!="Ready","the popup shows the stage it is waiting in");
   // Let the popup's own 10 Hz refresh run. A helper that has not answered yet must leave
   // the popup pending; only a finished request may release it.
   tick();
   ok&=expect(!apply->isEnabled()&&popup.isVisible()&&processor.chemistryStatus().busy,"a helper that has not answered leaves the popup pending, not closed");
   ok&=expect(status->getText()==juce::String(std::string(chemistry::stageDescription(chemistry::Stage::analysing))),"the pending status names the stage the coordinator reports");
   const auto cancelled=std::chrono::steady_clock::now();
   click(*cancel);
   ok&=expect(std::chrono::steady_clock::now()-cancelled<std::chrono::milliseconds(33),"Cancel never waits on the message thread either");
   ok&=expect(apply->isEnabled()&&popup.isVisible(),"cancelling returns the open popup to an interactive state");
   ok&=expect(!processor.chemistryStatus().busy,"a cancelled request is no longer busy");
  }
 }
 // #103: a bounded offline search returns far more candidates than one line of UI shows. The candidate list must be
 // a real viewport — every entry reachable by wheel, by scrollbar drag and by arrow key, with the selection applying
 // to the record the row stands for. Driven through the production search/populate path, not by poking the list.
 if(argc==2)
 {
  ChemistryPopup popup(processor);popup.setLookAndFeel(&editor->getLookAndFeel());popup.setVisible(true);
  auto*query=named<juce::TextEditor>(popup,"Offline discovery query");
  auto*search=labelled(popup,"search");
  auto*list=named<juce::ListBox>(popup,"Bounded offline discovery candidates or cached results");
  auto*metadata=named<juce::Label>(popup,"Selected record structure and provenance");
  ok&=expect(query!=nullptr&&search!=nullptr&&list!=nullptr&&metadata!=nullptr,"the popup exposes the offline query and the candidate list");
  if(query&&search&&list&&metadata)
  {
   query->setText("acid");click(*search);
   for(int i=0;i<300&&processor.discoveryStatus().busy;++i)juce::Thread::sleep(10);
   tick();// the popup's own 10 Hz refresh is what moves a finished search into the list
   const int rows=list->getListBoxModel()!=nullptr?list->getListBoxModel()->getNumRows():0;
   ok&=expect(rows==50,"the finished search populates the candidate list with every returned record");
   auto*viewport=list->getViewport();
   ok&=expect(viewport!=nullptr&&viewport->getViewedComponent()!=nullptr,"the candidate list is backed by a viewport");
   if(rows==50&&viewport&&viewport->getViewedComponent())
   {
    const int content=rows*list->getRowHeight();
    ok&=expect(viewport->getViewHeight()<content,"the visible candidate viewport is smaller than its content");
    ok&=expect(list->getNumRowsOnScreen()<rows,"more candidates exist than fit on screen");
    ok&=expect(list->getVerticalScrollBar().isVisible(),"the candidate list shows the scrollbar that makes it draggable");
    // Wheel.
    const int top=viewport->getViewPositionY();
    juce::MouseWheelDetails wheel{};wheel.deltaY=-1.0f;
    viewport->mouseWheelMove(event(*viewport,centreOf(*viewport)),wheel);
    ok&=expect(viewport->getViewPositionY()>top,"the mouse wheel scrolls the candidate list");
    // Arrow keys move the selection and drag the viewport along to it, including past the visible window.
    list->selectRow(0);
    ok&=expect(list->getSelectedRow()==0,"the first candidate selects");
    ok&=expect(list->keyPressed(juce::KeyPress(juce::KeyPress::downKey))&&list->getSelectedRow()==1,"the down arrow moves the candidate selection");
    list->selectRow(rows-1);
    ok&=expect(list->getSelectedRow()==rows-1,"the last candidate is reachable");
    const auto last=juce::Rectangle<int>(0,(rows-1)*list->getRowHeight(),1,list->getRowHeight());
    ok&=expect(viewport->getViewArea().intersects(last),"selecting the last candidate scrolls it into view");
    // Selection applies: the provenance label names the record the selected row stands for.
    ok&=expect(metadata->getText().contains("Q1049"),"the selected candidate's record drives the provenance label");
    list->deselectAllRows();
    ok&=expect(metadata->getText().isEmpty(),"deselecting clears the provenance label");
   }
  }
  popup.setLookAndFeel(nullptr);
 }
#endif
 editor->setVisible(false);editor.reset();return ok?0:1;
}
