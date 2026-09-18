#include "PluginEditor.hpp"
#include "PluginProcessor.hpp"
#if IUPAC_ENABLE_CHEMISTRY
#include "ChemistryPopup.hpp"
#endif
using namespace iupac;
namespace
{
std::string uniqueRouteId(const domain::Patch&p){for(int i=1;i<=1000;++i){auto s="route"+std::to_string(i);if(std::ranges::none_of(p.matrix,[&](const auto&r){return r.id==s;}))return s;}return{};}
domain::ParameterDescriptor hostDescriptor(std::string_view id,std::string_view unit,double lo,double hi,double def){return{id,unit,lo,hi,def,domain::ParameterScale::linear,domain::ParameterKind::continuous,false,0,20.0,{}};}
}
// Macro caption: double-click opens the inline entry for "LABEL | default".
struct IupacSynthEditor::MacroLabel final:juce::Component
{
 MacroLabel(IupacSynthEditor&e,std::size_t i):editor(e),index(i){setName("Macro "+juce::String((int)i+1)+" label and default");}
 void paint(juce::Graphics&g)override{g.setColour(ui::ink);ui::drawCaption(g,text,getLocalBounds(),juce::Justification::centred,9.0f);}
 void mouseDoubleClick(const juce::MouseEvent&)override{editor.openEntry(*this,getLocalBounds().withSizeKeepingCentre(juce::jmax(getWidth(),90),16),text+" | "+juce::String(defaultValue,3),[this](juce::String t){const auto bar=t.indexOfChar('|');const auto label=(bar>=0?t.substring(0,bar):t).trim().toStdString();const double value=bar>=0?juce::jlimit(0.0,1.0,t.substring(bar+1).getDoubleValue()):defaultValue;editor.setMacro(index,{label,value});});}
 IupacSynthEditor&editor;std::size_t index;juce::String text;double defaultValue{};
};
struct IupacSynthEditor::MeterView final:juce::Component
{
 MeterView(){setName("Output meter, active voices, and audible generation");setInterceptsMouseClicks(false,false);}
 void paint(juce::Graphics&g)override{auto r=getLocalBounds();auto bar=r.removeFromBottom(4).reduced(2,0);g.setColour(ui::ink.withAlpha(0.2f));g.fillRect(bar);const float n=juce::jlimit(0.0f,1.0f,(peakDb+60.0f)/66.0f);g.setColour(peakDb>-0.1f?ui::accent:ui::ink);g.fillRect(bar.withWidth(juce::roundToInt(n*(float)bar.getWidth())));g.setColour(ui::ink);ui::drawCaption(g,text,r,juce::Justification::centredRight,9.0f);}
 float peakDb{-100.0f};juce::String text;
};
struct IupacSynthEditor::ValueBubble final:juce::Component
{
 ValueBubble(){setInterceptsMouseClicks(false,false);setAlwaysOnTop(true);}
 void paint(juce::Graphics&g)override{auto r=getLocalBounds().toFloat().reduced(0.5f);g.setColour(ui::ground);g.fillRoundedRectangle(r,2.0f);g.setColour(ui::ink);g.drawRoundedRectangle(r,2.0f,ui::hairline);ui::drawCaption(g,text,getLocalBounds(),juce::Justification::centred,9.0f);}
 juce::String text;
};
IupacSynthEditor::IupacSynthEditor(IupacSynthProcessor&o):AudioProcessorEditor(o),owner_(o),keyboard_(o.keyboardState(),juce::MidiKeyboardComponent::horizontalKeyboard)
{
 setLookAndFeel(&laf_);setTitle("IUPAC Synth 2 editor");setDescription("Single-screen modular synthesizer editor");
 for(auto*b:{&newButton_,&loadButton_,&saveButton_,&savePresetButton_,&resetPatchButton_,&resetControlsButton_})addAndMakeVisible(b);newButton_.setComponentID("new");loadButton_.setComponentID("load");saveButton_.setComponentID("save");savePresetButton_.setComponentID("preset");resetPatchButton_.setComponentID("reset");resetControlsButton_.setComponentID("resetControls");
 presetList_.setName("File-backed preset library");presetList_.setTextWhenNothingSelected("presets");addAndMakeVisible(presetList_);meter_=std::make_unique<MeterView>();addAndMakeVisible(*meter_);
 constexpr std::array ids{"macro1","macro2","macro3","macro4","outputGain","width","masterTune","bypass"};
 auto attach=[&](std::size_t i,ui::ValueControl&c){auto*p=o.parameters().getParameter(ids[i]);attachments_[i]=std::make_unique<juce::ParameterAttachment>(*p,[&c](float v){c.setValue(v,false);},nullptr);attachments_[i]->sendInitialUpdate();c.onGestureStart=[this,i]{attachments_[i]->beginGesture();};c.onGestureEnd=[this,i]{attachments_[i]->endGesture();};c.onChange=[this,i](double v){attachments_[i]->setValueAsPartOfGesture((float)v);};};
 for(std::size_t i=0;i<4;++i){macroKnobs_[i]=std::make_unique<ui::Knob>(hostDescriptor(ids[i],"",0,1,0));macroKnobs_[i]->setName("Macro "+juce::String((int)i+1));macroKnobs_[i]->setStep(0.02);addAndMakeVisible(*macroKnobs_[i]);attach(i,*macroKnobs_[i]);macroLabels_[i]=std::make_unique<MacroLabel>(*this,i);addAndMakeVisible(*macroLabels_[i]);}
 outputGain_=std::make_unique<ui::Knob>(hostDescriptor("outputGain","dB",-60,6,-6));outputGain_->setCaption("gain");outputGain_->setName("outputGain");addAndMakeVisible(*outputGain_);attach(4,*outputGain_);
 width_=std::make_unique<ui::Fader>(hostDescriptor("width","",0,1,.5),false,false);width_->setCaption("width");width_->setName("width");addAndMakeVisible(*width_);attach(5,*width_);
 masterTune_=std::make_unique<ui::Knob>(hostDescriptor("masterTune","st",-12,12,0));masterTune_->setCaption("tune");masterTune_->setName("masterTune");addAndMakeVisible(*masterTune_);attach(6,*masterTune_);
 bypass_.setClickingTogglesState(true);bypass_.setName("bypass");addAndMakeVisible(bypass_);{auto*p=o.parameters().getParameter(ids[7]);attachments_[7]=std::make_unique<juce::ParameterAttachment>(*p,[this](float v){bypass_.setToggleState(v>=.5f,juce::dontSendNotification);},nullptr);attachments_[7]->sendInitialUpdate();bypass_.onClick=[this]{attachments_[7]->setValueAsCompleteGesture(bypass_.getToggleState()?1.0f:0.0f);};}
 addAndMakeVisible(field_);
 field_.onActivate=[this](std::string_view type,std::size_t slot){activateSlot(type,slot);};field_.onDeactivate=[this](std::size_t slot){deactivateSlot(slot);};field_.onConnect=[this](std::string s,std::string d,double g){connect(std::move(s),std::move(d),g);};field_.onDisconnect=[this](std::string s,std::string d){disconnect(std::move(s),std::move(d));};field_.onGain=[this](std::string s,std::string d,double g){setEdgeGain(std::move(s),std::move(d),g);};
 field_.onParameter=[this](std::string node,std::string parameter,std::vector<double>values){if(values.size()==1)setParameter(node,parameter,0,values[0]);else setParameterArray(node,parameter,std::move(values));};
 for(std::size_t i=0;i<3;++i){envelopes_[i].setCaption("E"+juce::String((int)i+1));envelopes_[i].setName("E"+juce::String((int)i+1)+" envelope");envelopes_[i].onChange=[this,i](domain::Envelope e){setEnvelope(i,e);};addAndMakeVisible(envelopes_[i]);}
 for(std::size_t i=0;i<2;++i){lfoRates_[i]=std::make_unique<ui::Knob>(domain::ParameterDescriptor{"rate","Hz",.05,12,1,domain::ParameterScale::logarithmic,domain::ParameterKind::continuous,false,0,20.0,{}});lfoRates_[i]->setCaption("L"+juce::String((int)i+1)+" rate");lfoRates_[i]->setName("L"+juce::String((int)i+1)+" rate");lfoRates_[i]->setStep(0.02);lfoRates_[i]->onChange=[this,i](double v){auto l=owner_.snapshot().editedPatch.lfos[i];l.rate=v;setLfo(i,l);};addAndMakeVisible(*lfoRates_[i]);
  lfoPreviews_[i].setCaption("L"+juce::String((int)i+1));addAndMakeVisible(lfoPreviews_[i]);lfoWaveforms_[i]=std::make_unique<ui::SegmentToggle>(std::vector<juce::String>{"sine","tri"});lfoWaveforms_[i]->setName("L"+juce::String((int)i+1)+" waveform");lfoWaveforms_[i]->onChange=[this,i](int w){auto l=owner_.snapshot().editedPatch.lfos[i];l.waveform=(domain::LfoWaveform)w;setLfo(i,l);};addAndMakeVisible(*lfoWaveforms_[i]);}
 addAndMakeVisible(lanes_);lanes_.onAdd=[this]{addLane();};lanes_.onEdit=[this](std::size_t i,domain::MatrixRow r){setLane(i,std::move(r));};lanes_.onRemove=[this](std::size_t i){removeLane(i);};
 lanes_.onSelect=[this](std::optional<std::size_t>i){const auto p=field_.patch();if(i&&*i<p.matrix.size())field_.setHighlightedParameter(p.matrix[*i].destinationNode,p.matrix[*i].destinationParameter);else field_.setHighlightedParameter({},{});};
 keyboard_.setName("Audition keyboard");keyboard_.setWantsKeyboardFocus(false);keyboard_.setKeyWidth(14.0f);addAndMakeVisible(keyboard_);
 entry_.setName("Precision entry");entry_.setVisible(false);addChildComponent(entry_);entry_.onCommit=[this](juce::String t){auto c=std::move(entryCommit_);entryCommit_=nullptr;closeEntry();if(c)c(t);};entry_.onCancel=[this]{entryCommit_=nullptr;closeEntry();};
 bubble_=std::make_unique<ValueBubble>();addChildComponent(*bubble_);
 newButton_.onClick=[this]{showResult(owner_.newDocument(),"New authored patch");refresh();};resetPatchButton_.onClick=[this]{showResult(owner_.resetPatchEdits(),"Edits reset");refresh();};resetControlsButton_.onClick=[this]{owner_.resetControls();showResult({},"Controls reset");};
 presetList_.onChange=[this]{auto i=presetList_.getSelectedItemIndex();if(i>=0&&i<(int)presets_.size()){showResult(owner_.loadStateFile(presets_[(std::size_t)i]),"Preset loaded");refresh();}};
 auto choose=[this](bool save,bool preset){chooser_=std::make_unique<juce::FileChooser>(save?"Save IUPAC patch":"Load IUPAC patch",juce::File(preset?presetDirectory().string():std::string{}),"*.iupacpatch");auto flags=save?(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles):(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles);chooser_->launchAsync(flags,[this,save,preset](const juce::FileChooser&fc){auto f=fc.getResult();if(f==juce::File{})return;if(save&&!f.hasFileExtension("iupacpatch"))f=f.withFileExtension("iupacpatch");showResult(save?owner_.saveStateFile(f.getFullPathName().toStdString()):owner_.loadStateFile(f.getFullPathName().toStdString()),save?"Patch saved":"Patch loaded");if(preset)refreshPresets();refresh();});};
 loadButton_.onClick=[choose]{choose(false,false);};saveButton_.onClick=[choose]{choose(true,false);};savePresetButton_.onClick=[choose]{choose(true,true);};
#if IUPAC_ENABLE_CHEMISTRY
 chemistryButton_.setComponentID("chemistry");chemistryButton_.setName("Chemistry launcher");chemistryButton_.onClick=[this]{openChemistry();};addAndMakeVisible(chemistryButton_);
#endif
 status_="Manual synth ready";setResizable(true,true);setResizeLimits(1000,700,1800,1200);setSize(1200,800);refreshPresets();refresh();startTimerHz(30);
}
IupacSynthEditor::~IupacSynthEditor()
{
 stopTimer();
#if IUPAC_ENABLE_CHEMISTRY
 if(chemistryDialog_!=nullptr)delete chemistryDialog_.getComponent();
#endif
 setLookAndFeel(nullptr);
}
void IupacSynthEditor::paint(juce::Graphics&g)
{
 g.fillAll(ui::ground);g.setColour(ui::ink);const auto strip=getLocalBounds().withHeight(78);g.drawLine(0,(float)strip.getBottom()+0.5f,(float)getWidth(),(float)strip.getBottom()+0.5f,ui::hairline);
 const auto statusArea=juce::Rectangle<int>(newButton_.getX(),newButton_.getBottom()+2,meter_->getX()-newButton_.getX()-8,14);g.setColour(statusError_?ui::accent:ui::ink);ui::drawCaption(g,status_,statusArea,juce::Justification::centredLeft,8.0f);
 g.setColour(ui::ink);g.drawLine(0,(float)field_.getBottom()+0.5f,(float)getWidth(),(float)field_.getBottom()+0.5f,ui::hairline);g.drawLine(0,(float)lanes_.getY()-0.5f,(float)getWidth(),(float)lanes_.getY()-0.5f,ui::hairline);g.drawLine(0,(float)keyboard_.getY()-0.5f,(float)getWidth(),(float)keyboard_.getY()-0.5f,ui::hairline);
}
void IupacSynthEditor::resized()
{
 auto r=getLocalBounds();auto strip=r.removeFromTop(78).reduced(8,4);auto row=strip.removeFromTop(24);
 for(auto*b:{&newButton_,&loadButton_,&saveButton_,&savePresetButton_,&resetPatchButton_,&resetControlsButton_}){b->setBounds(row.removeFromLeft(b==&resetPatchButton_||b==&resetControlsButton_?112:84));row.removeFromLeft(4);}
 presetList_.setBounds(row.removeFromLeft(150));meter_->setBounds(row.removeFromRight(230));
 strip.removeFromTop(16);auto controls=strip;const int knob=controls.getHeight();
#if IUPAC_ENABLE_CHEMISTRY
 chemistryButton_.setBounds(controls.removeFromRight(110).withSizeKeepingCentre(110,24));controls.removeFromRight(12);
#endif
 bypass_.setBounds(controls.removeFromRight(64).withSizeKeepingCentre(64,20));controls.removeFromRight(8);masterTune_->setBounds(controls.removeFromRight(knob));controls.removeFromRight(8);width_->setBounds(controls.removeFromRight(90).withSizeKeepingCentre(90,26));controls.removeFromRight(8);outputGain_->setBounds(controls.removeFromRight(knob));controls.removeFromRight(16);
 for(std::size_t i=0;i<4;++i){auto cell=controls.removeFromLeft(juce::jmin(96,controls.getWidth()/6));macroLabels_[i]->setBounds(cell.removeFromBottom(12));macroKnobs_[i]->setBounds(cell.withSizeKeepingCentre(cell.getHeight(),cell.getHeight()));}
 keyboard_.setBounds(r.removeFromBottom(52));lanes_.setBounds(r.removeFromBottom(juce::jmax(96,r.getHeight()/5)).reduced(8,2));auto modulators=r.removeFromBottom(60).reduced(8,2);field_.setBounds(r);
 const int envelope=juce::jmin(150,modulators.getWidth()/6);for(auto&e:envelopes_){e.setBounds(modulators.removeFromLeft(envelope));modulators.removeFromLeft(8);}
 const int lfoWidth=juce::jmin(200,(modulators.getWidth()-8)/2);for(std::size_t i=0;i<2;++i){auto cell=modulators.removeFromLeft(lfoWidth);lfoRates_[i]->setBounds(cell.removeFromLeft(cell.getHeight()));lfoWaveforms_[i]->setBounds(cell.removeFromRight(50).withSizeKeepingCentre(50,14));cell.removeFromRight(4);lfoPreviews_[i].setBounds(cell);modulators.removeFromLeft(8);}
 if(entry_.isVisible())closeEntry();
}
std::size_t IupacSynthEditor::textFieldCount()const
{
 std::size_t n=0;std::function<void(const juce::Component&)>walk=[&](const juce::Component&c){if(!c.isVisible())return;if(dynamic_cast<const juce::TextEditor*>(&c))++n;for(int i=0;i<c.getNumChildComponents();++i)walk(*c.getChildComponent(i));};walk(*this);return n;
}
void IupacSynthEditor::showValue(juce::Component&anchor,juce::String text)
{
 bubble_->text=text;const int w=juce::jmax(40,(int)juce::GlyphArrangement::getStringWidth(ui::labelFont(9.0f),text.toUpperCase())+14);auto area=getLocalArea(&anchor,anchor.getLocalBounds());auto b=juce::Rectangle<int>(w,16).withCentre({area.getCentreX(),area.getY()-10});if(b.getY()<0)b.setY(area.getBottom()+2);b.setX(juce::jlimit(0,juce::jmax(0,getWidth()-w),b.getX()));bubble_->setBounds(b);bubble_->setVisible(true);bubble_->toFront(false);bubble_->repaint();
}
void IupacSynthEditor::hideValue(){bubble_->setVisible(false);}
void IupacSynthEditor::openEntry(juce::Component&anchor,juce::Rectangle<int>bounds,juce::String initial,std::function<void(juce::String)>commit)
{
 hideValue();entryCommit_=std::move(commit);auto b=getLocalArea(&anchor,bounds);b=b.withSizeKeepingCentre(juce::jmax(b.getWidth(),52),juce::jmax(16,juce::jmin(b.getHeight(),18)));b.setX(juce::jlimit(0,juce::jmax(0,getWidth()-b.getWidth()),b.getX()));entry_.setBounds(b);entry_.setText(initial,false);entry_.setVisible(true);entry_.toFront(true);entry_.grabKeyboardFocus();entry_.selectAll();
}
void IupacSynthEditor::closeEntry(){if(!entry_.isVisible())return;entry_.setVisible(false);entry_.giveAwayKeyboardFocus();}
void IupacSynthEditor::showResult(const std::string&e,juce::String ok){status_=e.empty()?ok:juce::String("Rejected: ")+e;statusError_=!e.empty();repaint(0,24,getWidth(),24);}
std::filesystem::path IupacSynthEditor::presetDirectory()const{auto p=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("IUPAC Synth 2").getChildFile("Presets");p.createDirectory();return p.getFullPathName().toStdString();}
bool IupacSynthEditor::apply(const std::function<void(domain::Patch&)>&edit,juce::String ok){const auto e=owner_.editPatch(edit);showResult(e,std::move(ok));refresh();return e.empty();}
bool IupacSynthEditor::activateSlot(std::string_view type,std::size_t slot)
{
 const auto*d=domain::findModule(type);if(!d||!ui::slotAccepts(slot,d->type)){showResult("slot does not accept this module type",{});return false;}
 if(field_.slot(slot).active()){showResult("slot is already active",{});return false;}
 return apply([&](domain::Patch&p){p.nodes.push_back(ui::defaultNode(ui::uniqueNodeId(p,slot),*d));},"Slot activated");
}
bool IupacSynthEditor::deactivateSlot(std::size_t slot)
{
 if(slot>=ui::moduleSlotCount||!field_.slot(slot).active()){showResult("slot is not active",{});return false;}const auto id=field_.slot(slot).nodeId();
 return apply([&](domain::Patch&p){std::erase_if(p.edges,[&](auto&x){return x.source==id||x.destination==id;});std::erase_if(p.matrix,[&](auto&x){return x.destinationNode==id;});std::erase_if(p.nodes,[&](auto&n){return n.id==id;});},"Slot deactivated");
}
bool IupacSynthEditor::connect(std::string s,std::string d,double g){return apply([&](domain::Patch&p){p.edges.push_back({std::move(s),std::move(d),g});},"Cable connected");}
bool IupacSynthEditor::disconnect(std::string s,std::string d){return apply([&](domain::Patch&p){std::erase_if(p.edges,[&](auto&e){return e.source==s&&e.destination==d;});},"Cable removed");}
bool IupacSynthEditor::setEdgeGain(std::string s,std::string d,double g){return apply([&](domain::Patch&p){for(auto&e:p.edges)if(e.source==s&&e.destination==d)e.gain=g;},"Cable gain updated");}
bool IupacSynthEditor::addLane()
{
 const auto destinations=ui::laneDestinations(field_.patch());if(destinations.empty()){showResult("activate a modulatable slot first",{});return false;}
 return apply([&](domain::Patch&p){p.matrix.push_back({uniqueRouteId(p),true,domain::ModulationSource::e1,destinations[0].nodeId,destinations[0].parameter,0.0});},"Lane added");
}
bool IupacSynthEditor::setLane(std::size_t i,domain::MatrixRow r){return apply([&](domain::Patch&p){if(i<p.matrix.size()){r.id=p.matrix[i].id;p.matrix[i]=std::move(r);}},"Lane updated");}
bool IupacSynthEditor::removeLane(std::size_t i){return apply([&](domain::Patch&p){if(i<p.matrix.size())p.matrix.erase(p.matrix.begin()+(long)i);},"Lane removed");}
bool IupacSynthEditor::setParameter(std::string_view node,std::string_view parameter,std::size_t index,double v)
{
 return apply([&](domain::Patch&p){for(auto&n:p.nodes){if(n.id!=node)continue;for(auto&x:n.parameters)if(x.id==parameter&&index<x.values.size())x.values[index]=v;
  if(n.type==domain::ModuleType::harmonic&&(parameter=="tilt"||parameter=="inharmonicity")){double tilt=0,inharmonicity=0;for(const auto&x:n.parameters){if(x.id=="tilt")tilt=x.values[0];else if(x.id=="inharmonicity")inharmonicity=x.values[0];}(void)domain::applyHarmonicSpectrum(n,tilt,inharmonicity);}}},"Parameter updated");
}
bool IupacSynthEditor::setParameterArray(std::string_view node,std::string_view parameter,std::vector<double>v){return apply([&](domain::Patch&p){for(auto&n:p.nodes)if(n.id==node)for(auto&x:n.parameters)if(x.id==parameter&&x.values.size()==v.size())x.values=v;},"Array updated");}
bool IupacSynthEditor::setEnvelope(std::size_t i,domain::Envelope e){return apply([&](domain::Patch&p){if(i<3)p.envelopes[i]=e;},"Envelope updated");}
bool IupacSynthEditor::setLfo(std::size_t i,domain::Lfo l){return apply([&](domain::Patch&p){if(i<2)p.lfos[i]=l;},"LFO updated");}
bool IupacSynthEditor::setMacro(std::size_t i,domain::Macro m){return apply([&](domain::Patch&p){if(i<4)p.macros[i]=std::move(m);},"Macro updated");}
void IupacSynthEditor::syncModulators(const domain::Patch&p)
{
 for(std::size_t i=0;i<3;++i)envelopes_[i].setEnvelope(p.envelopes[i],false);for(std::size_t i=0;i<2;++i){lfoRates_[i]->setValue(p.lfos[i].rate,false);lfoPreviews_[i].setLfo(p.lfos[i]);lfoWaveforms_[i]->setIndex((int)p.lfos[i].waveform,false);}
 for(std::size_t i=0;i<4;++i){macroLabels_[i]->text=p.macros[i].label.empty()?"MACRO "+juce::String((int)i+1):juce::String(p.macros[i].label);macroLabels_[i]->defaultValue=p.macros[i].defaultValue;macroLabels_[i]->repaint();macroKnobs_[i]->setCaption({});}
}
void IupacSynthEditor::refresh(){const auto s=owner_.snapshot();field_.setPatch(s.editedPatch);lanes_.setPatch(s.editedPatch);syncModulators(s.editedPatch);shownGeneration_=owner_.publicationStatus().accepted;}
void IupacSynthEditor::refreshPresets(){auto previous=presetList_.getText();presets_=owner_.presetFiles(presetDirectory());presetList_.clear(juce::dontSendNotification);int i=1;for(auto&p:presets_)presetList_.addItem(p.stem().string(),i++);presetList_.setText(previous,juce::dontSendNotification);}
void IupacSynthEditor::timerCallback()
{
 const auto s=owner_.publicationStatus();if(s.accepted!=shownGeneration_)refresh();
 meter_->peakDb=juce::Decibels::gainToDecibels(owner_.outputPeak(),-100.f);meter_->text="out "+juce::String(meter_->peakDb,1)+" dBFS · voices "+juce::String((int)owner_.activeVoiceCount())+" · gen "+juce::String((juce::int64)s.audible)+(s.transitioning?" transitioning":" audible");meter_->repaint();
 field_.setEffective(owner_.effectiveValues());
#if IUPAC_ENABLE_CHEMISTRY
 auto chemistry=owner_.chemistryStatus();if((chemistry.generation!=shownChemistryGeneration_&&chemistry.generation>0)||(!chemistry.busy&&status_.startsWith("Chemistry"))){shownChemistryGeneration_=chemistry.generation;status_="Chemistry: "+juce::String(chemistry.text);statusError_=false;repaint(0,24,getWidth(),24);}
#endif
}
#if IUPAC_ENABLE_CHEMISTRY
void IupacSynthEditor::openChemistry()
{
 if(chemistryDialog_!=nullptr){chemistryDialog_->toFront(true);return;}auto popup=std::make_unique<ChemistryPopup>(owner_);popup->setLookAndFeel(&laf_);popup->onResult=[this](const std::string&e,juce::String ok){showResult(e,std::move(ok));refresh();};
 auto*content=popup.get();auto*dialog=new ChemistryDialog(keyboard_);dialog->setLookAndFeel(&laf_);dialog->setContentOwned(popup.release(),true);dialog->centreAroundComponent(this,dialog->getWidth(),dialog->getHeight());chemistryDialog_=dialog;
 juce::Component::SafePointer<juce::DialogWindow>safe(dialog);content->onClose=[safe]{if(safe!=nullptr)safe->setVisible(false);};// hiding a modal dialog dismisses and auto-deletes it
 dialog->setVisible(true);dialog->enterModalState(true,nullptr,true);
}
#endif
