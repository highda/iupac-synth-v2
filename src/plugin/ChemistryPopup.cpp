#if IUPAC_ENABLE_CHEMISTRY
#include "ChemistryPopup.hpp"
#include "PluginProcessor.hpp"
using namespace iupac;
ChemistryPopup::ChemistryPopup(IupacSynthProcessor&o):owner_(o)
{
 setName("Chemistry popup");for(auto*c:std::initializer_list<juce::Component*>{&mode_,&input_,&apply_,&reapply_,&cancel_,&status_,&inspector_,&trace_,&query_,&search_,&cached_,&clearCache_,&results_,&open_,&metadata_,&closeButton_})addAndMakeVisible(c);
 input_.setTextToShowWhenEmpty("offline molecular name or SMILES",ui::ink.withAlpha(0.5f));input_.setName("Molecular input");input_.setFont(ui::labelFont(12.0f));query_.setTextToShowWhenEmpty("browse offline common names and synonyms",ui::ink.withAlpha(0.5f));query_.setName("Offline discovery query");query_.setFont(ui::labelFont(12.0f));
 trace_.setMultiLine(true);trace_.setReadOnly(true);trace_.setVisible(false);trace_.setName("Analysis SonicIntent and mapping trace");trace_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),10.0f,juce::Font::plain)));inspector_.setClickingTogglesState(true);inspector_.setName("Analysis / SonicIntent / mapping trace");
 status_.setName("Chemistry status");status_.setJustificationType(juce::Justification::centredLeft);metadata_.setName("Selected record structure and provenance");metadata_.setJustificationType(juce::Justification::centredLeft);metadata_.setMinimumHorizontalScale(0.5f);results_.setName("Bounded offline discovery candidates or cached results");results_.setTextWhenNothingSelected("candidates");
 // The popup no longer closes on Apply and waits for nothing on the message thread (#97):
 // the request runs on the coordinator's worker, the 10 Hz timer below shows the stage it
 // reports, and the dialog closes only once that request has actually produced a sound.
 apply_.onClick=[this]{if(input_.getText().trim().isEmpty()){status_.setText("enter a name or SMILES",juce::dontSendNotification);return;}awaitedGeneration_=owner_.applyChemistry(mode_.index()==0?chemistry::InputMode::name:chemistry::InputMode::smiles,input_.getText().toStdString());showPending(true);timerCallback();};
 reapply_.onClick=[this]{const auto e=owner_.reapplyChemistry();if(!e.empty()){if(onResult)onResult(e,{});status_.setText(e,juce::dontSendNotification);return;}awaitedGeneration_=owner_.chemistryStatus().generation;showPending(true);timerCallback();};
 cancel_.onClick=[this]{owner_.cancelChemistry();owner_.cancelDiscovery();showPending(false);};inspector_.onClick=[this]{trace_.setVisible(inspector_.getToggleState());if(auto*w=findParentComponentOfClass<juce::DialogWindow>())w->setSize(getWidth(),trace_.isVisible()?560:330);resized();};
 search_.onClick=[this]{owner_.searchDiscovery(query_.getText().toStdString(),true);};cached_.onClick=[this]{owner_.inspectGeneratedCache();};clearCache_.onClick=[this]{const auto e=owner_.clearGeneratedCache();status_.setText(e.empty()?"Generated cache cleared; presets untouched":e,juce::dontSendNotification);};
 results_.onChange=[this]{auto i=results_.getSelectedItemIndex();if(i>=0&&i<candidates_.size()){auto r=candidates_[i];metadata_.setText(r.getProperty("recordId",{}).toString()+r.getProperty("key",{}).toString()+" | "+r.getProperty("canonicalIsomericSmiles",{}).toString()+r.getProperty("identity",{}).toString()+" | "+r.getProperty("validationStatus",{}).toString()+" | "+r.getProperty("revisionUrl",{}).toString(),juce::dontSendNotification);}};
 // A cached result is a file and loads at once; a validated structure starts the same
 // background generation Apply does, so it gets the same pending state rather than a
 // closed popup and a silent wait (#97).
 open_.onClick=[this]{auto i=results_.getSelectedItemIndex();if(i<0||i>=candidates_.size()){status_.setText("select an explicit discovery candidate or cached result",juce::dontSendNotification);return;}const auto before=owner_.chemistryStatus().generation;const auto e=owner_.applyDiscovery(candidates_[i]);if(!e.empty()){if(onResult)onResult(e,{});status_.setText(e,juce::dontSendNotification);return;}const auto after=owner_.chemistryStatus().generation;if(after!=before){awaitedGeneration_=after;showPending(true);timerCallback();return;}if(onResult)onResult({},"Selected sound loaded");close();};
 closeButton_.onClick=[this]{close();};query_.onReturnKey=[this]{search_.triggerClick();};input_.onReturnKey=[this]{apply_.triggerClick();};
 setSize(620,330);timerCallback();startTimerHz(10);
}
ChemistryPopup::~ChemistryPopup(){stopTimer();}
void ChemistryPopup::close(){if(onClose)onClose();}
void ChemistryPopup::showPending(bool pending){awaiting_=pending;if(!pending)awaitedGeneration_=0;apply_.setEnabled(!pending);reapply_.setEnabled(!pending);open_.setEnabled(!pending);}
void ChemistryPopup::paint(juce::Graphics&g){g.fillAll(ui::ground);g.setColour(ui::ink);ui::drawCaption(g,"molecule",juce::Rectangle<int>(12,8,200,14),juce::Justification::centredLeft,9.0f);ui::drawCaption(g,"offline browser",browserTitle_,juce::Justification::centredLeft,9.0f);g.drawLine(12.0f,(float)browserTitle_.getY()-4.5f,(float)getWidth()-12.0f,(float)browserTitle_.getY()-4.5f,ui::hairline);}
void ChemistryPopup::resized()
{
 auto r=getLocalBounds().reduced(12);r.removeFromTop(14);auto line=r.removeFromTop(26);mode_.setBounds(line.removeFromLeft(110).reduced(0,2));line.removeFromLeft(6);cancel_.setBounds(line.removeFromRight(70).reduced(0,2));line.removeFromRight(4);reapply_.setBounds(line.removeFromRight(80).reduced(0,2));line.removeFromRight(4);apply_.setBounds(line.removeFromRight(70).reduced(0,2));line.removeFromRight(6);input_.setBounds(line);
 r.removeFromTop(6);line=r.removeFromTop(24);inspector_.setBounds(line.removeFromRight(100).reduced(0,2));status_.setBounds(line);r.removeFromTop(12);browserTitle_=r.removeFromTop(14);r.removeFromTop(4);
 line=r.removeFromTop(26);query_.setBounds(line.removeFromLeft(juce::jmax(160,line.getWidth()-320)));line.removeFromLeft(6);search_.setBounds(line.removeFromLeft(70).reduced(0,2));line.removeFromLeft(4);cached_.setBounds(line.removeFromLeft(70).reduced(0,2));line.removeFromLeft(4);clearCache_.setBounds(line.removeFromLeft(90).reduced(0,2));
 r.removeFromTop(6);line=r.removeFromTop(26);results_.setBounds(line.removeFromLeft(juce::jmax(160,line.getWidth()-140)));line.removeFromLeft(6);open_.setBounds(line.reduced(0,2));r.removeFromTop(6);metadata_.setBounds(r.removeFromTop(22));r.removeFromTop(6);
 auto bottom=r.removeFromBottom(26);closeButton_.setBounds(bottom.removeFromRight(70).reduced(0,2));r.removeFromBottom(6);trace_.setBounds(r);
}
void ChemistryPopup::timerCallback()
{
 auto chemistry=owner_.chemistryStatus();if(chemistry.generation!=shownChemistryGeneration_||chemistry.busy||status_.getText().isEmpty()){shownChemistryGeneration_=chemistry.generation;status_.setText(chemistry.generation==0&&!chemistry.busy?juce::String("Ready"):juce::String(chemistry.text),juce::dontSendNotification);if(chemistry.trace.isNotEmpty())trace_.setText(chemistry.trace,false);}
 // The request this popup started has finished: report it and leave, or stay open
 // with the diagnostic so the input can be corrected without retyping it.
 if(awaiting_&&!chemistry.busy&&chemistry.generation==awaitedGeneration_){const auto failed=chemistry.failed;showPending(false);if(failed){status_.setText(chemistry.text,juce::dontSendNotification);return;}if(onResult)onResult({},juce::String(chemistry.text));close();return;}
 auto discovery=owner_.discoveryStatus();if(discovery.generation!=shownDiscoveryGeneration_){status_.setText("Discovery: "+juce::String(discovery.text),juce::dontSendNotification);if(!discovery.busy){shownDiscoveryGeneration_=discovery.generation;candidates_.clear();results_.clear();auto items=discovery.result.getProperty("candidates",{});if(!items.isArray())items=discovery.result;if(auto*a=items.getArray()){int id=1;for(const auto&x:*a){candidates_.add(x.clone());auto label=x.getProperty("displayName",{}).toString();if(label.isEmpty())label=x.getProperty("identity",{}).toString()+" [mapper "+x.getProperty("versions",{}).getProperty("mapper",{}).toString()+"]";results_.addItem(label,id++);}}}}
}
#endif
