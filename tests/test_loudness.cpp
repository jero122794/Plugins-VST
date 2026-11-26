// Validación del medidor contra los casos sintetizables de EBU Tech 3341 (loudness,
// true peak) y EBU Tech 3342 (LRA). Tolerancias las del propio estándar.
#include <dspcore/Loudness.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace dspcore;

static int failures = 0;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (! (cond)) {                                                     \
            ++failures;                                                     \
            std::printf ("  FAIL %s:%d: %s\n    ", __FILE__, __LINE__, #cond); \
            std::printf (__VA_ARGS__);                                      \
            std::printf ("\n");                                             \
        }                                                                   \
    } while (0)

constexpr double fs = 48000.0;
constexpr double pi = 3.14159265358979323846;

/** Secuencia de segmentos (dBFS de pico, segundos) de seno a 1 kHz, en estéreo. */
static void feedSine (LoudnessMeter& m, const std::vector<std::pair<double, double>>& segments,
                      double freq = 1000.0, double phaseDeg = 0.0, double linearAmp = -1.0)
{
    std::vector<float> l (1024), r (1024);
    long long t = 0;
    for (auto [db, sec] : segments)
    {
        const double amp = linearAmp > 0 ? linearAmp : std::pow (10.0, db / 20.0);
        long long remaining = (long long) std::llround (sec * fs);
        while (remaining > 0)
        {
            const int n = (int) std::min<long long> (remaining, 1024);
            for (int i = 0; i < n; ++i, ++t)
            {
                // Fundido de entrada de 20 ms: un arranque en escalón provoca sobreimpulso de Gibbs
                // en cualquier interpolador y no forma parte de las señales de prueba.
                const double fade = t < (long long) (0.02 * fs) ? 0.5 - 0.5 * std::cos (pi * (double) t / (0.02 * fs)) : 1.0;
                l[(size_t) i] = r[(size_t) i] = (float) (fade * amp * std::sin (2.0 * pi * freq * (double) t / fs + phaseDeg * pi / 180.0));
            }
            const float* ch[] = { l.data(), r.data() };
            m.process (ch, n);
            remaining -= n;
        }
    }
}


static void tech3341Loudness()
{
    struct Case { const char* name; std::vector<std::pair<double, double>> seg; double expectI; bool checkMS; };
    const std::vector<Case> cases = {
        { "3341 #1: -23 dBFS 20 s",               { { -23, 20 } }, -23.0, true },
        { "3341 #2: -33 dBFS 20 s",               { { -33, 20 } }, -33.0, true },
        { "3341 #3: -36/-23/-36",                 { { -36, 10 }, { -23, 60 }, { -36, 10 } }, -23.0, false },
        { "3341 #4: -72/-36/-23/-36/-72",         { { -72, 10 }, { -36, 10 }, { -23, 60 }, { -36, 10 }, { -72, 10 } }, -23.0, false },
        { "3341 #5: -26/-20/-26",                 { { -26, 20 }, { -20, 20.1 }, { -26, 20 } }, -23.0, false },
    };
    for (auto& c : cases)
    {
        LoudnessMeter m; m.prepare (fs, 2);
        feedSine (m, c.seg);
        const double I = m.getIntegrated();
        std::printf ("    %-34s I = %7.2f LUFS (esperado %.1f)", c.name, I, c.expectI);
        if (c.checkMS) std::printf ("  M = %.2f  S = %.2f", m.getMomentary(), m.getShortTerm());
        std::printf ("\n");
        CHECK (std::fabs (I - c.expectI) <= 0.1, "%s", c.name);
        if (c.checkMS)
        {
            CHECK (std::fabs (m.getMomentary() - c.expectI) <= 0.1, "momentary %s", c.name);
            CHECK (std::fabs (m.getShortTerm() - c.expectI) <= 0.1, "short-term %s", c.name);
        }
    }
}

static void monoChannel()
{
    // BS.1770: un seno de 1 kHz a 0 dBFS en un solo canal mide -3.01 LUFS
    LoudnessMeter m; m.prepare (fs, 1);
    feedSine (m, { { 0.0, 10 } });
    std::printf ("    mono 0 dBFS: %.2f LUFS (esperado -3.01)\n", m.getIntegrated());
    CHECK (std::fabs (m.getIntegrated() + 3.01) <= 0.1, "calibración en mono");
}

static void tech3341TruePeak()
{
    struct Case { const char* name; double freq, phase, amp, expectDb; };
    const std::vector<Case> cases = {
        { "3341 #15: fs/4, 0°,    0.50 FS", fs / 4, 0.0,  0.5,   -6.0 },
        { "3341 #16: fs/4, 45°,   0.50 FS", fs / 4, 45.0, 0.5,   -6.0 },
        { "3341 #17: fs/6, 60°,   0.50 FS", fs / 6, 60.0, 0.5,   -6.0 },
        { "3341 #18: fs/8, 67.5°, 0.50 FS", fs / 8, 67.5, 0.5,   -6.0 },
        { "3341 #19: fs/4, 45°,   1.41 FS", fs / 4, 45.0, 1.4125, 3.0 },
    };
    for (auto& c : cases)
    {
        LoudnessMeter m; m.prepare (fs, 2);
        feedSine (m, { { 0, 2.0 } }, c.freq, c.phase, c.amp);
        const double tp = m.getTruePeakDb(), sp = m.getSamplePeakDb();
        std::printf ("    %-34s TP %+6.2f dBTP (sample peak %+6.2f)\n", c.name, tp, sp);
        CHECK (tp - c.expectDb <= 0.2 && tp - c.expectDb >= -0.4, "%s (%.2f)", c.name, tp);
    }

    // Más allá del estándar: seno de 19 kHz (cerca de Nyquist) con fase desfavorable
    LoudnessMeter m; m.prepare (fs, 2);
    feedSine (m, { { 0, 2.0 } }, 19000.0, 37.0, 0.5);
    std::printf ("    19 kHz, 0.50 FS:                   TP %+6.2f dBTP\n", m.getTruePeakDb());
    CHECK (m.getTruePeakDb() + 6.0 <= 0.2 && m.getTruePeakDb() + 6.0 >= -0.6, "true peak a 19 kHz");
}

static void tech3342Lra()
{
    struct Case { const char* name; std::vector<std::pair<double, double>> seg; double expect; };
    const std::vector<Case> cases = {
        { "3342 #1: -20 / -30",          { { -20, 20 }, { -30, 20 } }, 10.0 },
        { "3342 #2: -20 / -15",          { { -20, 20 }, { -15, 20 } }, 5.0 },
        { "3342 #3: -40 / -20",          { { -40, 20 }, { -20, 20 } }, 20.0 },
        { "3342 #4: -50/-35/-20/-35/-50", { { -50, 20 }, { -35, 20 }, { -20, 20 }, { -35, 20 }, { -50, 20 } }, 15.0 },
    };
    for (auto& c : cases)
    {
        LoudnessMeter m; m.prepare (fs, 2);
        feedSine (m, c.seg);
        const double lra = m.getLoudnessRange();
        std::printf ("    %-34s LRA = %5.2f LU (esperado %.0f ±1)\n", c.name, lra, c.expect);
        CHECK (std::fabs (lra - c.expect) <= 1.0, "%s", c.name);
    }
}

static void silenceAndReset()
{
    LoudnessMeter m; m.prepare (fs, 2);
    feedSine (m, { { -100, 5 } });
    CHECK (! std::isfinite (m.getIntegrated()), "silencio: integrado indefinido (-inf)");
    feedSine (m, { { -23, 5 } });
    m.reset();
    CHECK (! std::isfinite (m.getIntegrated()) && m.getTruePeakDb() < -200.0, "reset limpia todo");
}

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        { "EBU Tech 3341: loudness", tech3341Loudness },
        { "calibración mono", monoChannel },
        { "EBU Tech 3341: true peak", tech3341TruePeak },
        { "EBU Tech 3342: LRA", tech3342Lra },
        { "silencio y reset", silenceAndReset },
    };
    for (auto& [name, fn] : tests)
    {
        const int before = failures;
        std::printf ("[ %s ]\n", name.c_str());
        fn();
        std::printf ("  %s\n", failures == before ? "ok" : "FALLÓ");
    }
    std::printf ("\n%s (%d fallos)\n", failures ? "TESTS FALLIDOS" : "TODOS LOS TESTS PASAN", failures);
    return failures ? 1 : 0;
}
