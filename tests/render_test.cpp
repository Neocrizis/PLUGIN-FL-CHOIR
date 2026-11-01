// Test du moteur sans JUCE : envoie une voix de synthese dans le choeur, verifie le resultat
// et ecrit des WAV d'ecoute.
#include "../Source/VoiceChoir.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

using namespace choir;

static int failures = 0;
static const double kSr = 48000.0;

static void check (bool ok, const std::string& name, const std::string& detail = "")
{
    std::printf ("[%s] %s %s\n", ok ? "OK" : "ECHEC", name.c_str(), detail.c_str());
    if (! ok)
        ++failures;
}

// Voix simple : dent de scie filtree, avec une enveloppe de syllabes optionnelle.
static std::vector<float> makeVoice (float hz, double seconds, bool syllables)
{
    std::vector<float> s ((size_t) (seconds * kSr));
    float phase = 0, lp = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        phase += hz / (float) kSr;
        if (phase >= 1.0f) phase -= 1.0f;
        const float saw = 2.0f * phase - 1.0f;
        lp += 0.25f * (saw - lp);
        float env = 0.3f;
        if (syllables)
        {
            const double t = (double) i / kSr;
            env *= std::fmod (t, 0.5) < 0.3 ? 1.0f : 0.0f;
        }
        s[i] = lp * env;
    }
    return s;
}

static std::vector<float> run (VoiceChoir& e, const std::vector<float>& in)
{
    std::vector<float> out (in.size() * 2);
    std::vector<float> l (256), r (256);
    for (size_t pos = 0; pos < in.size(); pos += 256)
    {
        const int n = (int) std::min<size_t> (256, in.size() - pos);
        e.process (in.data() + pos, in.data() + pos, l.data(), r.data(), n);
        for (int i = 0; i < n; ++i)
        {
            out[(pos + (size_t) i) * 2] = l[(size_t) i];
            out[(pos + (size_t) i) * 2 + 1] = r[(size_t) i];
        }
    }
    return out;
}

static std::unique_ptr<VoiceChoir> makeEngine (const Params& p)
{
    auto e = std::make_unique<VoiceChoir>();
    e->setParams (p);
    e->prepare (kSr);
    return e;
}

static Params soloSection (int section)
{
    Params p;
    p.harmony = Harmony::chord;
    p.sectionLevel = { 0, 0, 0, 0 };
    p.sectionLevel[(size_t) section] = 1.0f;
    p.singers = 1;
    p.ensemble = 0;
    p.character = 0;
    p.dryLevel = 0;
    p.reverbMix = 0;
    return p;
}

static float detectHz (const std::vector<float>& s, size_t from, size_t count)
{
    float best = 0, bestLag = 1;
    std::vector<float> c ((size_t) (kSr / 50) + 2);
    for (int lag = 48; lag < (int) (kSr / 50); ++lag)
    {
        float sum = 0;
        for (size_t i = 0; i < count; ++i)
            sum += s[(from + i) * 2] * s[(from + i + (size_t) lag) * 2];
        c[(size_t) lag] = sum;
        best = std::max (best, sum);
    }
    // Premier maximum proche du meilleur, pour eviter les multiples de la periode.
    for (int lag = 49; lag < (int) (kSr / 50) - 1; ++lag)
    {
        if (c[(size_t) lag] > 0.9f * best && c[(size_t) lag] >= c[(size_t) lag - 1] && c[(size_t) lag] >= c[(size_t) lag + 1])
        {
            bestLag = (float) lag;
            break;
        }
    }
    return (float) kSr / bestLag;
}

static float rms (const std::vector<float>& s, size_t fromFrame, size_t toFrame)
{
    double sum = 0;
    for (size_t i = fromFrame * 2; i < toFrame * 2; ++i)
        sum += s[i] * s[i];
    return (float) std::sqrt (sum / (double) std::max<size_t> (1, (toFrame - fromFrame) * 2));
}

static bool allFinite (const std::vector<float>& s)
{
    for (float x : s)
        if (! std::isfinite (x))
            return false;
    return true;
}

static float peak (const std::vector<float>& s)
{
    float p = 0;
    for (float x : s)
        p = std::max (p, std::abs (x));
    return p;
}

static void writeWav (const std::string& path, const std::vector<float>& s, int sr)
{
    std::ofstream f (path, std::ios::binary);
    const auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    const auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t bytes = (uint32_t) s.size() * 2;
    f.write ("RIFF", 4); u32 (36 + bytes); f.write ("WAVEfmt ", 8); u32 (16); u16 (1); u16 (2);
    u32 ((uint32_t) sr); u32 ((uint32_t) sr * 4); u16 (4); u16 (16); f.write ("data", 4); u32 (bytes);
    for (float x : s)
    {
        const auto v = (int16_t) (std::clamp (x, -1.0f, 1.0f) * 32767.0f);
        f.write ((const char*) &v, 2);
    }
}

static void checkPitch (const char* name, int section, Harmony harmony, float expected)
{
    auto p = soloSection (section);
    p.harmony = harmony;
    p.key = 0;
    p.scale = Scale::major;
    auto e = makeEngine (p);
    const auto out = run (*e, makeVoice (220.0f, 1.5, false));
    const float hz = detectHz (out, 48000, 4096);
    check (std::abs (hz / expected - 1.0f) < 0.03f, name, std::to_string (hz) + " Hz, attendu " + std::to_string (expected));
}

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";

    // --- Harmonies : Do majeur, La3 chante (220 Hz, note 57)
    check (diatonicShift (60, 0, Scale::major, 2) == 4, "tierce de Do = 4 demi-tons");
    check (diatonicShift (62, 0, Scale::major, 2) == 3, "tierce de Re = 3 demi-tons");
    check (diatonicShift (60, 0, Scale::major, -7) == -12, "octave basse");
    check (diatonicShift (60, 9, Scale::minor, 2) == 4, "Do en La mineur");

    checkPitch ("unisson", 1, Harmony::unison, 220.0f);
    checkPitch ("octave haute", 0, Harmony::octaves, 440.0f);
    checkPitch ("octave basse", 3, Harmony::octaves, 110.0f);
    checkPitch ("tierce au-dessus du La", 0, Harmony::chord, 261.63f);

    // --- La note est reconnue
    {
        auto e = makeEngine (soloSection (1));
        run (*e, makeVoice (220.0f, 0.5, false));
        check (std::abs (e->getDetectedMidi() - 57.0f) < 0.3f, "note detectee", std::to_string (e->getDetectedMidi()));
    }

    // --- Le choeur suit les syllabes chantees : silence entre deux syllabes
    {
        Params p;
        p.reverbMix = 0;
        p.dryLevel = 0;
        auto e = makeEngine (p);
        const auto out = run (*e, makeVoice (220.0f, 2.5, true));
        const float during = rms (out, 48000 + 4800, 48000 + 12000);
        const float between = rms (out, 48000 + 21000, 48000 + 23500);
        check (during > 0.02f && between < during * 0.1f, "le choeur suit les syllabes", std::to_string (during) + " / " + std::to_string (between));
    }

    // --- Le silence reste silencieux, meme avec du souffle sous le seuil
    {
        Params p;
        p.reverbMix = 0;
        auto e = makeEngine (p);
        std::vector<float> quiet (48000);
        uint32_t seed = 1;
        for (auto& x : quiet)
        {
            seed = seed * 1664525u + 1013904223u;
            x = ((float) (seed >> 16) / 32768.0f - 1.0f) * 1.0e-4f;
        }
        p.dryLevel = 0;
        e->setParams (p);
        const auto out = run (*e, quiet);
        check (peak (out) < 1.0e-3f, "bruit de fond sous le seuil", std::to_string (peak (out)));
    }

    // --- Consonne (bruit) : elle passe dans le choeur sans changer de hauteur
    {
        Params p = soloSection (1);
        auto e = makeEngine (p);
        std::vector<float> noise (48000);
        uint32_t seed = 7;
        for (auto& x : noise)
        {
            seed = seed * 1664525u + 1013904223u;
            x = ((float) (seed >> 16) / 32768.0f - 1.0f) * 0.1f;
        }
        const auto out = run (*e, noise);
        check (rms (out, 24000, 48000) > 0.01f, "les consonnes passent", std::to_string (rms (out, 24000, 48000)));
    }

    // --- Consonne : plus de bourdonnement regulier cree par les fragments
    {
        Params p = soloSection (1);
        p.singers = 5;
        p.ensemble = 0.6f;
        auto e = makeEngine (p);
        std::vector<float> noise (48000);
        uint32_t seed = 11;
        for (auto& x : noise)
        {
            seed = seed * 1664525u + 1013904223u;
            x = ((float) (seed >> 16) / 32768.0f - 1.0f) * 0.1f;
        }
        const auto out = run (*e, noise);
        double same = 0, energy = 0;
        for (size_t i = 24000; i < 47000; ++i)
        {
            same += out[i * 2] * out[(i + 240) * 2];
            energy += out[i * 2] * out[i * 2];
        }
        const double periodicity = energy > 0 ? same / energy : 1.0;
        check (std::abs (periodicity) < 0.3, "consonnes sans bourdonnement", std::to_string (periodicity));
    }

    // --- Les chanteurs doivent differer : gauche et droite ne sont pas une copie
    {
        Params p;
        p.dryLevel = 0;
        p.reverbMix = 0;
        auto e = makeEngine (p);
        const auto out = run (*e, makeVoice (220.0f, 3.0, false));
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = 48000; i < 144000; ++i)
        {
            lr += out[i * 2] * out[i * 2 + 1];
            ll += out[i * 2] * out[i * 2];
            rr += out[i * 2 + 1] * out[i * 2 + 1];
        }
        const double correlation = lr / std::sqrt (std::max (ll * rr, 1.0e-12));
        check (correlation < 0.8, "chanteurs decorreles", std::to_string (correlation));
    }

    // --- Styles : stabilite et niveau
    for (Style style : { Style::gospel, Style::kids, Style::shout })
    {
        Params p;
        p.style = style;
        auto e = makeEngine (p);
        const auto out = run (*e, makeVoice (220.0f, 1.5, true));
        check (allFinite (out) && peak (out) <= 1.0f && rms (out, 0, 72000) > 0.01f,
               "style " + std::to_string ((int) style), "rms=" + std::to_string (rms (out, 0, 72000)));
    }

    // --- Choeur complet : niveau, stabilite, fichier d'ecoute
    {
        Params p;
        p.singers = 4;
        p.dryLevel = 0.7f;
        auto e = makeEngine (p);
        std::vector<float> in;
        for (float hz : { 196.0f, 220.0f, 261.63f, 293.66f, 329.63f })
        {
            const auto part = makeVoice (hz, 1.0, true);
            in.insert (in.end(), part.begin(), part.end());
        }
        const auto out = run (*e, in);
        writeWav (outDir + "/choeur.wav", out, (int) kSr);
        check (allFinite (out), "aucun NaN");
        check (peak (out) <= 1.0f && rms (out, 0, in.size()) > 0.02f, "niveau du choeur", "rms=" + std::to_string (rms (out, 0, in.size())) + " crete=" + std::to_string (peak (out)));
    }

    // --- Autres frequences d'echantillonnage
    for (double rate : { 44100.0, 96000.0 })
    {
        auto e = std::make_unique<VoiceChoir>();
        e->setParams (Params {});
        e->prepare (rate);
        const auto out = run (*e, makeVoice (220.0f, 1.0, false));
        check (allFinite (out) && peak (out) <= 1.0f, "stable a " + std::to_string ((int) rate) + " Hz");
    }

    // --- Charge CPU au pire cas
    {
        Params p;
        p.singers = kMaxSingers;
        auto e = makeEngine (p);
        const auto in = makeVoice (110.0f, 5.0, false);
        const auto start = std::chrono::steady_clock::now();
        run (*e, in);
        const double used = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        check (used < 5.0, "temps reel au pire cas", std::to_string (used / 5.0 * 100.0) + " % d'un coeur");
    }

    std::printf (failures == 0 ? "\nTous les tests passent\n" : "\n%d test(s) en echec\n", failures);
    return failures == 0 ? 0 : 1;
}
