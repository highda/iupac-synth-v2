#pragma once
// Chemistry popup (extension build only): the modal dialog carrying Name/SMILES input with status/cancel, the offline
// browser with provenance and the collapsible analysis/sonic/rule inspector. Apply/Reapply act on the document and
// close; typing here never reaches the audition keyboard. The synth-only build compiles none of this.
#if IUPAC_ENABLE_CHEMISTRY
#include "EditorControls.hpp"
#include <memory>
class IupacSynthProcessor;
class ChemistryPopup final:public juce::Component,private juce::Timer,private juce::ListBoxModel
{
public:
 explicit ChemistryPopup(IupacSynthProcessor&);~ChemistryPopup()override;void resized()override;void paint(juce::Graphics&)override;
 std::function<void()>onClose;std::function<void(const std::string&,juce::String)>onResult;
 // The inspector grows the popup; whoever frames it (the #101 overlay) re-lays it out. Unset in bare-popup tests.
 std::function<void(int,int)>onPreferredSize;
private:
 void timerCallback()override;void close();
 // The candidate list is a juce::ListBox, not a ComboBox popup menu (#103): a bounded search returns far more
 // entries than a menu shows, and only a viewport-backed list gives the wheel, the drag and the arrow keys at once.
 int getNumRows()override;void paintListBoxItem(int,juce::Graphics&,int,int,bool)override;void selectedRowsChanged(int)override;
 // Index into `candidates_` of the row the user has explicitly selected, or -1.
 [[nodiscard]]int selectedCandidate()const;
 // Reflect a request that is still running: the popup stays open and interactive,
 // Apply is unavailable while its own request is in flight, and Cancel is the way out (#97).
 void showPending(bool);
 IupacSynthProcessor&owner_;iupac::ui::SegmentToggle mode_{{"name","smiles"}};juce::TextEditor input_,trace_,query_;juce::TextButton apply_{"apply"},reapply_{"reapply"},cancel_{"cancel"},inspector_{"inspector"},search_{"search"},cached_{"cached"},clearCache_{"clear cache"},open_{"apply / reopen"},closeButton_{"close"};juce::Label status_,metadata_;juce::ListBox results_;juce::StringArray labels_;juce::Array<juce::var>candidates_;std::uint64_t shownChemistryGeneration_{},shownDiscoveryGeneration_{},awaitedGeneration_{};bool awaiting_{};juce::Rectangle<int>browserTitle_;
};
// In-editor modal overlay (#101). The popup is a child of the editor, never a desktop window: a separate window
// floats above every application system-wide and, in the out-of-process AU, has no parent relationship to the host's
// editor window at all, so it cannot be tied to it. As an editor child it moves, minimises, hides and dies with the
// host window for free, in every format.
// #99 still applies and is still satisfied: JUCE dismisses a modal component through `ModalItem::componentVisibility-
// Changed`, which cancels only when `isShowing()` goes false. A component with neither peer nor parent is never
// showing; this one has a parent, so hiding it really does lift the modal block.
// The overlay covers the editor down to the audition keyboard only: the keyboard stays visible and clickable, which
// is what `canModalEventBeSentToComponent` already promises.
class ChemistryOverlay final:public juce::Component
{
public:
 ChemistryOverlay(juce::Component&keyboard,std::unique_ptr<ChemistryPopup>popup):keyboard_(keyboard),popup_(std::move(popup))
 {
  setName("Chemistry overlay");setWantsKeyboardFocus(true);setInterceptsMouseClicks(true,true);
  addAndMakeVisible(closeButton_);closeButton_.setName("Close chemistry");closeButton_.onClick=[this]{dismiss();};
  addAndMakeVisible(*popup_);content_={popup_->getWidth(),popup_->getHeight()};
 }
 std::function<void()>onDismiss;
 [[nodiscard]]ChemistryPopup&popup()noexcept{return*popup_;}
 // The popup asks for a taller frame when the inspector opens; re-centre it rather than resizing a window.
 void setContentSize(int w,int h){content_={w,h};resized();}
 [[nodiscard]]juce::Rectangle<int>contentBounds()const noexcept{return popup_->getBounds();}
 void dismiss(){exitModalState(0);setVisible(false);if(onDismiss)onDismiss();}
 void resized()override
 {
  auto r=getLocalBounds();
  const int w=juce::jmin(content_.getWidth(),juce::jmax(0,r.getWidth()-16));
  const int h=juce::jmin(content_.getHeight()+headerHeight,juce::jmax(0,r.getHeight()-16));
  // Header plus popup is the frame the user sees; centre that, not the popup alone.
  auto frame=juce::Rectangle<int>(w,h).withCentre(r.getCentre());popup_->setBounds(frame.withTrimmedTop(headerHeight));
  // Title left, close button right: the header is new here and must not reproduce the overlap of #102.
  closeButton_.setBounds(juce::Rectangle<int>(frame.getRight()-headerHeight,frame.getY(),headerHeight,headerHeight).reduced(3));
 }
 void paint(juce::Graphics&g)override
 {
  g.fillAll(iupac::ui::ink.withAlpha(0.55f));// dim the editor behind; the keyboard below the overlay stays bright
  auto frame=popup_->getBounds().withTop(popup_->getY()-headerHeight);
  g.setColour(iupac::ui::ground);g.fillRect(frame);
  g.setColour(iupac::ui::ink);g.drawRect(frame,(int)iupac::ui::hairline);
  g.drawLine((float)frame.getX(),(float)popup_->getY()-0.5f,(float)frame.getRight(),(float)popup_->getY()-0.5f,iupac::ui::hairline);
  iupac::ui::drawCaption(g,"CHEMISTRY",frame.withHeight(headerHeight).withTrimmedLeft(8).withTrimmedRight(headerHeight+4),juce::Justification::centredLeft,10.0f);
 }
 bool keyPressed(const juce::KeyPress&k)override{if(k==juce::KeyPress::escapeKey){dismiss();return true;}return false;}
 void inputAttemptWhenModal()override{}// a click on the dimmed backdrop is inert, not a dismissal
 bool canModalEventBeSentToComponent(const juce::Component*target)override{return target!=nullptr&&(target==&keyboard_||keyboard_.isParentOf(target));}
private:
 static constexpr int headerHeight=22;
 juce::Component&keyboard_;std::unique_ptr<ChemistryPopup>popup_;juce::TextButton closeButton_{"x"};juce::Rectangle<int>content_{620,330};
};
#endif
