#include "PluginEditor.hpp"
#include "PluginProcessor.hpp"

IupacSynthEditor::IupacSynthEditor(IupacSynthProcessor& owner)
    : AudioProcessorEditor(owner)
{
    status_.setText("Product foundation — synth features arrive in subsequent leaves", juce::dontSendNotification);
    status_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(status_);
    setResizable(true, true);
    setResizeLimits(1000, 700, 1800, 1200);
    setSize(1200, 800);
}

void IupacSynthEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff15171c));
}

void IupacSynthEditor::resized()
{
    status_.setBounds(getLocalBounds().reduced(32));
}
