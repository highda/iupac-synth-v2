#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class IupacSynthProcessor;

class IupacSynthEditor final : public juce::AudioProcessorEditor
{
public:
    explicit IupacSynthEditor(IupacSynthProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::Label status_;
};
