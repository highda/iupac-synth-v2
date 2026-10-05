#pragma once
#include "iupac/engine/PatchCoordinator.hpp"
#if IUPAC_ENABLE_CHEMISTRY
#include "iupac/chemistry/Extension.hpp"
#endif
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
class IupacSynthProcessor final : public juce::AudioProcessor
{
public:
 IupacSynthProcessor(); void prepareToPlay(double,int)override;void releaseResources()override{};bool isBusesLayoutSupported(const BusesLayout&)const override;void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&)override;
 juce::AudioProcessorEditor* createEditor()override;bool hasEditor()const override{return true;}const juce::String getName()const override{return JucePlugin_Name;}bool acceptsMidi()const override{return true;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}double getTailLengthSeconds()const override{return 6.1;}int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}const juce::String getProgramName(int)override{return{};}void changeProgramName(int,const juce::String&)override{}void getStateInformation(juce::MemoryBlock&)override;void setStateInformation(const void*,int)override;
 [[nodiscard]]juce::AudioProcessorValueTreeState&parameters()noexcept{return parameters_;}[[nodiscard]]iupac::domain::State snapshot()const;[[nodiscard]]std::string loadState(iupac::domain::State);[[nodiscard]]std::string newDocument();[[nodiscard]]std::string resetPatchEdits();void resetControls();
 [[nodiscard]]std::string editPatch(const std::function<void(iupac::domain::Patch&)>&);[[nodiscard]]std::string saveStateFile(const std::filesystem::path&)const;[[nodiscard]]std::string loadStateFile(const std::filesystem::path&);[[nodiscard]]std::vector<std::filesystem::path> presetFiles(const std::filesystem::path&)const;
 [[nodiscard]]juce::MidiKeyboardState& keyboardState()noexcept{return keyboardState_;}[[nodiscard]]float outputPeak()const noexcept{return outputPeak_.load(std::memory_order_relaxed);}[[nodiscard]]std::size_t activeVoiceCount()const noexcept{return coordinator_.activeVoiceCount();}[[nodiscard]]iupac::engine::PatchCoordinator::Status publicationStatus()const noexcept{return coordinator_.status();}[[nodiscard]]iupac::engine::EffectiveValues effectiveValues()const noexcept{return coordinator_.effectiveValues();}
#if IUPAC_ENABLE_CHEMISTRY
 struct ChemistryStatus{bool busy{};std::uint64_t generation{};std::string text{"Ready"};juce::String trace;iupac::chemistry::Stage stage{iupac::chemistry::Stage::idle};bool failed{};};
 // `append` marks a keyset continuation page (#138): the browser adds it below the rows it already shows.
 struct DiscoveryStatus{bool busy{};std::uint64_t generation{};std::string text{"Ready"};juce::var result;bool append{};};
 std::uint64_t applyChemistry(iupac::chemistry::InputMode,std::string);std::string reapplyChemistry();void cancelChemistry();void prewarmChemistry();[[nodiscard]]ChemistryStatus chemistryStatus()const;
 std::uint64_t searchDiscovery(std::string,bool prefix=true,juce::var after={});std::uint64_t inspectGeneratedCache();std::string clearGeneratedCache();std::string applyDiscovery(const juce::var&);void cancelDiscovery();[[nodiscard]]DiscoveryStatus discoveryStatus()const;
#endif
private:
 static juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout();static iupac::domain::Patch defaultPatch();iupac::domain::HostControls readControls()const noexcept;void writeControls(const iupac::domain::HostControls&);void render(juce::AudioBuffer<float>&,const juce::MidiBuffer&)noexcept;
 void publishDocument(const iupac::engine::CompiledPatch&);// enqueue, or keep retrying from the message thread while the audio FIFO is full
 // The command FIFO holds a bounded number of publications between audio callbacks; a publication that finds it full stays
 // producer-pending and is retried here so the newest edit is never dropped while the host is not rendering.
 struct PublishRetry final:juce::Timer{explicit PublishRetry(IupacSynthProcessor&o):owner(o){}~PublishRetry()override{stopTimer();}void timerCallback()override;IupacSynthProcessor&owner;};
 mutable std::mutex documentMutex_;iupac::domain::State document_;iupac::engine::PatchCoordinator coordinator_;PublishRetry publishRetry_{*this};juce::AudioProcessorValueTreeState parameters_;std::array<std::atomic<float>*,8> parameterValues_{};juce::MidiKeyboardState keyboardState_;std::atomic<float> outputPeak_{0};
#if IUPAC_ENABLE_CHEMISTRY
 mutable std::mutex chemistryMutex_;ChemistryStatus chemistryStatus_;
 mutable std::mutex discoveryMutex_;DiscoveryStatus discoveryStatus_;std::atomic<std::uint64_t> discoveryGeneration_{0};std::jthread discoveryWorker_;juce::var pendingDiscoveryRecord_;std::uint64_t pendingDiscoveryGeneration_{};std::unique_ptr<iupac::chemistry::ExtensionCoordinator> chemistry_;
#endif
};
