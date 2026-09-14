#include "PluginProcessor.hpp"
#include "PluginEditor.hpp"

IupacSynthProcessor::IupacSynthProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void IupacSynthProcessor::prepareToPlay(const double sampleRate, const int maximumExpectedSamplesPerBlock)
{
    engine_.prepare(sampleRate, static_cast<std::size_t>(maximumExpectedSamplesPerBlock));
}

bool IupacSynthProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet().isDisabled();
}

void IupacSynthProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

void IupacSynthProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

juce::AudioProcessorEditor* IupacSynthProcessor::createEditor()
{
    return new IupacSynthEditor(*this);
}

void IupacSynthProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    static constexpr char state[] = "{\"stateVersion\":1}";
    destination.append(state, sizeof(state) - 1);
}

void IupacSynthProcessor::setStateInformation(const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IupacSynthProcessor();
}
