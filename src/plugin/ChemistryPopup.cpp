#if IUPAC_ENABLE_CHEMISTRY
#include "ChemistryPopup.hpp"
#include "PluginProcessor.hpp"
using namespace iupac;
ChemistryPopup::ChemistryPopup(IupacSynthProcessor&o):owner_(o)
{
 setName("Chemistry popup");for(auto*c:std::initializer_list<juce::Component*>{&mode_,&input_,&apply_,&reapply_,&cancel_,&status_,&inspector_,&trace_,&query_,&search_,&cached_,&clearCache_,&results_,&open_,&metadata_,&closeButton_})addAndMakeVisible(c);
 input_.setTextToShowWhenEmpty("offline molecular name or SMILES",ui::ink.withAlpha(0.5f));input_.setName("Molecular input");input_.setFont(ui::labelFont(12.0f));query_.setTextToShowWhenEmpty("browse offline common names and synonyms",ui::ink.withAlpha(0.5f));query_.setName("Offline discovery query");query_.setFont(ui::labelFont(12.0f));
 trace_.setMultiLine(true);trace_.setReadOnly(true);trace_.setVisible(false);trace_.setName("Analysis SonicIntent and mapping trace");trace_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),10.0f,juce::Font::plain)));inspector_.setClickingTogglesState(true);inspector_.setName("Analysis / SonicIntent / mapping trace");
 status_.setName("Chemistry status");status_.setJustificationType(juce::Justification::centredLeft);metadata_.setName("Selected record structure and provenance");metadata_.setJustificationType(juce::Justification::centredLeft);metadata_.setMinimumHorizontalScale(0.5f);results_.setName("Bounded offline discovery candidates or cached results");results_.setModel(this);results_.setRowHeight(18);results_.setOutlineThickness((int)ui::hairline);results_.setMultipleSelectionEnabled(false);results_.setWantsKeyboardFocus(true);
 // The rows paint their own opaque background, so the box itself stays clear and the popup can draw the
 // "candidates" hint underneath while the list is empty.
 results_.setColour(juce::ListBox::backgroundColourId,juce::Colours::transparentBlack);
 // The popup no longer closes on Apply and waits for nothing on the message thread (#97):
 // the request runs on the coordinator's worker, the 10 Hz timer below shows the stage it
 // reports, and the dialog closes only once that request has actually produced a sound.
 apply_.onClick=[this]{if(input_.getText().trim().isEmpty()){status_.setText("enter a name or SMILES",juce::dontSendNotification);return;}awaitedGeneration_=owner_.applyChemistry(mode_.index()==0?chemistry::InputMode::name:chemistry::InputMode::smiles,input_.getText().toStdString());showPending(true);timerCallback();};
 reapply_.onClick=[this]{const auto e=owner_.reapplyChemistry();if(!e.empty()){if(onResult)onResult(e,{});status_.setText(e,juce::dontSendNotification);return;}awaitedGeneration_=owner_.chemistryStatus().generation;showPending(true);timerCallback();};
 cancel_.onClick=[this]{owner_.cancelChemistry();owner_.cancelDiscovery();showPending(false);};inspector_.onClick=[this]{trace_.setVisible(inspector_.getToggleState());if(onPreferredSize)onPreferredSize(getWidth(),trace_.isVisible()?630:400);resized();};
 search_.onClick=[this]{pagedQuery_=query_.getText().toStdString();nextPage_=juce::var();owner_.searchDiscovery(pagedQuery_,true);};cached_.onClick=[this]{owner_.inspectGeneratedCache();};clearCache_.onClick=[this]{const auto e=owner_.clearGeneratedCache();status_.setText(e.empty()?"Generated cache cleared; presets untouched":e,juce::dontSendNotification);};
 // A cached result is a file and loads at once; a validated structure starts the same
 // background generation Apply does, so it gets the same pending state rather than a
 // closed popup and a silent wait (#97).
 open_.onClick=[this]{const auto i=selectedCandidate();if(i<0){status_.setText("select an explicit discovery candidate or cached result",juce::dontSendNotification);return;}const auto before=owner_.chemistryStatus().generation;const auto e=owner_.applyDiscovery(candidates_[i]);if(!e.empty()){if(onResult)onResult(e,{});status_.setText(e,juce::dontSendNotification);return;}const auto after=owner_.chemistryStatus().generation;if(after!=before){awaitedGeneration_=after;showPending(true);timerCallback();return;}if(onResult)onResult({},"Selected sound loaded");close();};
 closeButton_.onClick=[this]{close();};query_.onReturnKey=[this]{search_.triggerClick();};input_.onReturnKey=[this]{apply_.triggerClick();};
 setSize(620,400);timerCallback();startTimerHz(10);
}
ChemistryPopup::~ChemistryPopup(){stopTimer();}
void ChemistryPopup::close(){if(onClose)onClose();}
int ChemistryPopup::getNumRows(){return labels_.size();}
int ChemistryPopup::selectedCandidate()const{const auto row=results_.getSelectedRow();return row>=0&&row<candidates_.size()?row:-1;}
void ChemistryPopup::paintListBoxItem(int row,juce::Graphics&g,int width,int height,bool selected)
{
 if(row<0||row>=labels_.size())return;
 g.setColour(selected?ui::ink:ui::ground);g.fillRect(0,0,width,height);
 g.setColour(selected?ui::ground:ui::ink);g.setFont(ui::labelFont(juce::jlimit(ui::minimumTextHeight,ui::scaledText(*this,10.0f),(float)height*0.62f)));
 g.drawFittedText(labels_[row].toUpperCase(),juce::Rectangle<int>(0,0,width,height).reduced(6,0),juce::Justification::centredLeft,1,1.0f);
}
void ChemistryPopup::selectedRowsChanged(int)
{
 const auto i=selectedCandidate();if(i<0){metadata_.setText({},juce::dontSendNotification);return;}
 auto r=candidates_[i];metadata_.setText(r.getProperty("recordId",{}).toString()+r.getProperty("key",{}).toString()+" | "+r.getProperty("canonicalIsomericSmiles",{}).toString()+r.getProperty("identity",{}).toString()+" | "+r.getProperty("validationStatus",{}).toString()+" | "+r.getProperty("revisionUrl",{}).toString(),juce::dontSendNotification);
}
void ChemistryPopup::showPending(bool pending){awaiting_=pending;if(!pending)awaitedGeneration_=0;apply_.setEnabled(!pending);reapply_.setEnabled(!pending);open_.setEnabled(!pending);}
void ChemistryPopup::paint(juce::Graphics&g){g.fillAll(ui::ground);g.setColour(ui::ink);ui::drawCaption(g,"molecule",juce::Rectangle<int>(12,8,200,14),juce::Justification::centredLeft,9.0f);ui::drawCaption(g,"offline browser",browserTitle_,juce::Justification::centredLeft,9.0f);if(labels_.isEmpty())ui::drawCaption(g,"candidates",results_.getBounds().withHeight(18).reduced(8,0),juce::Justification::centredLeft,9.0f);g.drawLine(12.0f,(float)browserTitle_.getY()-4.5f,(float)getWidth()-12.0f,(float)browserTitle_.getY()-4.5f,ui::hairline);}
void ChemistryPopup::resized()
{
 auto r=getLocalBounds().reduced(12);r.removeFromTop(14);auto line=r.removeFromTop(26);mode_.setBounds(line.removeFromLeft(110).reduced(0,2));line.removeFromLeft(6);cancel_.setBounds(line.removeFromRight(70).reduced(0,2));line.removeFromRight(4);reapply_.setBounds(line.removeFromRight(80).reduced(0,2));line.removeFromRight(4);apply_.setBounds(line.removeFromRight(70).reduced(0,2));line.removeFromRight(6);input_.setBounds(line);
 r.removeFromTop(6);line=r.removeFromTop(24);inspector_.setBounds(line.removeFromRight(100).reduced(0,2));status_.setBounds(line);r.removeFromTop(12);browserTitle_=r.removeFromTop(14);r.removeFromTop(4);
 line=r.removeFromTop(26);query_.setBounds(line.removeFromLeft(juce::jmax(160,line.getWidth()-320)));line.removeFromLeft(6);search_.setBounds(line.removeFromLeft(70).reduced(0,2));line.removeFromLeft(4);cached_.setBounds(line.removeFromLeft(70).reduced(0,2));line.removeFromLeft(4);clearCache_.setBounds(line.removeFromLeft(90).reduced(0,2));
 // The candidate list needs several rows of its own height, not one combo line: BROWSE-01 pages through a bounded
 // search of up to 32 hits and every one of them has to be reachable by wheel, drag and arrow key (#103).
 // Metadata line, close row and their gaps are all that sit below the list; the rest of the popup is the list while
 // the inspector is closed, and the trace takes the growth when it opens.
 r.removeFromTop(6);const int available=juce::jmax(3*results_.getRowHeight(),r.getHeight()-72);
 auto results=r.removeFromTop(trace_.isVisible()?juce::jmin(96,available):available);auto side=results.removeFromRight(140);results.removeFromRight(6);open_.setBounds(side.removeFromTop(26).reduced(0,2));results_.setBounds(results);
 r.removeFromTop(6);metadata_.setBounds(r.removeFromTop(22));r.removeFromTop(6);
 auto bottom=r.removeFromBottom(26);closeButton_.setBounds(bottom.removeFromRight(70).reduced(0,2));r.removeFromBottom(6);trace_.setBounds(r);
}
void ChemistryPopup::timerCallback()
{
 auto chemistry=owner_.chemistryStatus();if(chemistry.generation!=shownChemistryGeneration_||chemistry.busy||status_.getText().isEmpty()){shownChemistryGeneration_=chemistry.generation;status_.setText(chemistry.generation==0&&!chemistry.busy?juce::String("Ready"):juce::String(chemistry.text),juce::dontSendNotification);if(chemistry.trace.isNotEmpty())trace_.setText(chemistry.trace,false);}
 // The request this popup started has finished: report it and leave, or stay open
 // with the diagnostic so the input can be corrected without retyping it.
 if(awaiting_&&!chemistry.busy&&chemistry.generation==awaitedGeneration_){const auto failed=chemistry.failed;showPending(false);if(failed){status_.setText(chemistry.text,juce::dontSendNotification);return;}if(onResult)onResult({},juce::String(chemistry.text));close();return;}
 auto discovery=owner_.discoveryStatus();if(discovery.generation!=shownDiscoveryGeneration_){status_.setText("Discovery: "+juce::String(discovery.text),juce::dontSendNotification);if(!discovery.busy){shownDiscoveryGeneration_=discovery.generation;if(!discovery.append){candidates_.clear();labels_.clear();}nextPage_=discovery.result.getProperty("next",{});auto items=discovery.result.getProperty("candidates",{});if(!items.isArray())items=discovery.result;if(auto*a=items.getArray())for(const auto&x:*a){candidates_.add(x.clone());auto label=x.getProperty("displayName",{}).toString();if(label.isEmpty())label=x.getProperty("identity",{}).toString()+" [mapper "+x.getProperty("versions",{}).getProperty("mapper",{}).toString()+"]";labels_.add(label);}
 if(discovery.append){results_.updateContent();}else{results_.deselectAllRows();results_.updateContent();results_.scrollToEnsureRowIsOnscreen(0);metadata_.setText({},juce::dontSendNotification);}repaint();}}
 loadMoreWhenNearEnd();
}
// The next page is requested once the last five rows come into view, by wheel, scrollbar or arrow keys alike.
void ChemistryPopup::loadMoreWhenNearEnd()
{
 if(!nextPage_.isObject()||owner_.discoveryStatus().busy||labels_.isEmpty())return;
 const auto lastVisible=results_.getRowContainingPosition(1,results_.getHeight()-2);
 if(lastVisible>=0&&lastVisible<labels_.size()-5)return;
 const auto after=nextPage_;nextPage_=juce::var();owner_.searchDiscovery(pagedQuery_,true,after);
}
#endif
