#include "PluginEditor.h"

namespace
{
    const juce::Colour kBackground (0xff17151f);
    const juce::Colour kPanel (0xff24212f);
    const juce::Colour kAccent (0xffe8b86d);
    const juce::Colour kText (0xffece8f4);
    const juce::Colour kDim (0xff9a94ab);

    constexpr int kWidth = 780;
    constexpr int kHeight = 470;
    constexpr int kKnobColumns = 7;

    juce::String noteName (float midi)
    {
        if (midi < 0.0f)
            return juce::String::fromUTF8 ("Voix : en attente");

        static const char* const names[] = { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
        const int note = juce::roundToInt (midi);
        return "Voix : " + juce::String (names[note % 12]) + juce::String (note / 12 - 1);
    }
}

ChoraleEditor::ChoraleEditor (ChoraleProcessor& p)
    : AudioProcessorEditor (p), chorale (p)
{
    for (int i = 0; i < chorale.getNumPrograms(); ++i)
        presets.addItem (chorale.getProgramName (i), i + 1);
    presets.setSelectedId (chorale.getCurrentProgram() + 1, juce::dontSendNotification);
    presets.onChange = [this] { chorale.setCurrentProgram (presets.getSelectedId() - 1); };
    addAndMakeVisible (presets);

    noteLabel.setColour (juce::Label::textColourId, kAccent);
    noteLabel.setFont (juce::FontOptions (16.0f));
    addAndMakeVisible (noteLabel);

    addChoice ("style", "Style");
    addChoice ("harmony", "Harmonie");
    addChoice ("key", juce::String::fromUTF8 ("Tonalit\xC3\xA9"));
    addChoice ("scale", "Gamme");

    addKnob ("soprano", "Soprano");
    addKnob ("alto", "Alto");
    addKnob ("tenor", juce::String::fromUTF8 ("T\xC3\xA9nor"));
    addKnob ("bass", "Basse");
    addKnob ("singers", "Chanteurs");
    addKnob ("ensemble", "Ensemble");
    addKnob ("character", juce::String::fromUTF8 ("Caract\xC3\xA8re"));
    addKnob ("choirLevel", "Choeur");
    addKnob ("dryLevel", "Voix");
    addKnob ("gate", "Seuil");
    addKnob ("reverbMix", juce::String::fromUTF8 ("R\xC3\xA9verb."));
    addKnob ("reverbSize", "Salle");
    addKnob ("gain", "Volume");

    setSize (kWidth, kHeight);
    startTimerHz (15);
}

ChoraleEditor::~ChoraleEditor()
{
    stopTimer();
}

void ChoraleEditor::addKnob (const char* parameterId, const juce::String& text)
{
    auto* knob = knobs.add (new Knob());

    knob->slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    knob->slider.setColour (juce::Slider::rotarySliderOutlineColourId, kPanel.brighter (0.3f));
    knob->slider.setColour (juce::Slider::thumbColourId, kText);
    knob->slider.setColour (juce::Slider::textBoxTextColourId, kDim);
    knob->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (knob->slider);

    knob->label.setText (text, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setColour (juce::Label::textColourId, kText);
    knob->label.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (knob->label);

    knob->attachment = std::make_unique<SliderAttachment> (chorale.apvts, parameterId, knob->slider);
}

void ChoraleEditor::addChoice (const char* parameterId, const juce::String& text)
{
    auto* choice = choices.add (new Choice());

    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (chorale.apvts.getParameter (parameterId)))
        choice->box.addItemList (parameter->choices, 1);
    addAndMakeVisible (choice->box);

    choice->label.setText (text, juce::dontSendNotification);
    choice->label.setColour (juce::Label::textColourId, kDim);
    choice->label.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (choice->label);

    choice->attachment = std::make_unique<ComboAttachment> (chorale.apvts, parameterId, choice->box);
}

void ChoraleEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("Chorale", 20, 12, 130, 36, juce::Justification::centredLeft);
}

void ChoraleEditor::resized()
{
    auto area = getLocalBounds().reduced (20, 12);

    auto top = area.removeFromTop (36);
    top.removeFromLeft (140);
    presets.setBounds (top.removeFromRight (240).reduced (0, 3));
    noteLabel.setBounds (top);

    area.removeFromTop (10);
    auto choiceRow = area.removeFromTop (52);
    const int choiceWidth = choiceRow.getWidth() / juce::jmax (1, choices.size());
    for (auto* choice : choices)
    {
        auto cell = choiceRow.removeFromLeft (choiceWidth).reduced (6, 0);
        choice->label.setBounds (cell.removeFromTop (20));
        choice->box.setBounds (cell.reduced (0, 2));
    }

    area.removeFromTop (12);
    const int rows = (knobs.size() + kKnobColumns - 1) / kKnobColumns;
    const int rowHeight = area.getHeight() / rows;
    const int columnWidth = area.getWidth() / kKnobColumns;

    for (int i = 0; i < knobs.size(); ++i)
    {
        juce::Rectangle<int> cell (area.getX() + (i % kKnobColumns) * columnWidth,
                                   area.getY() + (i / kKnobColumns) * rowHeight,
                                   columnWidth, rowHeight);
        knobs[i]->label.setBounds (cell.removeFromTop (20));
        knobs[i]->slider.setBounds (cell);
    }
}

void ChoraleEditor::timerCallback()
{
    noteLabel.setText (noteName (chorale.getDetectedMidi()), juce::dontSendNotification);
}
