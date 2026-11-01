#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using Apvts = juce::AudioProcessorValueTreeState;

    Apvts::ParameterLayout createLayout()
    {
        const auto addFloat = [] (Apvts::ParameterLayout& layout, const char* id, const juce::String& name,
                                  juce::NormalisableRange<float> range, float defaultValue)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, defaultValue));
        };

        Apvts::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "harmony", 1 }, "Harmonie", juce::StringArray { "Unisson", "Octaves", "Accord" }, 2));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "key", 1 }, juce::String::fromUTF8 ("Tonalit\xC3\xA9"),
            juce::StringArray { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "scale", 1 }, "Gamme", juce::StringArray { "Majeure", "Mineure", "Sans gamme" }, 0));

        addFloat (layout, "soprano", "Soprano", { 0.0f, 1.0f, 0.01f }, 0.8f);
        addFloat (layout, "alto", "Alto", { 0.0f, 1.0f, 0.01f }, 0.8f);
        addFloat (layout, "tenor", juce::String::fromUTF8 ("T\xC3\xA9nor"), { 0.0f, 1.0f, 0.01f }, 0.8f);
        addFloat (layout, "bass", "Basse", { 0.0f, 1.0f, 0.01f }, 0.8f);
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "singers", 1 }, "Chanteurs", 1, choir::kMaxSingers, 4));
        addFloat (layout, "ensemble", "Ensemble", { 0.0f, 1.0f, 0.01f }, 0.5f);
        addFloat (layout, "character", juce::String::fromUTF8 ("Caract\xC3\xA8re"), { 0.0f, 1.5f, 0.01f }, 1.0f);
        addFloat (layout, "choirLevel", "Choeur", { 0.0f, 1.0f, 0.01f }, 0.8f);
        addFloat (layout, "dryLevel", "Voix", { 0.0f, 1.0f, 0.01f }, 0.7f);
        addFloat (layout, "gate", "Seuil", { -70.0f, -20.0f, 0.1f }, -50.0f);
        addFloat (layout, "reverbMix", juce::String::fromUTF8 ("R\xC3\xA9verb\xC3\xA9ration"), { 0.0f, 1.0f, 0.01f }, 0.3f);
        addFloat (layout, "reverbSize", "Taille salle", { 0.0f, 1.0f, 0.01f }, 0.7f);
        addFloat (layout, "gain", "Volume", { -40.0f, 6.0f, 0.1f }, 0.0f);
        return layout;
    }

    struct Preset
    {
        const char* name;
        int harmony;
        float soprano, alto, tenor, bass;
        int singers;
        float ensemble, character, choirLevel, dryLevel, reverbMix, reverbSize;
    };

    const Preset kPresets[] = {
        { "Grand choeur", 2, 0.8f, 0.8f, 0.8f, 0.8f, 6, 0.5f, 1.0f, 0.9f, 0.5f, 0.35f, 0.7f },
        { "Gospel", 2, 1.0f, 0.9f, 0.8f, 0.6f, 4, 0.6f, 1.0f, 0.9f, 0.8f, 0.2f, 0.45f },
        { "Unisson large", 0, 0.8f, 0.8f, 0.8f, 0.8f, 8, 0.7f, 1.0f, 0.9f, 0.4f, 0.3f, 0.6f },
        { "Octaves", 1, 0.8f, 0.5f, 0.5f, 0.8f, 5, 0.4f, 1.0f, 0.9f, 0.6f, 0.3f, 0.6f },
        { "Petit ensemble", 2, 0.7f, 0.7f, 0.7f, 0.5f, 1, 0.3f, 1.0f, 0.8f, 0.8f, 0.15f, 0.35f },
        { "Cathedrale", 2, 0.8f, 0.7f, 0.8f, 1.0f, 8, 0.45f, 1.0f, 0.8f, 0.2f, 0.6f, 0.95f },
    };
}

//==============================================================================
ChoraleProcessor::ChoraleProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createLayout())
{
}

void ChoraleProcessor::prepareToPlay (double sampleRate, int)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate);
}

bool ChoraleProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (input == juce::AudioChannelSet::stereo() || input == juce::AudioChannelSet::mono());
}

choir::Params ChoraleProcessor::readParams() const
{
    const auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    choir::Params p;
    p.harmony = (choir::Harmony) juce::roundToInt (value ("harmony"));
    p.key = juce::roundToInt (value ("key"));
    p.scale = (choir::Scale) juce::roundToInt (value ("scale"));
    p.sectionLevel = { value ("soprano"), value ("alto"), value ("tenor"), value ("bass") };
    p.singers = juce::roundToInt (value ("singers"));
    p.ensemble = value ("ensemble");
    p.character = value ("character");
    p.choirLevel = value ("choirLevel");
    p.dryLevel = value ("dryLevel");
    p.gateDb = value ("gate");
    p.reverbMix = value ("reverbMix");
    p.reverbSize = value ("reverbSize");
    p.gainDb = value ("gain");
    return p;
}

void ChoraleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    engine.setParams (readParams());

    const int inputs = getTotalNumInputChannels();
    if (inputs == 0 || buffer.getNumChannels() == 0)
    {
        buffer.clear();
        return;
    }

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
    const float* inputRight = inputs > 1 ? right : left;
    engine.process (left, inputRight, left, right, buffer.getNumSamples());
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

    set ("harmony", (float) preset.harmony);
    set ("soprano", preset.soprano);
    set ("alto", preset.alto);
    set ("tenor", preset.tenor);
    set ("bass", preset.bass);
    set ("singers", (float) preset.singers);
    set ("ensemble", preset.ensemble);
    set ("character", preset.character);
    set ("choirLevel", preset.choirLevel);
    set ("dryLevel", preset.dryLevel);
    set ("reverbMix", preset.reverbMix);
    set ("reverbSize", preset.reverbSize);
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
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChoraleProcessor();
}
