#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using Apvts = juce::AudioProcessorValueTreeState;

    const char* const kLyricsProperty = "lyrics";

    Apvts::ParameterLayout createLayout()
    {
        const auto vowelName = [] (float value, int)
        {
            static const char* const names[] = { "A", "E", "I", "O", "OU" };
            return juce::String (names[juce::jlimit (0, 4, juce::roundToInt (value))]);
        };

        const auto addFloat = [] (Apvts::ParameterLayout& layout, const char* id, const juce::String& name,
                                  juce::NormalisableRange<float> range, float defaultValue)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, defaultValue));
        };

        Apvts::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "lyricsOn", 1 }, "Paroles", true));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "vowel", 1 }, "Voyelle", juce::NormalisableRange<float> (0.0f, 4.0f, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (vowelName)));

        addFloat (layout, "soprano", "Soprano", { 0.0f, 1.0f, 0.01f }, 0.8f);
        addFloat (layout, "alto", "Alto", { 0.0f, 1.0f, 0.01f }, 0.8f);
        addFloat (layout, "tenor", juce::String::fromUTF8 ("T\xC3\xA9nor"), { 0.0f, 1.0f, 0.01f }, 0.8f);
        addFloat (layout, "bass", "Basse", { 0.0f, 1.0f, 0.01f }, 0.8f);
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "singers", 1 }, "Chanteurs", 1, choir::kMaxSingers, 4));
        addFloat (layout, "ensemble", "Ensemble", { 0.0f, 1.0f, 0.01f }, 0.5f);
        addFloat (layout, "vibratoRate", "Vitesse vibrato", { 3.0f, 8.0f, 0.01f }, 5.2f);
        addFloat (layout, "vibratoDepth", "Profondeur vibrato", { 0.0f, 100.0f, 0.1f }, 25.0f);
        addFloat (layout, "breath", "Souffle", { 0.0f, 1.0f, 0.01f }, 0.15f);
        addFloat (layout, "brightness", "Brillance", { 0.0f, 1.0f, 0.01f }, 0.5f);
        addFloat (layout, "consonants", "Consonnes", { 0.0f, 1.0f, 0.01f }, 0.7f);
        addFloat (layout, "attack", "Attaque", { 0.002f, 3.0f, 0.001f, 0.4f }, 0.04f);
        addFloat (layout, "release", juce::String::fromUTF8 ("Rel\xC3\xA2\x63hement"), { 0.01f, 5.0f, 0.001f, 0.4f }, 0.6f);
        addFloat (layout, "reverbMix", juce::String::fromUTF8 ("R\xC3\xA9verb\xC3\xA9ration"), { 0.0f, 1.0f, 0.01f }, 0.3f);
        addFloat (layout, "reverbSize", "Taille salle", { 0.0f, 1.0f, 0.01f }, 0.7f);
        addFloat (layout, "gain", "Volume", { -40.0f, 6.0f, 0.1f }, -6.0f);
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "autoOctave", 1 }, "Octaves auto", true));
        return layout;
    }

    struct Preset
    {
        const char* name;
        const char* lyrics;
        bool lyricsOn;
        float vowel, soprano, alto, tenor, bass;
        int singers;
        float ensemble, vibratoDepth, breath, brightness, attack, release, reverbMix, reverbSize;
    };

    const Preset kPresets[] = {
        { "Gloria - Choeur complet", "Glo-ri-a in ex-cel-sis De-o", true, 0, 0.8f, 0.8f, 0.8f, 0.8f, 4, 0.5f, 25, 0.15f, 0.5f, 0.04f, 0.6f, 0.30f, 0.7f },
        { "Ave Maria - Sopranos", "A-ve Ma-ri-a", true, 0, 1.0f, 0.5f, 0.0f, 0.0f, 6, 0.4f, 35, 0.2f, 0.6f, 0.12f, 1.0f, 0.45f, 0.85f },
        { "Kyrie - Moines", "Ky-ri-e e-le-i-son", true, 0, 0.0f, 0.0f, 0.6f, 1.0f, 8, 0.35f, 8, 0.1f, 0.3f, 0.2f, 1.2f, 0.5f, 0.95f },
        { "Amen - Gospel", "A-men", true, 0, 0.9f, 0.9f, 0.9f, 0.7f, 5, 0.6f, 45, 0.2f, 0.8f, 0.03f, 0.4f, 0.2f, 0.45f },
        { "Paroles - Solistes", "La vie est bel-le", true, 0, 0.8f, 0.7f, 0.7f, 0.6f, 1, 0.1f, 30, 0.1f, 0.6f, 0.03f, 0.3f, 0.15f, 0.35f },
        { "Nappe - Ooh", "", false, 3.6f, 0.8f, 0.8f, 0.7f, 0.6f, 8, 0.7f, 18, 0.25f, 0.25f, 0.8f, 2.0f, 0.55f, 0.9f },
    };
}

//==============================================================================
ChoraleProcessor::ChoraleProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createLayout())
{
    setLyrics (kPresets[0].lyrics);
}

void ChoraleProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    engine.setParams (readParams());
}

bool ChoraleProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

choir::Params ChoraleProcessor::readParams() const
{
    const auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    choir::Params p;
    p.lyricsOn = value ("lyricsOn") > 0.5f;
    p.vowel = value ("vowel");
    p.sectionLevel = { value ("soprano"), value ("alto"), value ("tenor"), value ("bass") };
    p.singers = juce::roundToInt (value ("singers"));
    p.ensemble = value ("ensemble");
    p.vibratoRate = value ("vibratoRate");
    p.vibratoDepth = value ("vibratoDepth");
    p.breath = value ("breath");
    p.brightness = value ("brightness");
    p.consonants = value ("consonants");
    p.attack = value ("attack");
    p.release = value ("release");
    p.reverbMix = value ("reverbMix");
    p.reverbSize = value ("reverbSize");
    p.gainDb = value ("gain");
    p.autoOctave = value ("autoOctave") > 0.5f;
    return p;
}

void ChoraleProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber());
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        engine.allNotesOff();
    else if (m.isSustainPedalOn())
        engine.setSustain (true);
    else if (m.isSustainPedalOff())
        engine.setSustain (false);
    else if (m.isPitchWheel())
        engine.setPitchBend (2.0f * (float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isControllerOfType (1))
        engine.setModWheel ((float) m.getControllerValue() / 127.0f);
}

// Les paroles repartent du debut au lancement de la lecture et a chaque retour de boucle.
void ChoraleProcessor::handleTransport()
{
    if (restartLyrics.exchange (false))
        engine.resetLyricsPosition();

    auto* playHead = getPlayHead();
    if (playHead == nullptr)
        return;

    const auto position = playHead->getPosition();
    if (! position.hasValue())
        return;

    const bool playing = position->getIsPlaying();
    const double ppq = position->getPpqPosition().orFallback (0.0);

    if (playing && (! wasPlaying || ppq < lastPpq - 0.01))
        engine.resetLyricsPosition();

    wasPlaying = playing;
    lastPpq = ppq;
}

void ChoraleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    engine.setParams (readParams());
    handleTransport();

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    int position = 0;
    for (const auto metadata : midi)
    {
        const int eventPosition = juce::jlimit (0, buffer.getNumSamples(), metadata.samplePosition);
        if (eventPosition > position)
        {
            engine.render (left + position, right + position, eventPosition - position);
            position = eventPosition;
        }
        handleMidi (metadata.getMessage());
    }

    if (position < buffer.getNumSamples())
        engine.render (left + position, right + position, buffer.getNumSamples() - position);

    midi.clear();
}

juce::AudioProcessorEditor* ChoraleProcessor::createEditor()
{
    return new ChoraleEditor (*this);
}

//==============================================================================
int ChoraleProcessor::getNumPrograms()
{
    return (int) std::size (kPresets);
}

const juce::String ChoraleProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? juce::String (kPresets[index].name) : juce::String();
}

void ChoraleProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    currentProgram = index;
    const auto& preset = kPresets[index];

    const auto set = [this] (const char* id, float value)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    set ("lyricsOn", preset.lyricsOn ? 1.0f : 0.0f);
    set ("vowel", preset.vowel);
    set ("soprano", preset.soprano);
    set ("alto", preset.alto);
    set ("tenor", preset.tenor);
    set ("bass", preset.bass);
    set ("singers", (float) preset.singers);
    set ("ensemble", preset.ensemble);
    set ("vibratoDepth", preset.vibratoDepth);
    set ("breath", preset.breath);
    set ("brightness", preset.brightness);
    set ("attack", preset.attack);
    set ("release", preset.release);
    set ("reverbMix", preset.reverbMix);
    set ("reverbSize", preset.reverbSize);

    if (preset.lyricsOn)
        setLyrics (preset.lyrics);
}

juce::String ChoraleProcessor::getLyrics() const
{
    return apvts.state.getProperty (kLyricsProperty).toString();
}

void ChoraleProcessor::setLyrics (const juce::String& text)
{
    apvts.state.setProperty (kLyricsProperty, text, nullptr);
    engine.setLyrics (choir::parseLyrics (text.toStdString()));
}

void ChoraleProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChoraleProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            engine.setLyrics (choir::parseLyrics (getLyrics().toStdString()));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChoraleProcessor();
}
