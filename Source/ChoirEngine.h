// Moteur audio de la Chorale : synthese par formants, sans JUCE ni echantillons.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace choir
{
constexpr float kPi = 3.14159265358979f;
constexpr int kMaxNotes = 12;
constexpr int kMaxSingers = 8;
constexpr int kNumSections = 4;
constexpr int kNumFormants = 5;
constexpr int kControlBlock = 32;
constexpr int kMaxSyllables = 512;

//==============================================================================
// Phonemes et paroles

enum Consonant : uint8_t { P, T, K, B, D, G, S, F, Sh, H, Z, V, J, M, N, L, R, W, kNumConsonants };

struct ConsonantSpec
{
    float closureMs, closureVoice, bodyMs, voice, noise, noiseHz, noiseQ, formantScale, vowelOverride;
};

inline const ConsonantSpec& consonantSpec (uint8_t c)
{
    static const ConsonantSpec specs[kNumConsonants] = {
        //  clos  cVoice body  voice noise  Hz     Q     fScale vowel
        { 35.f, 0.00f, 15.f, 0.0f, 0.70f,  900.f, 1.0f, 1.00f, -1.f }, // P
        { 30.f, 0.00f, 15.f, 0.0f, 0.80f, 4000.f, 1.5f, 1.00f, -1.f }, // T
        { 35.f, 0.00f, 20.f, 0.0f, 0.70f, 2000.f, 1.5f, 1.00f, -1.f }, // K
        { 30.f, 0.25f, 12.f, 0.4f, 0.30f,  900.f, 1.0f, 0.80f, -1.f }, // B
        { 25.f, 0.25f, 12.f, 0.4f, 0.35f, 3500.f, 1.5f, 0.85f, -1.f }, // D
        { 30.f, 0.25f, 15.f, 0.4f, 0.30f, 1800.f, 1.5f, 0.85f, -1.f }, // G
        {  0.f, 0.00f, 85.f, 0.0f, 0.70f, 6500.f, 3.0f, 1.00f, -1.f }, // S
        {  0.f, 0.00f, 75.f, 0.0f, 0.35f, 5000.f, 0.8f, 1.00f, -1.f }, // F
        {  0.f, 0.00f, 85.f, 0.0f, 0.70f, 3000.f, 2.0f, 1.00f, -1.f }, // Sh
        {  0.f, 0.00f, 55.f, 0.0f, 0.25f, 1500.f, 0.6f, 1.00f, -1.f }, // H
        {  0.f, 0.00f, 75.f, 0.5f, 0.45f, 6000.f, 3.0f, 0.90f, -1.f }, // Z
        {  0.f, 0.00f, 65.f, 0.6f, 0.20f, 4000.f, 0.8f, 0.85f, -1.f }, // V
        {  0.f, 0.00f, 75.f, 0.5f, 0.45f, 2800.f, 2.0f, 0.90f, -1.f }, // J
        {  0.f, 0.00f, 70.f, 0.55f, 0.0f, 1000.f, 1.0f, 0.55f, -1.f }, // M
        {  0.f, 0.00f, 65.f, 0.55f, 0.0f, 1000.f, 1.0f, 0.65f, -1.f }, // N
        {  0.f, 0.00f, 55.f, 0.70f, 0.0f, 1000.f, 1.0f, 0.75f, -1.f }, // L
        {  0.f, 0.00f, 50.f, 0.65f, 0.05f, 1500.f, 1.0f, 0.80f, -1.f }, // R
        {  0.f, 0.00f, 50.f, 0.80f, 0.0f, 1000.f, 1.0f, 1.00f,  4.f }, // W
    };
    return specs[c];
}

struct Syllable
{
    uint8_t onset[3] {};
    uint8_t numOnset = 0;
    float vowel = 0.0f; // 0=A 1=E 2=I 3=O 4=OU
    uint8_t coda[2] {};
    uint8_t numCoda = 0;
    char text[16] {};
};

namespace detail
{
    inline std::string normaliseLyrics (const std::string& in)
    {
        static const struct { const char* from; char to; } accents[] = {
            { "\xC3\xA9", 'e' }, { "\xC3\xA8", 'e' }, { "\xC3\xAA", 'e' }, { "\xC3\xAB", 'e' },
            { "\xC3\x89", 'e' }, { "\xC3\x88", 'e' }, { "\xC3\xA0", 'a' }, { "\xC3\xA2", 'a' },
            { "\xC3\x80", 'a' }, { "\xC3\xAE", 'i' }, { "\xC3\xAF", 'i' }, { "\xC3\xB4", 'o' },
            { "\xC3\xB9", 'u' }, { "\xC3\xBB", 'u' }, { "\xC3\xBC", 'u' }, { "\xC3\xA7", 's' },
            { "\xC5\x93", 'e' }, { "\xC5\x92", 'e' },
        };

        std::string out;
        for (size_t i = 0; i < in.size();)
        {
            bool replaced = false;
            for (const auto& a : accents)
            {
                if (in.compare (i, 2, a.from) == 0)
                {
                    out += a.to;
                    i += 2;
                    replaced = true;
                    break;
                }
            }
            if (replaced)
                continue;

            auto c = (unsigned char) in[i++];
            if (c >= 'A' && c <= 'Z')
                c = (unsigned char) (c - 'A' + 'a');
            if ((c >= 'a' && c <= 'z') || c == '-' || c == ' ' || c == '\n' || c == '\t' || c == '\r')
                out += (char) c;
            else if (c == '\'' || c == ',' || c == '.' || c == ';')
                out += ' ';
        }
        return out;
    }

    struct Phone
    {
        bool isVowel;
        float vowel;
        uint8_t consonant;
        std::string letters;
    };

    inline bool isVowelLetter (char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; }

    inline std::vector<Phone> toPhones (const std::string& w)
    {
        std::vector<Phone> phones;
        const auto vowel = [&] (float v, const char* s) { phones.push_back ({ true, v, 0, s }); };
        const auto cons = [&] (uint8_t c, const char* s) { phones.push_back ({ false, 0.0f, c, s }); };
        const auto starts = [&] (size_t i, const char* s) { return w.compare (i, std::strlen (s), s) == 0; };

        for (size_t i = 0; i < w.size();)
        {
            const char c = w[i];
            const char next = i + 1 < w.size() ? w[i + 1] : '\0';

            if (starts (i, "eau"))      { vowel (3, "eau"); i += 3; }
            else if (starts (i, "au"))  { vowel (3, "au");  i += 2; }
            else if (starts (i, "ou"))  { vowel (4, "ou");  i += 2; }
            else if (starts (i, "ai"))  { vowel (1, "ai");  i += 2; }
            else if (starts (i, "ei"))  { vowel (1, "ei");  i += 2; }
            else if (starts (i, "eu"))  { vowel (1, "eu");  i += 2; }
            else if (starts (i, "oi"))  { cons (W, "o"); vowel (0, "i"); i += 2; }
            else if (starts (i, "ch"))  { cons (Sh, "ch"); i += 2; }
            else if (starts (i, "ph"))  { cons (F, "ph");  i += 2; }
            else if (starts (i, "th"))  { cons (T, "th");  i += 2; }
            else if (starts (i, "qu"))  { cons (K, "qu");  i += 2; }
            else if (starts (i, "gn"))  { cons (N, "gn");  i += 2; }
            else if (! isVowelLetter (c) && c == next) { ++i; } // consonne doublee
            else
            {
                switch (c)
                {
                    case 'a': vowel (0, "a"); break;
                    case 'e': vowel (1, "e"); break;
                    case 'i': vowel (2, "i"); break;
                    case 'y': vowel (2, "y"); break;
                    case 'o': vowel (3, "o"); break;
                    case 'u': vowel (4, "u"); break;
                    case 'c': if (next == 'e' || next == 'i' || next == 'y') cons (S, "c"); else cons (K, "c"); break;
                    case 'g': if (next == 'e' || next == 'i' || next == 'y') cons (J, "g"); else cons (G, "g"); break;
                    case 'x': cons (K, "x"); cons (S, ""); break;
                    case 'p': cons (P, "p"); break;
                    case 't': cons (T, "t"); break;
                    case 'k': cons (K, "k"); break;
                    case 'b': cons (B, "b"); break;
                    case 'd': cons (D, "d"); break;
                    case 's': cons (S, "s"); break;
                    case 'f': cons (F, "f"); break;
                    case 'h': cons (H, "h"); break;
                    case 'z': cons (Z, "z"); break;
                    case 'v': cons (V, "v"); break;
                    case 'j': cons (J, "j"); break;
                    case 'm': cons (M, "m"); break;
                    case 'n': cons (N, "n"); break;
                    case 'l': cons (L, "l"); break;
                    case 'r': cons (R, "r"); break;
                    case 'w': cons (W, "w"); break;
                    default: break;
                }
                ++i;
            }
        }
        return phones;
    }

    inline bool canStartCluster (uint8_t first, uint8_t second)
    {
        const bool stopOrFricative = first == P || first == T || first == K || first == B
                                  || first == D || first == G || first == F || first == V;
        return stopOrFricative && (second == L || second == R);
    }

    struct DraftSyllable
    {
        std::vector<uint8_t> onset, coda;
        float vowel = 0.0f;
        std::string text;
    };

    inline void appendChunk (const std::string& chunk, std::vector<Syllable>& result)
    {
        std::vector<DraftSyllable> drafts;
        std::vector<Phone> pending;

        for (auto& ph : toPhones (chunk))
        {
            if (! ph.isVowel)
            {
                pending.push_back (ph);
                continue;
            }

            DraftSyllable d;
            size_t firstOnset = 0;
            if (! drafts.empty() && pending.size() >= 2)
            {
                // VCCV : la coupure tombe entre les consonnes, sauf groupes "tr", "bl"...
                firstOnset = pending.size() - 1;
                if (canStartCluster (pending[firstOnset - 1].consonant, pending[firstOnset].consonant))
                    --firstOnset;
                for (size_t i = 0; i < firstOnset; ++i)
                {
                    drafts.back().coda.push_back (pending[i].consonant);
                    drafts.back().text += pending[i].letters;
                }
            }
            for (size_t i = firstOnset; i < pending.size(); ++i)
            {
                d.onset.push_back (pending[i].consonant);
                d.text += pending[i].letters;
            }
            d.vowel = ph.vowel;
            d.text += ph.letters;
            drafts.push_back (std::move (d));
            pending.clear();
        }

        if (! drafts.empty())
        {
            for (auto& ph : pending)
            {
                drafts.back().coda.push_back (ph.consonant);
                drafts.back().text += ph.letters;
            }
        }

        for (auto& d : drafts)
        {
            Syllable s;
            s.numOnset = (uint8_t) std::min<size_t> (d.onset.size(), 3);
            for (int i = 0; i < s.numOnset; ++i)
                s.onset[i] = d.onset[d.onset.size() - (size_t) s.numOnset + (size_t) i];
            s.numCoda = (uint8_t) std::min<size_t> (d.coda.size(), 2);
            for (int i = 0; i < s.numCoda; ++i)
                s.coda[i] = d.coda[(size_t) i];
            s.vowel = d.vowel;
            std::strncpy (s.text, d.text.c_str(), sizeof (s.text) - 1);
            result.push_back (s);
        }
    }
} // namespace detail

// Espaces et tirets separent les morceaux ; chaque morceau est ensuite decoupe en syllabes.
inline std::vector<Syllable> parseLyrics (const std::string& utf8)
{
    std::vector<Syllable> result;
    std::string chunk;
    for (char c : detail::normaliseLyrics (utf8) + " ")
    {
        if (c == ' ' || c == '-' || c == '\n' || c == '\t' || c == '\r')
        {
            detail::appendChunk (chunk, result);
            chunk.clear();
        }
        else
        {
            chunk += c;
        }
    }
    if (result.size() > (size_t) kMaxSyllables)
        result.resize ((size_t) kMaxSyllables);
    return result;
}

//==============================================================================
// Reverberation : reseau de 8 lignes a retard avec matrice de Householder

class Reverb
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        static const float baseMs[8] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.3f, 73.1f };
        for (size_t i = 0; i < 8; ++i)
        {
            lengths[i] = std::max (8, (int) (baseMs[i] * 2.0f * sr / 1000.0f));
            lines[i].assign ((size_t) lengths[i], 0.0f);
            positions[i] = 0;
            damp[i] = 0.0f;
        }
        dampCoef = 1.0f - std::exp (-2.0f * kPi * 5500.0f / sr);
        setSize (0.7f);
    }

    void setSize (float size)
    {
        const float rt60 = 1.0f + 7.0f * size;
        for (size_t i = 0; i < 8; ++i)
            feedback[i] = std::pow (10.0f, -3.0f * ((float) lengths[i] / sr) / rt60);
    }

    void process (float inL, float inR, float& outL, float& outR)
    {
        std::array<float, 8> o;
        float sum = 0.0f;
        for (size_t i = 0; i < 8; ++i)
        {
            o[i] = lines[i][(size_t) positions[i]];
            sum += o[i];
        }

        for (size_t i = 0; i < 8; ++i)
        {
            damp[i] += dampCoef * ((o[i] - 0.25f * sum) - damp[i]);
            const float in = (i % 2 == 0) ? inL : inR;
            lines[i][(size_t) positions[i]] = in + damp[i] * feedback[i] + 1.0e-20f;
            if (++positions[i] >= lengths[i])
                positions[i] = 0;
        }

        outL = 0.35f * (o[0] - o[2] + o[4] - o[6]);
        outR = 0.35f * (o[1] - o[3] + o[5] - o[7]);
    }

private:
    float sr = 48000.0f, dampCoef = 0.5f;
    std::array<std::vector<float>, 8> lines;
    std::array<int, 8> lengths {}, positions {};
    std::array<float, 8> damp {}, feedback {};
};

//==============================================================================

struct Params
{
    bool lyricsOn = true;
    float vowel = 0.0f;
    std::array<float, kNumSections> sectionLevel { 0.8f, 0.8f, 0.8f, 0.8f }; // S A T B
    int singers = 4;
    float ensemble = 0.5f;
    float vibratoRate = 5.2f;
    float vibratoDepth = 25.0f;
    float breath = 0.15f;
    float brightness = 0.5f;
    float consonants = 0.7f;
    float attack = 0.04f;
    float release = 0.6f;
    float reverbMix = 0.3f;
    float reverbSize = 0.7f;
    float gainDb = -6.0f;
    bool autoOctave = true;
};

class ChoirEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        controlDt = (float) kControlBlock / sr;
        reverb.prepare (sampleRate);
        tiltCoef = 1.0f - std::exp (-2.0f * kPi * 700.0f / sr);
        rampCoef = 1.0f - std::exp (-1.0f / (0.004f * sr));
        glideCoef = 1.0f - std::exp (-controlDt / 0.012f);
        for (auto& n : notes)
            n.active = false;
        clock = 0;
        lastTriggerClock = -1000000;
        gainCurrent = dbToGain (params.gainDb);
        wetCurrent = params.reverbMix;
    }

    void setParams (const Params& p) { params = p; reverb.setSize (p.reverbSize); }

    void setLyrics (const std::vector<Syllable>& syllables)
    {
        while (lyricsLock.test_and_set (std::memory_order_acquire)) {}
        numLyrics = std::min ((int) syllables.size(), kMaxSyllables);
        std::copy_n (syllables.begin(), numLyrics, lyrics.begin());
        lyricsPosition.store (0);
        lyricsLock.clear (std::memory_order_release);
    }

    void resetLyricsPosition() { lyricsPosition.store (0); lastTriggerClock = -1000000; }
    int getLyricsPosition() const { return lyricsPosition.load(); }

    void noteOn (int midiNote, float velocity)
    {
        const auto syllable = nextSyllable();
        auto& n = pickNote();
        if (n.active)
        {
            n.stealing = true;
            n.pendingNote = midiNote;
            n.pendingVelocity = velocity;
            n.pendingSyllable = syllable;
            n.pendingHeld = true;
        }
        else
        {
            startNote (n, midiNote, velocity, syllable);
        }
    }

    void noteOff (int midiNote)
    {
        for (auto& n : notes)
        {
            if (n.active && n.held && n.midiNote == midiNote)
            {
                n.held = false;
                n.released = ! sustain;
            }
            if (n.stealing && n.pendingNote == midiNote)
                n.pendingHeld = false;
        }
    }

    void allNotesOff()
    {
        for (auto& n : notes)
        {
            n.held = false;
            n.released = true;
            n.stealing = false;
        }
    }

    void setSustain (bool on)
    {
        sustain = on;
        if (! on)
            for (auto& n : notes)
                if (n.active && ! n.held)
                    n.released = true;
    }

    void setPitchBend (float semitones) { pitchBend = semitones; }
    void setModWheel (float value) { modWheel = value; }

    // Remplace le contenu de left et right.
    void render (float* left, float* right, int numSamples)
    {
        int done = 0;
        while (done < numSamples)
        {
            if (controlCountdown <= 0)
            {
                updateControl();
                controlCountdown = kControlBlock;
            }

            const int n = std::min (controlCountdown, numSamples - done);
            renderChunk (left + done, right + done, n);
            controlCountdown -= n;
            done += n;
            clock += n;
        }
    }

private:
    //==========================================================================
    struct Singer
    {
        float phase = 0, inc = 0, detuneNorm = 0, drift = 0, driftTarget = 0;
        float vibPhase = 0, vibRate = 5, gainL = 0.7f, gainR = 0.7f;
        int delay = 0;
    };

    struct Svf
    {
        float ic1 = 0, ic2 = 0;
        inline float bandpass (float x, float a1, float a2, float a3)
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v1;
        }
    };

    struct FormantCoef { float a1 = 0, a2 = 0, a3 = 0, gain = 0; };

    struct Section
    {
        float pitch = 60;
        std::array<Singer, kMaxSingers> singers;
        std::array<FormantCoef, kNumFormants> coefs;
        std::array<Svf, kNumFormants> filtersL, filtersR;
        float tiltL = 0, tiltR = 0;
    };

    enum class Stage { onset, sustain, coda, done };

    struct Note
    {
        bool active = false, held = false, released = false, stealing = false, pendingHeld = false;
        int midiNote = 60, pendingNote = 60;
        float velocity = 1, pendingVelocity = 1;
        Syllable syllable, pendingSyllable;
        int64_t startClock = 0;
        float env = 0, age = 0;
        Stage stage = Stage::done;
        int phoneIndex = 0;
        float phoneTime = 0;
        float voiceCurrent = 0, voiceTarget = 0, noiseCurrent = 0, noiseTarget = 0;
        float vowel = 0, formantScale = 1, noiseHz = 2000, noiseQ = 1;
        FormantCoef noiseCoef;
        Svf noiseL, noiseR;
        std::array<Section, kNumSections> sections;
    };

    //==========================================================================
    static float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

    float random01()
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (float) (rngState & 0xffffff) / 16777215.0f;
    }

    float randomBipolar() { return 2.0f * random01() - 1.0f; }

    Syllable nextSyllable()
    {
        Syllable result;
        if (! params.lyricsOn)
        {
            result.vowel = params.vowel;
            return result;
        }

        // Les notes d'un meme accord chantent la meme syllabe.
        const bool sameChord = clock - lastTriggerClock < (int64_t) (0.03f * sr);
        if (lyricsLock.test_and_set (std::memory_order_acquire))
            return lastSyllable;

        if (numLyrics > 0)
        {
            if (sameChord)
            {
                result = lastSyllable;
            }
            else
            {
                int pos = lyricsPosition.load();
                if (pos >= numLyrics)
                    pos = 0;
                result = lyrics[(size_t) pos];
                lyricsPosition.store ((pos + 1) % numLyrics);
            }
        }
        lyricsLock.clear (std::memory_order_release);

        lastSyllable = result;
        lastTriggerClock = clock;
        return result;
    }

    Note& pickNote()
    {
        for (auto& n : notes)
            if (! n.active)
                return n;

        Note* best = nullptr;
        for (auto& n : notes)
            if (! n.stealing && n.released && (best == nullptr || n.env < best->env))
                best = &n;
        if (best != nullptr)
            return *best;

        for (auto& n : notes)
            if (! n.stealing && (best == nullptr || n.startClock < best->startClock))
                best = &n;
        return best != nullptr ? *best : notes[0];
    }

    void startNote (Note& n, int midiNote, float velocity, const Syllable& syllable)
    {
        static const float centres[kNumSections] = { 72.0f, 65.0f, 57.0f, 48.0f };
        static const float sectionPan[kNumSections] = { -0.25f, 0.25f, -0.1f, 0.1f };

        n.active = true;
        n.held = true;
        n.released = false;
        n.stealing = false;
        n.midiNote = midiNote;
        n.velocity = velocity;
        n.syllable = syllable;
        n.startClock = clock;
        n.env = 0;
        n.age = 0;
        n.stage = syllable.numOnset > 0 ? Stage::onset : Stage::sustain;
        n.phoneIndex = 0;
        n.phoneTime = 0;
        n.voiceCurrent = n.noiseCurrent = 0;
        n.vowel = syllable.vowel;
        n.formantScale = 1;
        n.noiseL = {};
        n.noiseR = {};

        for (int s = 0; s < kNumSections; ++s)
        {
            auto& sec = n.sections[(size_t) s];
            sec.pitch = (float) midiNote;
            if (params.autoOctave)
                sec.pitch += 12.0f * std::round ((centres[s] - (float) midiNote) / 12.0f);
            sec.filtersL = {};
            sec.filtersR = {};
            sec.tiltL = sec.tiltR = 0;

            for (auto& singer : sec.singers)
            {
                singer.phase = random01();
                singer.detuneNorm = randomBipolar();
                singer.drift = 0;
                singer.driftTarget = randomBipolar();
                singer.vibPhase = random01() * 2.0f * kPi;
                singer.vibRate = 0.9f + 0.2f * random01();
                singer.delay = (int) (random01() * params.ensemble * 0.015f * sr);
                const float pan = std::clamp (sectionPan[s] + 0.7f * randomBipolar(), -1.0f, 1.0f);
                const float angle = (pan + 1.0f) * 0.25f * kPi;
                singer.gainL = std::cos (angle);
                singer.gainR = std::sin (angle);
            }
        }
    }

    void updateArticulation (Note& n)
    {
        const auto& syl = n.syllable;
        const uint8_t* list = nullptr;
        int count = 0;

        if (n.stage == Stage::sustain && n.released)
        {
            n.stage = syl.numCoda > 0 ? Stage::coda : Stage::done;
            n.phoneIndex = 0;
            n.phoneTime = 0;
        }

        if (n.stage == Stage::onset)  { list = syl.onset; count = syl.numOnset; }
        else if (n.stage == Stage::coda) { list = syl.coda; count = syl.numCoda; }

        float vowelTarget = params.lyricsOn ? syl.vowel : params.vowel;
        float scaleTarget = 1.0f;

        if (list != nullptr)
        {
            const auto& spec = consonantSpec (list[n.phoneIndex]);
            const float ms = n.phoneTime * 1000.0f;

            if (ms < spec.closureMs)
            {
                n.voiceTarget = spec.closureVoice;
                n.noiseTarget = 0.0f;
            }
            else
            {
                n.voiceTarget = spec.voice;
                n.noiseTarget = spec.noise;
            }
            n.noiseHz = spec.noiseHz;
            n.noiseQ = spec.noiseQ;
            scaleTarget = spec.formantScale;
            if (spec.vowelOverride >= 0.0f)
                vowelTarget = spec.vowelOverride;

            n.phoneTime += controlDt;
            if (n.phoneTime * 1000.0f >= spec.closureMs + spec.bodyMs)
            {
                n.phoneTime = 0;
                if (++n.phoneIndex >= count)
                {
                    n.phoneIndex = 0;
                    n.stage = n.stage == Stage::onset ? Stage::sustain : Stage::done;
                }
            }
        }
        else
        {
            n.voiceTarget = (n.stage == Stage::done && syl.numCoda > 0) ? 0.0f : 1.0f;
            n.noiseTarget = 0.0f;
        }

        n.vowel += glideCoef * (vowelTarget - n.vowel);
        n.formantScale += glideCoef * (scaleTarget - n.formantScale);
    }

    static const float (&formantRow (int section, int vowel, int field))[kNumFormants]
    {
        // Frequences (Hz), amplitudes (dB) et largeurs de bande (Hz) classiques des voix chantees.
        static const float table[kNumSections][5][3][kNumFormants] = {
            { // Soprano
                { { 800, 1150, 2900, 3900, 4950 }, { 0, -6, -32, -20, -50 }, { 80, 90, 120, 130, 140 } },
                { { 350, 2000, 2800, 3600, 4950 }, { 0, -20, -15, -40, -56 }, { 60, 100, 120, 150, 200 } },
                { { 270, 2140, 2950, 3900, 4950 }, { 0, -12, -26, -26, -44 }, { 60, 90, 100, 120, 120 } },
                { { 450, 800, 2830, 3800, 4950 }, { 0, -11, -22, -22, -50 }, { 70, 80, 100, 130, 135 } },
                { { 325, 700, 2700, 3800, 4950 }, { 0, -16, -35, -40, -60 }, { 50, 60, 170, 180, 200 } },
            },
            { // Alto
                { { 800, 1150, 2800, 3500, 4950 }, { 0, -4, -20, -36, -60 }, { 80, 90, 120, 130, 140 } },
                { { 400, 1600, 2700, 3300, 4950 }, { 0, -24, -30, -35, -60 }, { 60, 80, 120, 150, 200 } },
                { { 350, 1700, 2700, 3700, 4950 }, { 0, -20, -30, -36, -60 }, { 50, 100, 120, 150, 200 } },
                { { 450, 800, 2830, 3500, 4950 }, { 0, -9, -16, -28, -55 }, { 70, 80, 100, 130, 135 } },
                { { 325, 700, 2530, 3500, 4950 }, { 0, -12, -30, -40, -64 }, { 50, 60, 170, 180, 200 } },
            },
            { // Tenor
                { { 650, 1080, 2650, 2900, 3250 }, { 0, -6, -7, -8, -22 }, { 80, 90, 120, 130, 140 } },
                { { 400, 1700, 2600, 3200, 3580 }, { 0, -14, -12, -14, -20 }, { 70, 80, 100, 120, 120 } },
                { { 290, 1870, 2800, 3250, 3540 }, { 0, -15, -18, -20, -30 }, { 40, 90, 100, 120, 120 } },
                { { 400, 800, 2600, 2800, 3000 }, { 0, -10, -12, -12, -26 }, { 40, 80, 100, 120, 120 } },
                { { 350, 600, 2700, 2900, 3300 }, { 0, -20, -17, -14, -26 }, { 40, 60, 100, 120, 120 } },
            },
            { // Basse
                { { 600, 1040, 2250, 2450, 2750 }, { 0, -7, -9, -9, -20 }, { 60, 70, 110, 120, 130 } },
                { { 400, 1620, 2400, 2800, 3100 }, { 0, -12, -9, -12, -18 }, { 40, 80, 100, 120, 120 } },
                { { 250, 1750, 2600, 3050, 3340 }, { 0, -30, -16, -22, -28 }, { 60, 90, 100, 120, 120 } },
                { { 400, 750, 2400, 2600, 2900 }, { 0, -11, -21, -20, -40 }, { 40, 80, 100, 120, 120 } },
                { { 350, 600, 2400, 2675, 2950 }, { 0, -20, -32, -28, -36 }, { 40, 80, 100, 120, 120 } },
            },
        };
        return table[section][vowel][field];
    }

    FormantCoef makeBandpass (float freq, float bandwidth, float gain) const
    {
        FormantCoef c;
        freq = std::min (freq, 0.45f * sr);
        const float g = std::tan (kPi * freq / sr);
        const float k = bandwidth / freq;
        c.a1 = 1.0f / (1.0f + g * (g + k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        c.gain = k * gain;
        return c;
    }

    void updateControl()
    {
        gainTarget = dbToGain (params.gainDb);
        const int numSingers = std::clamp (params.singers, 1, kMaxSingers);

        for (auto& n : notes)
        {
            if (! n.active)
                continue;

            n.age += controlDt;
            updateArticulation (n);
            n.noiseCoef = makeBandpass (n.noiseHz, n.noiseHz / std::max (0.3f, n.noiseQ), 1.0f);

            const float clampedVowel = std::clamp (n.vowel, 0.0f, 4.0f);
            const int v0 = std::min ((int) clampedVowel, 3);
            const float vf = clampedVowel - (float) v0;
            const float vibFade = std::clamp ((n.age - 0.25f) / 0.5f, 0.0f, 1.0f);
            const float vibDepth = (params.vibratoDepth + modWheel * 60.0f) * vibFade;

            for (int s = 0; s < kNumSections; ++s)
            {
                if (params.sectionLevel[(size_t) s] < 0.001f)
                    continue;

                auto& sec = n.sections[(size_t) s];
                for (int f = 0; f < kNumFormants; ++f)
                {
                    const float hz = formantRow (s, v0, 0)[f] + vf * (formantRow (s, v0 + 1, 0)[f] - formantRow (s, v0, 0)[f]);
                    const float db = formantRow (s, v0, 1)[f] + vf * (formantRow (s, v0 + 1, 1)[f] - formantRow (s, v0, 1)[f]);
                    const float bw = formantRow (s, v0, 2)[f] + vf * (formantRow (s, v0 + 1, 2)[f] - formantRow (s, v0, 2)[f]);
                    float amp = dbToGain (db);
                    if (f > 0)
                        amp *= n.formantScale * n.formantScale;
                    // Le premier formant suit la fondamentale, sinon les aigus perdent leur voix.
                    float target = hz * n.formantScale;
                    if (f == 0)
                        target = std::max (target, 440.0f * std::pow (2.0f, (sec.pitch - 69.0f) / 12.0f));
                    sec.coefs[(size_t) f] = makeBandpass (target, bw, amp);
                }

                for (int i = 0; i < numSingers; ++i)
                {
                    auto& singer = sec.singers[(size_t) i];
                    singer.vibPhase += 2.0f * kPi * params.vibratoRate * singer.vibRate * controlDt;
                    if (singer.vibPhase > 2.0f * kPi)
                        singer.vibPhase -= 2.0f * kPi;
                    singer.drift += 0.002f * (singer.driftTarget - singer.drift);
                    if (random01() < 0.002f)
                        singer.driftTarget = randomBipolar();

                    const float cents = params.ensemble * (22.0f * singer.detuneNorm + 10.0f * singer.drift)
                                      + vibDepth * std::sin (singer.vibPhase);
                    const float pitch = sec.pitch + pitchBend + cents * 0.01f;
                    singer.inc = std::min (0.45f, 440.0f * std::pow (2.0f, (pitch - 69.0f) / 12.0f) / sr);
                }
            }
        }
    }

    static float polyBlep (float t, float dt)
    {
        if (t < dt)
        {
            const float x = t / dt;
            return x + x - x * x - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            const float x = (t - 1.0f) / dt;
            return x * x + x + x + 1.0f;
        }
        return 0.0f;
    }

    void renderChunk (float* left, float* right, int numSamples)
    {
        std::array<float, kControlBlock> dryL {}, dryR {};

        for (auto& n : notes)
            if (n.active)
                renderNote (n, dryL.data(), dryR.data(), numSamples);

        const float wetTarget = params.reverbMix;
        for (int i = 0; i < numSamples; ++i)
        {
            gainCurrent += 0.002f * (gainTarget - gainCurrent);
            wetCurrent += 0.002f * (wetTarget - wetCurrent);

            float wetL, wetR;
            reverb.process (dryL[(size_t) i], dryR[(size_t) i], wetL, wetR);
            const float dryGain = 1.0f - 0.5f * wetCurrent;
            left[i] = std::tanh (gainCurrent * kMakeup * (dryL[(size_t) i] * dryGain + wetL * wetCurrent));
            right[i] = std::tanh (gainCurrent * kMakeup * (dryR[(size_t) i] * dryGain + wetR * wetCurrent));
        }
    }

    void renderNote (Note& n, float* outL, float* outR, int numSamples)
    {
        const int numSingers = std::clamp (params.singers, 1, kMaxSingers);
        const float singerNorm = 1.0f / std::sqrt ((float) numSingers);
        const float attackInc = 1.0f / (std::max (0.002f, params.attack) * sr);
        const float releaseCoef = std::exp (-5.0f / (std::max (0.01f, params.release) * sr));
        const float fastInc = 1.0f / (0.005f * sr);
        const float breath = params.breath * 0.6f;
        const float brightness = params.brightness;
        const auto& nc = n.noiseCoef;

        for (int i = 0; i < numSamples; ++i)
        {
            if (n.stealing)
            {
                n.env -= fastInc;
                if (n.env <= 0.0f)
                {
                    const Syllable syl = n.pendingSyllable;
                    const bool stillHeld = n.pendingHeld;
                    startNote (n, n.pendingNote, n.pendingVelocity, syl);
                    n.held = stillHeld;
                    n.released = ! stillHeld && ! sustain;
                    updateArticulation (n);
                }
            }
            else if (n.released)
            {
                n.env *= releaseCoef;
            }
            else
            {
                n.env = std::min (1.0f, n.env + attackInc);
            }

            n.voiceCurrent += rampCoef * (n.voiceTarget - n.voiceCurrent);
            n.noiseCurrent += rampCoef * (n.noiseTarget - n.noiseCurrent);

            const float noiseL = randomBipolar();
            const float noiseR = randomBipolar();
            float mixL = 0.0f, mixR = 0.0f;

            for (size_t s = 0; s < kNumSections; ++s)
            {
                const float level = params.sectionLevel[s];
                if (level < 0.001f)
                    continue;

                auto& sec = n.sections[s];
                float srcL = 0.0f, srcR = 0.0f;

                for (int v = 0; v < numSingers; ++v)
                {
                    auto& singer = sec.singers[(size_t) v];
                    if (singer.delay > 0)
                    {
                        --singer.delay;
                        continue;
                    }
                    const float dt = singer.inc;
                    singer.phase += dt;
                    if (singer.phase >= 1.0f)
                        singer.phase -= 1.0f;
                    const float saw = 2.0f * singer.phase - 1.0f - polyBlep (singer.phase, dt);
                    srcL += saw * singer.gainL;
                    srcR += saw * singer.gainR;
                }

                srcL = (srcL * singerNorm + noiseL * breath) * n.voiceCurrent;
                srcR = (srcR * singerNorm + noiseR * breath) * n.voiceCurrent;

                sec.tiltL += tiltCoef * (srcL - sec.tiltL);
                sec.tiltR += tiltCoef * (srcR - sec.tiltR);
                const float inL = sec.tiltL + brightness * (srcL - sec.tiltL);
                const float inR = sec.tiltR + brightness * (srcR - sec.tiltR);

                float sumL = 0.0f, sumR = 0.0f;
                for (size_t f = 0; f < kNumFormants; ++f)
                {
                    const auto& c = sec.coefs[f];
                    sumL += c.gain * sec.filtersL[f].bandpass (inL, c.a1, c.a2, c.a3);
                    sumR += c.gain * sec.filtersR[f].bandpass (inR, c.a1, c.a2, c.a3);
                }
                mixL += sumL * level;
                mixR += sumR * level;
            }

            const float consonant = n.noiseCurrent * params.consonants * kConsonantLevel;
            const float nL = n.noiseL.bandpass (noiseL, nc.a1, nc.a2, nc.a3) * nc.gain * consonant;
            const float nR = n.noiseR.bandpass (noiseR, nc.a1, nc.a2, nc.a3) * nc.gain * consonant;

            outL[i] += (mixL * n.env + nL) * n.velocity;
            outR[i] += (mixR * n.env + nR) * n.velocity;
        }

        if (n.released && ! n.stealing && n.env < 1.0e-4f && n.stage == Stage::done && n.noiseCurrent < 1.0e-4f)
            n.active = false;
    }

    //==========================================================================
    static constexpr float kMakeup = 3.0f;
    static constexpr float kConsonantLevel = 0.5f;

    float sr = 48000.0f, controlDt = kControlBlock / 48000.0f;
    float tiltCoef = 0.1f, rampCoef = 0.01f, glideCoef = 0.1f;
    float gainCurrent = 0.5f, gainTarget = 0.5f, wetCurrent = 0.3f;
    float pitchBend = 0.0f, modWheel = 0.0f;
    bool sustain = false;
    int controlCountdown = 0;
    int64_t clock = 0, lastTriggerClock = -1000000;
    uint32_t rngState = 0x12345678u;

    Params params;
    Reverb reverb;
    std::array<Note, kMaxNotes> notes;

    std::array<Syllable, kMaxSyllables> lyrics {};
    int numLyrics = 0;
    Syllable lastSyllable;
    std::atomic<int> lyricsPosition { 0 };
    std::atomic_flag lyricsLock = ATOMIC_FLAG_INIT;
};

} // namespace choir
