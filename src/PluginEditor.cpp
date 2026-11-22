#include "PluginEditor.h"

#include <cmath>

namespace
{
const juce::Colour panelColour{0xff202124};
const juce::Colour knobFaceColour{0xff303336};
const juce::Colour accentColour{0xff25b8a2};
const juce::Colour mutedColour{0xff73787a};
}

void AuroraLookAndFeel::drawRotarySlider(juce::Graphics& graphics,
                                         int x,
                                         int y,
                                         int width,
                                         int height,
                                         float sliderPosition,
                                         float rotaryStartAngle,
                                         float rotaryEndAngle,
                                         juce::Slider& slider)
{
    const auto enabled = slider.isEnabled();
    const auto bounds = juce::Rectangle<float>{static_cast<float>(x),
                                               static_cast<float>(y),
                                               static_cast<float>(width),
                                               static_cast<float>(height)}.reduced(9.0f);
    const auto diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto knobBounds = juce::Rectangle<float>{diameter, diameter}.withCentre(bounds.getCentre());
    const auto centre = knobBounds.getCentre();
    const auto radius = diameter * 0.5f;

    graphics.setColour(juce::Colour{0xff101112});
    graphics.fillEllipse(knobBounds.translated(0.0f, 3.0f));
    graphics.setGradientFill(juce::ColourGradient{knobFaceColour.brighter(0.12f),
                                                  knobBounds.getX(),
                                                  knobBounds.getY(),
                                                  knobFaceColour.darker(0.45f),
                                                  knobBounds.getRight(),
                                                  knobBounds.getBottom(),
                                                  false});
    graphics.fillEllipse(knobBounds);
    graphics.setColour(enabled ? juce::Colour{0xff555a5c} : juce::Colour{0xff414547});
    graphics.drawEllipse(knobBounds, 1.5f);

    const auto angle = rotaryStartAngle + sliderPosition * (rotaryEndAngle - rotaryStartAngle);
    const auto pointerLength = radius * 0.7f;
    const auto pointerInner = radius * 0.2f;
    const auto pointerStart = juce::Point<float>{
        centre.x + std::cos(angle) * pointerInner,
        centre.y + std::sin(angle) * pointerInner};
    const auto pointerEnd = juce::Point<float>{
        centre.x + std::cos(angle) * pointerLength,
        centre.y + std::sin(angle) * pointerLength};

    graphics.setColour(enabled ? juce::Colours::white : mutedColour);
    graphics.drawLine(pointerStart.x, pointerStart.y, pointerEnd.x, pointerEnd.y, 3.0f);
    graphics.setColour(enabled ? accentColour : mutedColour.darker(0.3f));
    graphics.fillEllipse(juce::Rectangle<float>{6.0f, 6.0f}.withCentre(centre));
}

KarmaTestBedAudioProcessorEditor::KarmaTestBedAudioProcessorEditor(
    KarmaTestBedAudioProcessor& processorToEdit)
    : AudioProcessorEditor(&processorToEdit)
{
    setLookAndFeel(&auroraLookAndFeel);

    configureKnob(warp, "WARP");
    configureKnob(time, "TIME");
    configureKnob(blur, "BLUR");
    configureKnob(reflect, "REFLECT");
    configureKnob(mix, "MIX");
    configureKnob(atmosphere, "ATMOSPHERE");
    setSize(430, 690);
}

KarmaTestBedAudioProcessorEditor::~KarmaTestBedAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void KarmaTestBedAudioProcessorEditor::configureKnob(Knob knob,
                                                      const juce::String& labelText)
{
    auto& slider = knobs[knob];
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::degreesToRadians(135.0f),
                               juce::degreesToRadians(405.0f),
                               true);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accentColour);
    slider.setEnabled(false);
    addAndMakeVisible(slider);

    auto& label = labels[knob];
    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, mutedColour);
    label.setFont(juce::FontOptions{12.0f, juce::Font::bold});
    addAndMakeVisible(label);
}

void KarmaTestBedAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(panelColour);

    graphics.setColour(juce::Colour{0xff090a0b});
    graphics.fillRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 5.0f);
    graphics.setColour(juce::Colour{0xff505456});
    graphics.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 5.0f, 1.0f);

    const auto panel = getLocalBounds().toFloat().reduced(16.0f);
    juce::ColourGradient spectralGradient{juce::Colour{0xff30dc66},
                                          panel.getX(),
                                          0.0f,
                                          juce::Colour{0xff2788df},
                                          panel.getRight(),
                                          0.0f,
                                          false};
    spectralGradient.addColour(0.5, juce::Colour{0xff22c9b9});
    graphics.setGradientFill(spectralGradient);

    for (int line = 0; line < 5; ++line)
    {
        const auto inset = 24.0f + static_cast<float>(line) * 8.0f;
        juce::Path trace;
        trace.startNewSubPath(panel.getX() + inset, 92.0f + static_cast<float>(line) * 7.0f);
        trace.lineTo(panel.getX() + inset, 55.0f);
        trace.lineTo(panel.getRight() - inset, 55.0f);
        trace.lineTo(panel.getRight() - inset, 92.0f + static_cast<float>(line) * 7.0f);
        graphics.strokePath(trace, juce::PathStrokeType{1.5f});
    }

    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::FontOptions{19.0f, juce::Font::bold});
    graphics.drawText("KARMA", 20, 20, getWidth() - 40, 26, juce::Justification::centred);

    graphics.setColour(juce::Colour{0xffc7cbcc});
    graphics.setFont(juce::FontOptions{12.0f});
    graphics.drawText("STEREO PASS-THROUGH TEST", 20, 44, getWidth() - 40, 20, juce::Justification::centred);

    graphics.setColour(juce::Colour{0xffe6e7e7});
    graphics.setFont(juce::FontOptions{10.0f, juce::Font::bold});
    graphics.drawText("REVERSE / FREEZE", 34, 114, 132, 18, juce::Justification::centredLeft);
    graphics.setColour(mutedColour);
    graphics.setFont(juce::FontOptions{9.0f});
    graphics.drawText("NOT IMPLEMENTED", 34, 132, 132, 14, juce::Justification::centredLeft);
}

void KarmaTestBedAudioProcessorEditor::resized()
{
    const int knobSize = 104;
    const int leftColumn = 174;
    const int rightColumn = 326;
    const int firstRow = 150;
    const int secondRow = 326;
    const int thirdRow = 502;

    const auto placeKnob = [this](Knob knob, int centreX, int top)
    {
        knobs[knob].setBounds(centreX - knobSize / 2, top, knobSize, knobSize);
        labels[knob].setBounds(centreX - 76, top + knobSize - 2, 152, 40);
    };

    placeKnob(warp, leftColumn, firstRow);
    placeKnob(time, rightColumn, firstRow + 30);
    placeKnob(blur, leftColumn, secondRow);
    placeKnob(reflect, rightColumn, secondRow + 30);
    placeKnob(mix, leftColumn, thirdRow);
    placeKnob(atmosphere, rightColumn, thirdRow + 30);
}
