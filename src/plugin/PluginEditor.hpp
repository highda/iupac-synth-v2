#pragma once
// Single-screen graph editor (ARCHITECTURE "State, editing and UI", D6). Top strip → module field → modulator strip →
// lane matrix → audition keyboard, all on one always-visible screen; every action is a document transaction on the
// processor and the screen is re-synced from the resulting snapshot. Programmatic hooks mirror the gestures for tests.
#include "EditorControls.hpp"
#include "EditorField.hpp"
#include "EditorLanes.hpp"
#if IUPAC_ENABLE_CHEMISTRY
#include "ChemistryPopup.hpp"
#endif
#include <juce_audio_utils/juce_audio_utils.h>
#include <filesystem>
#include <memory>
class IupacSynthProcessor;
class IupacSynthEditor final:public juce::AudioProcessorEditor,public iupac::ui::EditorShell,private juce::Timer
{
public:
 explicit IupacSynthEditor(IupacSynthProcessor&);~IupacSynthEditor()override;void paint(juce::Graphics&)override;void resized()override;
 // Programmatic hooks (same transactions as the gestures).
 bool activateSlot(std::string_view type,std::size_t slot);bool deactivateSlot(std::size_t slot);bool connect(std::string source,std::string destination,double gain,iupac::domain::AudioPort port=iupac::domain::AudioPort::in);bool disconnect(std::string source,std::string destination,iupac::domain::AudioPort port=iupac::domain::AudioPort::in);bool setEdgeGain(std::string source,std::string destination,double gain,iupac::domain::AudioPort port=iupac::domain::AudioPort::in);
 bool addLane();bool setLane(std::size_t row,iupac::domain::MatrixRow);bool removeLane(std::size_t row);bool setParameter(std::string_view nodeId,std::string_view parameter,std::size_t index,double value);bool setParameterArray(std::string_view nodeId,std::string_view parameter,std::vector<double>);bool setEnvelope(std::size_t,iupac::domain::Envelope);bool setLfo(std::size_t,iupac::domain::Lfo);bool setMacro(std::size_t,iupac::domain::Macro);
 void refresh();[[nodiscard]]juce::String statusText()const{return status_;}
 [[nodiscard]]iupac::ui::ModuleField&field()noexcept{return field_;}[[nodiscard]]iupac::ui::LaneMatrix&lanes()noexcept{return lanes_;}[[nodiscard]]iupac::ui::PrecisionEntry&precisionEntry()noexcept{return entry_;}[[nodiscard]]juce::MidiKeyboardComponent&keyboard()noexcept{return keyboard_;}[[nodiscard]]iupac::ui::AdsrCurve&envelopeCurve(std::size_t i)noexcept{return envelopes_[i];}[[nodiscard]]iupac::ui::Knob&macroKnob(std::size_t i)noexcept{return*macroKnobs_[i];}
 [[nodiscard]]std::size_t textFieldCount()const;// visible text-entry components on the default screen (0 by contract)
 // EditorShell
 void showValue(juce::Component&,juce::String)override;void hideValue()override;void openEntry(juce::Component&,juce::Rectangle<int>,juce::String,std::function<void(juce::String)>)override;[[nodiscard]]bool entryOpen()const override{return entry_.isVisible();}
private:
 struct MacroLabel;struct MeterView;struct ValueBubble;
 // Live text scale (#89): published on `laf_` by resized() and read back by every child through its look-and-feel.
 [[nodiscard]]float scale()const noexcept{return laf_.scale();}
 [[nodiscard]]int stripHeight()const noexcept{return juce::roundToInt(78.0f*scale());}
 void timerCallback()override;bool apply(const std::function<void(iupac::domain::Patch&)>&,juce::String ok);void showResult(const std::string&,juce::String ok);void refreshPresets();void closeEntry();std::filesystem::path presetDirectory()const;void syncModulators(const iupac::domain::Patch&);
 iupac::ui::EditorLookAndFeel laf_;IupacSynthProcessor&owner_;
 juce::TextButton newButton_{"new"},loadButton_{"load"},saveButton_{"save"},savePresetButton_{"preset"},resetPatchButton_{"reset edits"},resetControlsButton_{"reset controls"};juce::ComboBox presetList_;juce::String status_;bool statusError_{};std::unique_ptr<MeterView>meter_;
 std::array<std::unique_ptr<iupac::ui::Knob>,4>macroKnobs_;std::array<std::unique_ptr<MacroLabel>,4>macroLabels_;std::unique_ptr<iupac::ui::Knob>outputGain_,masterTune_;std::unique_ptr<iupac::ui::Fader>width_;juce::TextButton bypass_{"bypass"};std::array<std::unique_ptr<juce::ParameterAttachment>,8>attachments_;
 iupac::ui::ModuleField field_;std::array<iupac::ui::AdsrCurve,iupac::domain::envelopeCount>envelopes_;std::array<std::unique_ptr<iupac::ui::Knob>,2>lfoRates_,lfoFades_;std::array<iupac::ui::LfoPreview,2>lfoPreviews_;std::array<std::unique_ptr<iupac::ui::SegmentToggle>,2>lfoWaveforms_,lfoSyncModes_,lfoDivisions_;iupac::ui::LaneMatrix lanes_;juce::MidiKeyboardComponent keyboard_;
 std::unique_ptr<ValueBubble>bubble_;iupac::ui::PrecisionEntry entry_;std::function<void(juce::String)>entryCommit_;std::unique_ptr<juce::FileChooser>chooser_;std::vector<std::filesystem::path>presets_;std::uint64_t shownGeneration_{};
#if IUPAC_ENABLE_CHEMISTRY
 juce::TextButton chemistryButton_{"chemistry"};std::unique_ptr<ChemistryOverlay>chemistryOverlay_;std::uint64_t shownChemistryGeneration_{};void openChemistry();void layoutChemistryOverlay();
#endif
};
