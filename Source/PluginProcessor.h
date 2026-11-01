#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "ChoirEngine.h"

class ChoraleProcessor : public juce::AudioProcessor
{
public:
    ChoraleProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Chorale"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::String getLyrics() const;
    void setLyrics (const juce::String& text);
    void requestLyricsRestart() { restartLyrics.store (true); }
    int getLyricsPosition() const { return engine.getLyricsPosition(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    choir::Params readParams() const;
    void handleMidi (const juce::MidiMessage& message);
    void handleTransport();

    choir::ChoirEngine engine;
    std::atomic<bool> restartLyrics { false };
    int currentProgram = 0;
    bool wasPlaying = false;
    double lastPpq = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoraleProcessor)
};
