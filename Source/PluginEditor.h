#pragma once

#include "PluginProcessor.h"

class ChoraleEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit ChoraleEditor (ChoraleProcessor&);
    ~ChoraleEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    Knob& addKnob (const char* parameterId, const juce::String& text);
    void timerCallback() override;
    void updateSyllableDisplay();

    ChoraleProcessor& processor;
    juce::OwnedArray<Knob> knobs;

    juce::ComboBox presets;
    juce::TextEditor lyricsEditor;
    juce::Label syllableLabel;
    juce::TextButton restartButton;
    juce::ToggleButton lyricsToggle, octaveToggle;
    std::unique_ptr<ButtonAttachment> lyricsAttachment, octaveAttachment;

    std::vector<choir::Syllable> syllables;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoraleEditor)
};
