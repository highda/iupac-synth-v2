#pragma once
// Chemistry popup (extension build only): the modal dialog carrying Name/SMILES input with status/cancel, the offline
// browser with provenance and the collapsible analysis/sonic/rule inspector. Apply/Reapply act on the document and
// close; typing here never reaches the audition keyboard. The synth-only build compiles none of this.
#if IUPAC_ENABLE_CHEMISTRY
#include "EditorControls.hpp"
class IupacSynthProcessor;
class ChemistryPopup final:public juce::Component,private juce::Timer
{
public:
 explicit ChemistryPopup(IupacSynthProcessor&);~ChemistryPopup()override;void resized()override;void paint(juce::Graphics&)override;
 std::function<void()>onClose;std::function<void(const std::string&,juce::String)>onResult;
private:
 void timerCallback()override;void close();
 // Reflect a request that is still running: the popup stays open and interactive,
 // Apply is unavailable while its own request is in flight, and Cancel is the way out (#97).
 void showPending(bool);
 IupacSynthProcessor&owner_;iupac::ui::SegmentToggle mode_{{"name","smiles"}};juce::TextEditor input_,trace_,query_;juce::TextButton apply_{"apply"},reapply_{"reapply"},cancel_{"cancel"},inspector_{"inspector"},search_{"search"},cached_{"cached"},clearCache_{"clear cache"},open_{"apply / reopen"},closeButton_{"close"};juce::Label status_,metadata_;juce::ComboBox results_;juce::Array<juce::var>candidates_;std::uint64_t shownChemistryGeneration_{},shownDiscoveryGeneration_{},awaitedGeneration_{};bool awaiting_{};juce::Rectangle<int>browserTitle_;
};
// Modal dialog that still lets the audition keyboard receive mouse events while the popup is open.
// #99: it must own a desktop peer. JUCE dismisses a modal component through `ModalItem::componentVisibilityChanged`,
// which cancels only when `isShowing()` goes false — and a component with neither a peer nor a parent is never
// showing, so a peerless modal dialog is invisible yet blocks every editor event for the lifetime of the editor.
// The peer is created here rather than through the constructor's `addToDesktop` argument so the style flags are
// derived after the non-native title bar and fixed size are set; always-on-top keeps it above the host's own editor
// window in VST3/AU, including out-of-process `AUHostingServiceXPC` views, where the plugin cannot reparent it.
class ChemistryDialog final:public juce::DialogWindow
{
public:
 explicit ChemistryDialog(juce::Component&keyboard):juce::DialogWindow("CHEMISTRY",iupac::ui::ground,true,false),keyboard_(keyboard){setUsingNativeTitleBar(false);setResizable(false,false);addToDesktop();setAlwaysOnTop(true);}
 void closeButtonPressed()override{setVisible(false);}bool escapeKeyPressed()override{setVisible(false);return true;}
 bool canModalEventBeSentToComponent(const juce::Component*target)override{return target!=nullptr&&(target==&keyboard_||keyboard_.isParentOf(target));}
private:
 juce::Component&keyboard_;
};
#endif
