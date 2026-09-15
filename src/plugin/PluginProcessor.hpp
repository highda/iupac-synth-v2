#pragma once
#include "iupac/engine/PatchCoordinator.hpp"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <mutex>
class IupacSynthProcessor final : public juce::AudioProcessor
{
public:
 IupacSynthProcessor(); void prepareToPlay(double,int)override;void releaseResources()override{};bool isBusesLayoutSupported(const BusesLayout&)const override;void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&)override;
 juce::AudioProcessorEditor* createEditor()override;bool hasEditor()const override{return true;}const juce::String getName()const override{return JucePlugin_Name;}bool acceptsMidi()const override{return true;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}double getTailLengthSeconds()const override{return 6.1;}int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}const juce::String getProgramName(int)override{return{};}void changeProgramName(int,const juce::String&)override{}void getStateInformation(juce::MemoryBlock&)override;void setStateInformation(const void*,int)override;
 [[nodiscard]]juce::AudioProcessorValueTreeState&parameters()noexcept{return parameters_;}[[nodiscard]]iupac::domain::State snapshot()const;[[nodiscard]]std::string loadState(iupac::domain::State);[[nodiscard]]std::string newDocument();[[nodiscard]]std::string resetPatchEdits();void resetControls();
private:
 static juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout();static iupac::domain::Patch defaultPatch();iupac::domain::HostControls readControls()const noexcept;void writeControls(const iupac::domain::HostControls&);void render(juce::AudioBuffer<float>&,const juce::MidiBuffer&)noexcept;
 mutable std::mutex documentMutex_;iupac::domain::State document_;iupac::engine::PatchCoordinator coordinator_;juce::AudioProcessorValueTreeState parameters_;std::array<std::atomic<float>*,8> parameterValues_{};
};
