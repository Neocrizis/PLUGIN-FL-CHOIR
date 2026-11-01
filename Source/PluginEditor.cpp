#include "PluginEditor.h"

namespace
{
    const juce::Colour kBackground (0xff17151f);
    const juce::Colour kPanel (0xff24212f);
    const juce::Colour kAccent (0xffe8b86d);
    const juce::Colour kText (0xffece8f4);
    const juce::Colour kDim (0xff9a94ab);

    constexpr int kWidth = 780;
    constexpr int kHeight = 540;
    constexpr int kKnobColumns = 8;
}

ChoraleEditor::ChoraleEditor (ChoraleProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presets.addItem (processor.getProgramName (i), i + 1);
    presets.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);
    presets.onChange = [this]
    {
        processor.setCurrentProgram (presets.getSelectedId() - 1);
        lyricsEditor.setText (processor.getLyrics(), juce::dontSendNotification);
        syllables = choir::parseLyrics (processor.getLyrics().toStdString());
    };
    addAndMakeVisible (presets);

    lyricsEditor.setMultiLine (true);
    lyricsEditor.setReturnKeyStartsNewLine (true);
    lyricsEditor.setFont (juce::FontOptions (17.0f));
    lyricsEditor.setText (processor.getLyrics(), juce::dontSendNotification);
    lyricsEditor.setColour (juce::TextEditor::backgroundColourId, kPanel);
    lyricsEditor.setColour (juce::TextEditor::textColourId, kText);
    lyricsEditor.setColour (juce::TextEditor::outlineColourId, kDim.withAlpha (0.4f));
    lyricsEditor.setColour (juce::TextEditor::focusedOutlineColourId, kAccent);
    lyricsEditor.onTextChange = [this]
    {
        processor.setLyrics (lyricsEditor.getText());
        syllables = choir::parseLyrics (lyricsEditor.getText().toStdString());
    };
    addAndMakeVisible (lyricsEditor);
    syllables = choir::parseLyrics (processor.getLyrics().toStdString());

    syllableLabel.setColour (juce::Label::textColourId, kAccent);
    syllableLabel.setFont (juce::FontOptions (15.0f));
    addAndMakeVisible (syllableLabel);

    restartButton.setButtonText (juce::String::fromUTF8 ("Reprendre au d\xC3\xA9\x62ut"));
    restartButton.onClick = [this] { processor.requestLyricsRestart(); };
    addAndMakeVisible (restartButton);

    lyricsToggle.setButtonText ("Paroles");
    octaveToggle.setButtonText ("Octaves auto");
    for (auto* toggle : { &lyricsToggle, &octaveToggle })
    {
        toggle->setColour (juce::ToggleButton::textColourId, kText);
        toggle->setColour (juce::ToggleButton::tickColourId, kAccent);
        addAndMakeVisible (*toggle);
    }
    lyricsAttachment = std::make_unique<ButtonAttachment> (processor.apvts, "lyricsOn", lyricsToggle);
    octaveAttachment = std::make_unique<ButtonAttachment> (processor.apvts, "autoOctave", octaveToggle);

    addKnob ("soprano", "Soprano");
    addKnob ("alto", "Alto");
    addKnob ("tenor", juce::String::fromUTF8 ("T\xC3\xA9nor"));
    addKnob ("bass", "Basse");
    addKnob ("singers", "Chanteurs");
    addKnob ("ensemble", "Ensemble");
    addKnob ("consonants", "Consonnes");
    addKnob ("vowel", "Voyelle");
    addKnob ("vibratoRate", "Vib. vitesse");
    addKnob ("vibratoDepth", "Vib. profondeur");
    addKnob ("breath", "Souffle");
    addKnob ("brightness", "Brillance");
    addKnob ("attack", "Attaque");
    addKnob ("release", juce::String::fromUTF8 ("Rel\xC3\xA2\x63hement"));
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

ChoraleEditor::Knob& ChoraleEditor::addKnob (const char* parameterId, const juce::String& text)
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

    knob->attachment = std::make_unique<SliderAttachment> (processor.apvts, parameterId, knob->slider);
    return *knob;
}

void ChoraleEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("Chorale", 20, 12, 200, 36, juce::Justification::centredLeft);
}

void ChoraleEditor::resized()
{
    auto area = getLocalBounds().reduced (20, 12);

    auto top = area.removeFromTop (36);
    top.removeFromLeft (150);
    presets.setBounds (top.removeFromRight (260).reduced (0, 3));

    area.removeFromTop (8);
    lyricsEditor.setBounds (area.removeFromTop (84));

    area.removeFromTop (6);
    auto row = area.removeFromTop (30);
    lyricsToggle.setBounds (row.removeFromLeft (90));
    octaveToggle.setBounds (row.removeFromLeft (125));
    restartButton.setBounds (row.removeFromRight (160).reduced (0, 2));
    syllableLabel.setBounds (row);

    area.removeFromTop (10);
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
    updateSyllableDisplay();
}

void ChoraleEditor::updateSyllableDisplay()
{
    if (syllables.empty())
    {
        syllableLabel.setText ("Aucune syllabe", juce::dontSendNotification);
        return;
    }

    const int position = juce::jlimit (0, (int) syllables.size() - 1, processor.getLyricsPosition());
    syllableLabel.setText (juce::String::fromUTF8 ("Prochaine : ")
                               + juce::String (syllables[(size_t) position].text)
                               + "  (" + juce::String (position + 1) + "/" + juce::String ((int) syllables.size()) + ")",
                           juce::dontSendNotification);
}
