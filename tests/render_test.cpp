// Test du moteur sans JUCE : verifie les paroles et le signal, puis ecrit des WAV d'ecoute.
#include "../Source/ChoirEngine.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>

using namespace choir;

static int failures = 0;

static void check (bool ok, const std::string& name, const std::string& detail = "")
{
    std::printf ("[%s] %s %s\n", ok ? "OK" : "ECHEC", name.c_str(), detail.c_str());
    if (! ok)
        ++failures;
}

static std::string join (const std::vector<Syllable>& v)
{
    std::string s;
    for (const auto& x : v)
        s += (s.empty() ? "" : "|") + std::string (x.text);
    return s;
}

struct Event { double time; int note; bool on; };

static std::vector<float> renderEvents (ChoirEngine& e, const std::vector<Event>& events, double seconds, double sr)
{
    const int total = (int) (seconds * sr);
    std::vector<float> out ((size_t) total * 2);
    std::vector<float> l (256), r (256);
    size_t next = 0;
    for (int pos = 0; pos < total; pos += 256)
    {
        const int n = std::min (256, total - pos);
        while (next < events.size() && events[next].time * sr <= pos)
        {
            if (events[next].on) e.noteOn (events[next].note, 0.8f);
            else e.noteOff (events[next].note);
            ++next;
        }
        e.render (l.data(), r.data(), n);
        for (int i = 0; i < n; ++i)
        {
            out[(size_t) (pos + i) * 2] = l[(size_t) i];
            out[(size_t) (pos + i) * 2 + 1] = r[(size_t) i];
        }
    }
    return out;
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

static float peak (const std::vector<float>& s, size_t from, size_t to)
{
    float p = 0;
    for (size_t i = from; i < to && i < s.size(); ++i)
        p = std::max (p, std::abs (s[i]));
    return p;
}

static float rms (const std::vector<float>& s, size_t from, size_t to)
{
    double sum = 0;
    for (size_t i = from; i < to; ++i)
        sum += s[i] * s[i];
    return (float) std::sqrt (sum / (double) std::max<size_t> (1, to - from));
}

static bool allFinite (const std::vector<float>& s)
{
    for (float x : s)
        if (! std::isfinite (x))
            return false;
    return true;
}

// Frequence dominante par autocorrelation sur le canal gauche.
static float detectPitch (const std::vector<float>& s, size_t from, size_t count, float sr)
{
    float best = 0, bestLag = 1;
    for (int lag = (int) (sr / 1000); lag < (int) (sr / 60); ++lag)
    {
        float c = 0;
        for (size_t i = 0; i < count; ++i)
            c += s[(from + i) * 2] * s[(from + i + (size_t) lag) * 2];
        if (c > best) { best = c; bestLag = (float) lag; }
    }
    return sr / bestLag;
}

// Part d'energie au-dessus de 1200 Hz, mesuree par difference avec un passe-bas.
static float highRatio (const std::vector<float>& s, size_t from, size_t to, float sr)
{
    const float a = 1.0f - std::exp (-2.0f * kPi * 1200.0f / sr);
    float lp = 0;
    double hi = 0, all = 0;
    for (size_t i = from; i < to; ++i)
    {
        lp += a * (s[i * 2] - lp);
        const float h = s[i * 2] - lp;
        hi += h * h;
        all += s[i * 2] * s[i * 2];
    }
    return (float) (hi / std::max (1.0e-12, all));
}

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    const double sr = 48000.0;

    // --- Decoupage des paroles
    check (join (parseLyrics ("Glo-ri-a")) == "glo|ri|a", "tirets", join (parseLyrics ("Glo-ri-a")));
    check (join (parseLyrics ("Gloria")) == "glo|ri|a", "decoupage auto", join (parseLyrics ("Gloria")));
    check (join (parseLyrics ("Amen")) == "a|men", "coda", join (parseLyrics ("Amen")));
    check (join (parseLyrics ("excelsis")) == "ex|cel|sis", "VCCV", join (parseLyrics ("excelsis")));
    check (join (parseLyrics ("\xC3\xA9t\xC3\xA9 chanson")) == "e|te|chan|son", "accents et ch", join (parseLyrics ("\xC3\xA9t\xC3\xA9 chanson")));
    check (join (parseLyrics ("l'amour, toujours!")) == "l|a|mour|tou|jours" || join (parseLyrics ("l'amour, toujours!")) == "a|mour|tou|jours",
           "ponctuation", join (parseLyrics ("l'amour, toujours!")));
    check (parseLyrics ("").empty() && parseLyrics ("123 !!").empty(), "texte vide");

    auto engine = std::make_unique<ChoirEngine>();
    engine->prepare (sr);
    Params p;
    p.reverbMix = 0.0f;
    engine->setParams (p);

    // --- Chaque note avance d'une syllabe, un accord n'en consomme qu'une
    engine->setLyrics (parseLyrics ("a-e-i"));
    float dummyL[256], dummyR[256];
    engine->noteOn (60, 0.8f); engine->noteOn (64, 0.8f); engine->noteOn (67, 0.8f);
    check (engine->getLyricsPosition() == 1, "accord = une syllabe");
    for (int i = 0; i < 20; ++i) engine->render (dummyL, dummyR, 256);
    engine->noteOn (62, 0.8f);
    check (engine->getLyricsPosition() == 2, "note suivante = syllabe suivante");
    engine->allNotesOff();

    // --- Hauteur : une seule section, sans vibrato ni desaccord
    {
        auto e = std::make_unique<ChoirEngine>();
        e->prepare (sr);
        Params q;
        q.reverbMix = 0; q.sectionLevel = { 0, 0, 1, 0 }; q.singers = 1; q.ensemble = 0; q.vibratoDepth = 0; q.breath = 0; q.autoOctave = false;
        e->setParams (q);
        e->setLyrics (parseLyrics ("a"));
        auto s = renderEvents (*e, { { 0, 57, true } }, 1.0, sr);
        const float hz = detectPitch (s, 24000, 4096, (float) sr);
        check (std::abs (hz - 220.0f) < 4.0f, "justesse La3", std::to_string (hz) + " Hz");
    }

    // --- Les voyelles sont bien differentes
    {
        const auto vowelRatio = [&] (const char* text)
        {
            auto e = std::make_unique<ChoirEngine>();
            e->prepare (sr);
            Params q;
            q.reverbMix = 0; q.sectionLevel = { 0, 0, 1, 0 }; q.autoOctave = false; q.vibratoDepth = 0;
            e->setParams (q);
            e->setLyrics (parseLyrics (text));
            auto s = renderEvents (*e, { { 0, 57, true } }, 1.0, sr);
            return highRatio (s, 24000, 40000, (float) sr);
        };
        const float a = vowelRatio ("a"), ou = vowelRatio ("ou");
        check (a > ou * 1.5f, "voyelles A et OU distinctes", std::to_string (a) + " / " + std::to_string (ou));
    }

    // --- Un S produit du souffle aigu avant la voyelle
    {
        auto e = std::make_unique<ChoirEngine>();
        e->prepare (sr);
        Params q;
        q.reverbMix = 0; q.consonants = 1;
        e->setParams (q);
        e->setLyrics (parseLyrics ("sa"));
        auto s = renderEvents (*e, { { 0, 57, true } }, 1.0, sr);
        const float consonant = highRatio (s, 1500, 3500, (float) sr);
        const float vowel = highRatio (s, 24000, 40000, (float) sr);
        check (consonant > vowel * 1.5f && rms (s, 3000, 7000) > 0.002f, "consonne S audible", std::to_string (consonant) + " / " + std::to_string (vowel));
    }

    // --- Chant complet, niveau et retour au silence
    {
        auto e = std::make_unique<ChoirEngine>();
        e->prepare (sr);
        Params q;
        e->setParams (q);
        e->setLyrics (parseLyrics ("Glo-ri-a in ex-cel-sis De-o"));
        const int melody[][4] = { { 60, 64, 67, 72 }, { 60, 65, 69, 72 }, { 62, 65, 67, 71 }, { 60, 64, 67, 72 },
                                  { 59, 62, 67, 74 }, { 60, 64, 69, 72 }, { 60, 65, 69, 77 }, { 62, 65, 67, 71 }, { 60, 64, 67, 72 } };
        std::vector<Event> ev;
        double t = 0.1;
        for (const auto& chord : melody)
        {
            for (int n : chord) ev.push_back ({ t, n, true });
            for (int n : chord) ev.push_back ({ t + 0.62, n, false });
            t += 0.68;
        }
        std::stable_sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b) { return a.time < b.time; });
        const auto s = renderEvents (*e, ev, t + 3.0, sr);
        writeWav (outDir + "/gloria.wav", s, (int) sr);
        const float level = rms (s, 96000, 400000);
        check (allFinite (s), "aucun NaN");
        check (level > 0.03f && level < 0.5f, "niveau du chant", "rms=" + std::to_string (level) + " crete=" + std::to_string (peak (s, 0, s.size())));
    }

    {
        auto e = std::make_unique<ChoirEngine>();
        e->prepare (sr);
        Params q;
        q.reverbMix = 0;
        e->setParams (q);
        e->setLyrics (parseLyrics ("sal"));
        auto s = renderEvents (*e, { { 0, 60, true }, { 0.5, 60, false } }, 6.0, sr);
        check (peak (s, s.size() - 48000, s.size()) < 1.0e-3f, "silence apres relachement", std::to_string (peak (s, s.size() - 48000, s.size())));
    }

    // --- Vol de voix : 40 notes rapides, sans clic ni erreur
    {
        auto e = std::make_unique<ChoirEngine>();
        e->prepare (sr);
        Params q;
        q.singers = 8;
        e->setParams (q);
        e->setLyrics (parseLyrics ("ka-ti-pou-so"));
        std::vector<Event> ev;
        for (int i = 0; i < 40; ++i)
            ev.push_back ({ i * 0.05, 40 + i, true });
        const auto s = renderEvents (*e, ev, 3.0, sr);
        check (allFinite (s) && peak (s, 0, s.size()) <= 1.0f, "40 notes sans erreur");
    }

    // --- Charge CPU au pire cas
    {
        auto e = std::make_unique<ChoirEngine>();
        e->prepare (sr);
        Params q;
        q.singers = 8;
        e->setParams (q);
        e->setLyrics (parseLyrics ("a"));
        std::vector<Event> ev;
        for (int i = 0; i < kMaxNotes; ++i)
            ev.push_back ({ 0.0, 48 + i, true });
        const auto start = std::chrono::steady_clock::now();
        const auto s = renderEvents (*e, ev, 5.0, sr);
        const double used = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        check (used < 5.0, "temps reel au pire cas", std::to_string (used / 5.0 * 100.0) + " % d'un coeur");
    }

    std::printf (failures == 0 ? "\nTous les tests passent\n" : "\n%d test(s) en echec\n", failures);
    return failures == 0 ? 0 : 1;
}
