#pragma once

#include <JuceHeader.h>

#include <array>

#include "PluginProcessor.h"

class AuroraLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& graphics,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPosition,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;
};

class KarmaTestBedAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit KarmaTestBedAudioProcessorEditor(KarmaTestBedAudioProcessor& processorToEdit);
    ~KarmaTestBedAudioProcessorEditor() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    enum Knob
    {
        warp,
        time,
        blur,
        reflect,
        mix,
        atmosphere,
        knobCount
    };

    void configureKnob(Knob knob, const juce::String& label);

    AuroraLookAndFeel auroraLookAndFeel;
    std::array<juce::Slider, knobCount> knobs;
    std::array<juce::Label, knobCount> labels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KarmaTestBedAudioProcessorEditor)
};
