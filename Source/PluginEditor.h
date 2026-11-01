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
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboAttachment> attachment;
    };

    void addKnob (const char* parameterId, const juce::String& text);
    void addChoice (const char* parameterId, const juce::String& text);
    void timerCallback() override;

    ChoraleProcessor& chorale;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Choice> choices;

    juce::ComboBox presets;
    juce::Label noteLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoraleEditor)
};
