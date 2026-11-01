// Moteur audio de la Chorale : transforme une voix chantee en direct en choeur.
// Chaque chanteur rejoue des fragments de la voix (PSOLA) : les mots restent ceux du chanteur.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace choir
{
constexpr float kPi = 3.14159265358979f;
constexpr int kNumSections = 4;
constexpr int kMaxSingers = 8;
constexpr int kMaxGrains = 8;
constexpr int kRingSize = 1 << 17;
constexpr int kNumMarks = 128;

enum class Harmony { unison = 0, octaves = 1, chord = 2 };
enum class Scale { major = 0, minor = 1, none = 2 };

struct Params
{
    Harmony harmony = Harmony::chord;
    int key = 0; // 0 = Do
    Scale scale = Scale::major;
    std::array<float, kNumSections> sectionLevel { 0.8f, 0.8f, 0.8f, 0.8f }; // S A T B
    int singers = 4;
    float ensemble = 0.5f;
    float character = 1.0f; // ampleur des differences de timbre entre pupitres
    float choirLevel = 0.8f;
    float dryLevel = 0.7f;
    float gateDb = -50.0f;
    float reverbMix = 0.3f;
    float reverbSize = 0.7f;
    float gainDb = 0.0f;
};

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
// Decalage en demi-tons de chaque pupitre pour une note chantee.
inline int floorDiv (int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

inline int diatonicShift (int note, int key, Scale scale, int degrees)
{
    static const int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* steps = scale == Scale::minor ? minorSteps : majorSteps;

    const int pitchClass = ((note - key) % 12 + 12) % 12;
    int index = 0;
    for (int i = 0; i < 7; ++i)
        if (steps[i] <= pitchClass)
            index = i;

    const int target = index + degrees;
    const int octaves = floorDiv (target, 7);
    const int wrapped = target - octaves * 7;
    return steps[wrapped] - pitchClass + 12 * octaves;
}

inline std::array<int, kNumSections> sectionShifts (int note, const Params& p)
{
    if (p.harmony == Harmony::unison)
        return { 0, 0, 0, 0 };
    if (p.harmony == Harmony::octaves)
        return { 12, 0, 0, -12 };
    if (p.scale == Scale::none)
        return { 4, 0, -5, -12 };

    return { diatonicShift (note, p.key, p.scale, 2),
             0,
             diatonicShift (note, p.key, p.scale, -3),
             diatonicShift (note, p.key, p.scale, -7) };
}

//==============================================================================
class VoiceChoir
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        ring.assign ((size_t) kRingSize, 0.0f);
        writePos = 0;
        decimation = std::max (1, (int) std::lround (sr / 12000.0f));
        decimatedRate = sr / (float) decimation;
        decCount = 0;
        decSum = 0.0f;
        decFill = 0;
        decSinceAnalysis = 0;
        baseDelay = 0.02f * sr;
        unvoicedPeriod = 0.005f * sr;
        period = unvoicedPeriod;
        voiced = false;
        heldNote = -1;
        numMarks = 0;
        markHead = 0;
        nextMark = (int64_t) period;
        envelope = 0.0f;
        gate = 0.0f;
        attackCoef = 1.0f - std::exp (-1.0f / (0.002f * sr));
        releaseCoef = 1.0f - std::exp (-1.0f / (0.08f * sr));
        gainSmooth = 1.0f - std::exp (-1.0f / (0.02f * sr));
        gainCurrent = dbToGain (params.gainDb);
        detectedMidi.store (-1.0f);
        reverb.prepare (sampleRate);
        reverb.setSize (params.reverbSize);
        initialiseVoices();
    }

    void setParams (const Params& p)
    {
        params = p;
        reverb.setSize (p.reverbSize);
    }

    // Note detectee en MIDI, ou -1 s'il n'y a pas de note chantee.
    float getDetectedMidi() const { return detectedMidi.load(); }

    // Les tampons d'entree et de sortie peuvent etre les memes.
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples)
    {
        const int numSingers = std::clamp (params.singers, 1, kMaxSingers);
        const float sectionNorm = 1.0f / std::sqrt ((float) numSingers);
        const float gateLevel = dbToGain (params.gateDb);
        const float gainTarget = dbToGain (params.gainDb);

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = inL[n];
            const float dryR = inR[n];
            const float x = 0.5f * (dryL + dryR);

            ring[(size_t) (writePos & (kRingSize - 1))] = x;
            ++writePos;

            analyse (x);
            updateMarks();

            const float rectified = std::abs (x);
            envelope += (rectified > envelope ? attackCoef : releaseCoef) * (rectified - envelope);
            const float gateTarget = envelope > gateLevel ? 1.0f : 0.0f;
            gate += (gateTarget > gate ? attackCoef : releaseCoef) * (gateTarget - gate);

            float wetL = 0.0f, wetR = 0.0f;
            for (size_t s = 0; s < (size_t) kNumSections; ++s)
            {
                const float level = params.sectionLevel[s] * sectionNorm;
                for (int v = 0; v < numSingers; ++v)
                {
                    const float out = renderSinger (singers[s][(size_t) v], s, level);
                    wetL += out * singers[s][(size_t) v].gainL;
                    wetR += out * singers[s][(size_t) v].gainR;
                }
            }

            wetL *= params.choirLevel * gate;
            wetR *= params.choirLevel * gate;

            float roomL, roomR;
            reverb.process (wetL, wetR, roomL, roomR);

            gainCurrent += gainSmooth * (gainTarget - gainCurrent);
            const float dryGain = params.dryLevel;
            const float wetGain = 1.0f - 0.5f * params.reverbMix;
            outL[n] = std::tanh (gainCurrent * (dryL * dryGain + wetL * wetGain + roomL * params.reverbMix));
            outR[n] = std::tanh (gainCurrent * (dryR * dryGain + wetR * wetGain + roomR * params.reverbMix));
        }
    }

private:
    //==========================================================================
    struct Grain
    {
        bool active = false;
        double centre = 0.0;
        float index = 0.0f, length = 1.0f, inc = 1.0f, amp = 0.0f;
    };

    struct Singer
    {
        std::array<Grain, kMaxGrains> grains;
        float countdown = 0.0f, semitones = 0.0f;
        float detuneNorm = 0.0f, scatterNorm = 0.0f, timbreNorm = 0.0f;
        float drift = 0.0f, driftTarget = 0.0f;
        float gainL = 0.7f, gainR = 0.7f;
    };

    static float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

    float random01()
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (float) (rngState & 0xffffff) / 16777215.0f;
    }

    float randomBipolar() { return 2.0f * random01() - 1.0f; }

    void initialiseVoices()
    {
        static const float sectionPan[kNumSections] = { -0.25f, 0.25f, -0.1f, 0.1f };
        for (size_t s = 0; s < (size_t) kNumSections; ++s)
        {
            for (auto& singer : singers[s])
            {
                singer = Singer {};
                singer.countdown = random01() * period;
                singer.detuneNorm = randomBipolar();
                singer.scatterNorm = random01();
                singer.timbreNorm = randomBipolar();
                singer.driftTarget = randomBipolar();
                const float pan = std::clamp (sectionPan[s] + 0.7f * randomBipolar(), -1.0f, 1.0f);
                const float angle = (pan + 1.0f) * 0.25f * kPi;
                singer.gainL = std::cos (angle);
                singer.gainR = std::sin (angle);
            }
        }
    }

    //==========================================================================
    // Detection de la hauteur (YIN) sur un signal reduit a environ 12 kHz.
    void analyse (float x)
    {
        decSum += x;
        if (++decCount < decimation)
            return;

        const float sample = decSum / (float) decimation;
        decCount = 0;
        decSum = 0.0f;

        std::copy (decBuffer.begin() + 1, decBuffer.end(), decBuffer.begin());
        decBuffer.back() = sample;
        decFill = std::min (decFill + 1, (int) decBuffer.size());

        if (++decSinceAnalysis < 64 || decFill < (int) decBuffer.size())
            return;

        decSinceAnalysis = 0;
        detectPitch();
    }

    void detectPitch()
    {
        constexpr int window = 256;
        const int tauMin = std::max (2, (int) (decimatedRate / 1000.0f));
        const int tauMax = std::min ((int) (decimatedRate / 60.0f), (int) decBuffer.size() - window - 2);

        float energy = 0.0f;
        for (int i = 0; i < window; ++i)
            energy += decBuffer[(size_t) i] * decBuffer[(size_t) i];

        if (energy / (float) window < 1.0e-7f || envelope < dbToGain (params.gateDb))
        {
            setUnvoiced();
            return;
        }

        std::array<float, 300> cmnd {};
        float running = 0.0f;
        int best = -1;

        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            float d = 0.0f;
            for (int i = 0; i < window; ++i)
            {
                const float diff = decBuffer[(size_t) i] - decBuffer[(size_t) (i + tau)];
                d += diff * diff;
            }
            running += d;
            cmnd[(size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
        }

        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < 0.2f)
            {
                while (tau < tauMax && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }

        if (best < 0)
        {
            setUnvoiced();
            return;
        }

        const float a = cmnd[(size_t) (best - 1)], b = cmnd[(size_t) best], c = cmnd[(size_t) (best + 1)];
        const float denominator = a - 2.0f * b + c;
        const float offset = std::abs (denominator) > 1.0e-6f ? 0.5f * (a - c) / denominator : 0.0f;
        const float tau = (float) best + std::clamp (offset, -1.0f, 1.0f);

        const float hz = decimatedRate / tau;
        const float newPeriod = std::min (sr / hz, baseDelay);
        period = voiced ? period + 0.4f * (newPeriod - period) : newPeriod;
        voiced = true;

        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        if (heldNote < 0 || std::abs (midi - (float) heldNote) > 0.6f)
            heldNote = (int) std::lround (midi);
        detectedMidi.store (midi);
    }

    void setUnvoiced()
    {
        voiced = false;
        period = unvoicedPeriod;
        heldNote = -1;
        detectedMidi.store (-1.0f);
    }

    //==========================================================================
    // Reperes de periode, places sur les cretes de la voix pour garder les fragments en phase.
    void updateMarks()
    {
        const int reach = std::max (1, (int) (period * 0.25f));
        if (writePos < nextMark + reach)
            return;

        int64_t mark = nextMark;
        float peak = -1.0e9f;
        for (int64_t p = nextMark - reach; p <= nextMark + reach; ++p)
        {
            if (p < 0)
                continue;
            const float value = ring[(size_t) (p & (kRingSize - 1))];
            if (value > peak)
            {
                peak = value;
                mark = p;
            }
        }

        marks[(size_t) markHead] = mark;
        markHead = (markHead + 1) % kNumMarks;
        numMarks = std::min (numMarks + 1, kNumMarks);
        nextMark = mark + std::max<int64_t> (1, (int64_t) period);
    }

    bool findMark (int64_t target, int64_t& result) const
    {
        for (int i = 1; i <= numMarks; ++i)
        {
            const auto m = marks[(size_t) ((markHead - i + kNumMarks) % kNumMarks)];
            if (m <= target)
            {
                result = m;
                return true;
            }
        }
        return false;
    }

    //==========================================================================
    void spawnGrain (Singer& singer, size_t section)
    {
        static const float sectionTimbre[kNumSections] = { 1.15f, 1.05f, 0.97f, 0.88f };

        int targetSemis = 0;
        if (voiced && heldNote >= 0)
            targetSemis = sectionShifts (heldNote, params)[section];
        singer.semitones += 0.35f * ((float) targetSemis - singer.semitones);

        singer.drift += 0.05f * (singer.driftTarget - singer.drift);
        if (random01() < 0.02f)
            singer.driftTarget = randomBipolar();

        const float cents = voiced ? params.ensemble * (22.0f * singer.detuneNorm + 10.0f * singer.drift) : 0.0f;
        const float ratio = voiced ? std::pow (2.0f, (singer.semitones + cents * 0.01f) / 12.0f) : 1.0f;

        const float spacing = period / ratio;
        singer.countdown += spacing;

        const float scatter = params.ensemble * singer.scatterNorm * 0.03f * sr;
        int64_t centre = 0;
        if (gate < 0.001f || ! findMark (writePos - (int64_t) (baseDelay + scatter), centre))
            return;

        const float length = 2.0f * period;
        const float timbre = 1.0f + params.character * (sectionTimbre[section] - 1.0f + 0.03f * singer.timbreNorm);
        if ((double) centre - (double) length * timbre < 0.0)
            return;

        for (auto& grain : singer.grains)
        {
            if (! grain.active)
            {
                grain.active = true;
                grain.centre = (double) centre;
                grain.index = 0.0f;
                grain.length = length;
                grain.inc = timbre;
                grain.amp = 1.0f / std::max (ratio, 0.5f);
                return;
            }
        }
    }

    float renderSinger (Singer& singer, size_t section, float level)
    {
        singer.countdown -= 1.0f;
        if (singer.countdown <= 0.0f)
            spawnGrain (singer, section);

        if (level < 1.0e-4f)
        {
            for (auto& grain : singer.grains)
                grain.active = false;
            return 0.0f;
        }

        float out = 0.0f;
        for (auto& grain : singer.grains)
        {
            if (! grain.active)
                continue;

            const float t = grain.index / grain.length;
            const float window = 0.5f - 0.5f * std::cos (2.0f * kPi * t);
            const double position = grain.centre + (double) ((grain.index - 0.5f * grain.length) * grain.inc);
            const auto whole = (int64_t) std::floor (position);
            const auto fraction = (float) (position - (double) whole);
            const float a = ring[(size_t) (whole & (kRingSize - 1))];
            const float b = ring[(size_t) ((whole + 1) & (kRingSize - 1))];
            out += (a + fraction * (b - a)) * window * grain.amp;

            grain.index += 1.0f;
            if (grain.index >= grain.length)
                grain.active = false;
        }
        return out * level;
    }

    //==========================================================================
    float sr = 48000.0f, decimatedRate = 12000.0f;
    int decimation = 4, decCount = 0, decFill = 0, decSinceAnalysis = 0;
    float decSum = 0.0f;
    std::array<float, 512> decBuffer {};

    std::vector<float> ring;
    int64_t writePos = 0, nextMark = 0;
    std::array<int64_t, kNumMarks> marks {};
    int markHead = 0, numMarks = 0;

    float baseDelay = 960.0f, unvoicedPeriod = 240.0f, period = 240.0f;
    bool voiced = false;
    int heldNote = -1;
    std::atomic<float> detectedMidi { -1.0f };

    float envelope = 0.0f, gate = 0.0f, attackCoef = 0.1f, releaseCoef = 0.01f;
    float gainCurrent = 1.0f, gainSmooth = 0.01f;
    uint32_t rngState = 0x12345678u;

    Params params;
    Reverb reverb;
    std::array<std::array<Singer, kMaxSingers>, kNumSections> singers;
};

} // namespace choir
