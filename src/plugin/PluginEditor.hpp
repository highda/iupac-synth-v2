#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <filesystem>
#include <memory>
class IupacSynthProcessor;
class IupacSynthEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
 explicit IupacSynthEditor(IupacSynthProcessor&);~IupacSynthEditor()override;void paint(juce::Graphics&)override;void resized()override;
 bool addModule(std::string_view);bool removeSelectedModule();bool addEdge(std::string,std::string,double);bool removeSelectedEdge();bool addMatrixRow();bool updateSelectedMatrixRow();bool setSelectedParameter(std::string_view,std::size_t,double);void refresh();[[nodiscard]]juce::String statusText()const{return status_.getText();}
private:
 void timerCallback()override;void refreshModules();void refreshDetail();void refreshGraph();void refreshMatrix();void refreshPresets();void showResult(const std::string&,juce::String);std::filesystem::path presetDirectory()const;static juce::String sourceName(int);
 IupacSynthProcessor&owner_;juce::TextButton newButton_{"New"},loadButton_{"Load"},saveButton_{"Save"},savePresetButton_{"Save preset"},resetPatchButton_{"Reset edits"},resetControlsButton_{"Reset controls"};juce::ComboBox presetList_;juce::Label status_,meter_,graph_;juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};juce::Component modulesTab_,matrixTab_;juce::ListBox moduleList_,edgeList_,matrixList_;juce::ComboBox addType_,edgeSource_,edgeDestination_,matrixSource_,matrixDestination_;juce::TextButton addModuleButton_{"Add module"},removeModuleButton_{"Remove selected"},addEdgeButton_{"Add edge"},removeEdgeButton_{"Remove selected edge"},addRowButton_{"Add route"},updateRowButton_{"Update selected route"},removeRowButton_{"Remove selected route"};juce::Slider edgeGain_,matrixDepth_;juce::ToggleButton matrixEnabled_{"Enabled"};juce::Viewport detailViewport_;juce::Component detailContent_;juce::OwnedArray<juce::Label>detailLabels_;juce::OwnedArray<juce::Slider>detailSliders_;juce::OwnedArray<juce::ComboBox>detailChoices_;juce::OwnedArray<juce::TextEditor>arrayEditors_;std::array<juce::Label,4>macroLabels_;std::array<juce::Slider,8>hostSliders_;std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,8>hostAttachments_;juce::MidiKeyboardComponent keyboard_;std::unique_ptr<juce::FileChooser>chooser_;std::vector<std::filesystem::path>presets_;int selectedModule_{},selectedEdge_{},selectedRow_{};struct ModulesModel;struct EdgesModel;struct MatrixModel;std::unique_ptr<ModulesModel>modulesModel_;std::unique_ptr<EdgesModel>edgesModel_;std::unique_ptr<MatrixModel>matrixModel_;std::array<juce::Slider,14>modulatorSliders_;std::array<juce::ComboBox,2>lfoWaveforms_;std::array<juce::TextEditor,4>macroEditors_;
};
