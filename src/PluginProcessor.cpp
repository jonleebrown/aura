#include "PluginProcessor.h"

#include "PluginEditor.h"

KarmaTestBedAudioProcessor::KarmaTestBedAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void KarmaTestBedAudioProcessor::prepareToPlay(double sampleRate,
                                               int maximumExpectedSamplesPerBlock)
{
    juce::ignoreUnused(sampleRate, maximumExpectedSamplesPerBlock);
}

void KarmaTestBedAudioProcessor::releaseResources()
{
}

bool KarmaTestBedAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void KarmaTestBedAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(buffer, midiMessages);
    juce::ScopedNoDenormals noDenormals;
}

juce::AudioProcessorEditor* KarmaTestBedAudioProcessor::createEditor()
{
    return new KarmaTestBedAudioProcessorEditor(*this);
}

bool KarmaTestBedAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String KarmaTestBedAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool KarmaTestBedAudioProcessor::acceptsMidi() const
{
    return false;
}

bool KarmaTestBedAudioProcessor::producesMidi() const
{
    return false;
}

bool KarmaTestBedAudioProcessor::isMidiEffect() const
{
    return false;
}

double KarmaTestBedAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int KarmaTestBedAudioProcessor::getNumPrograms()
{
    return 1;
}

int KarmaTestBedAudioProcessor::getCurrentProgram()
{
    return 0;
}

void KarmaTestBedAudioProcessor::setCurrentProgram(int)
{
}

const juce::String KarmaTestBedAudioProcessor::getProgramName(int)
{
    return {};
}

void KarmaTestBedAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void KarmaTestBedAudioProcessor::getStateInformation(juce::MemoryBlock& destinationData)
{
    destinationData.reset();
}

void KarmaTestBedAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ignoreUnused(data, sizeInBytes);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KarmaTestBedAudioProcessor();
}
