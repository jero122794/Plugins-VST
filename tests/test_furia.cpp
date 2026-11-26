// Tests de Furia: curvas de ratio, tiempos, modo Opto, detector, distorsión y estabilidad.
#include <dspcore/Furia.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace dspcore;
using R = FuriaCompressor::Ratio;

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
static float lin (float db) { return std::pow (10.0f, db / 20.0f); }

/** Ejecuta un seno y devuelve la GR por bloque de 32 muestras. */
static std::vector<float> runSine (FuriaCompressor& c, std::vector<std::pair<float, double>> segs, double f = 1000.0)
{
    std::vector<float> grs;
    long long t = 0;
    float buf[2][32];
    for (auto [db, sec] : segs)
        for (long long k = 0; k < (long long) (sec * fs); k += 32)
        {
            for (int i = 0; i < 32; ++i, ++t) buf[0][i] = buf[1][i] = lin (db) * (float) std::sin (2 * pi * f * (double) t / fs);
            float* ch[] = { buf[0], buf[1] };
            c.processDynamics (ch, 2, 32);
            grs.push_back (c.getGainReductionDb());
        }
    return grs;
}

static FuriaCompressor make (R ratio, float attack = 5.0f, float release = 5.0f)
{
    FuriaCompressor c;
    c.prepare (fs);
    c.setInput (2.5f);   // 0 dB de ganancia de entrada
    c.setRatio (ratio);
    c.setAttack (attack);
    c.setRelease (release);
    return c;
}

static void testStaticCurves()
{
    struct Case { R r; float expected; const char* name; };
    for (auto [r, expected, name] : { Case { R::R2, 10.0f, "2:1" }, Case { R::R4, 15.0f, "4:1" }, Case { R::R10, 18.0f, "10:1" },
                                      Case { R::R20, 19.0f, "20:1" }, Case { R::Nuke, 19.98f, "Nuke" }, Case { R::R1, 0.0f, "1:1" } })
    {
        auto c = make (r);
        const float gr = c.staticGr (FuriaCompressor::thresholdDb + 20.0f);
        std::printf ("    %-5s 20 dB por encima del umbral → GR %.2f dB (esperado %.2f)\n", name, gr, expected);
        CHECK (std::fabs (gr - expected) < 0.05f, "%s", name);
    }
}

static void testOneToOneTransparent()
{
    auto c = make (R::R1);
    float err = 0;
    float buf[2][64];
    for (int b = 0; b < 200; ++b)
    {
        float ref[64];
        for (int i = 0; i < 64; ++i) ref[i] = buf[0][i] = buf[1][i] = 0.8f * std::sin (0.03f * (float) (b * 64 + i));
        float* ch[] = { buf[0], buf[1] };
        c.processDynamics (ch, 2, 64);
        for (int i = 0; i < 64; ++i) err = std::max (err, std::fabs (buf[0][i] - ref[i]));
    }
    CHECK (err < 1e-5f, "1:1 sin distorsión ni HP debe ser transparente (err %g)", err);
}

static void testAttackRelease()
{
    double prevA = 0, prevR = 0;
    for (float knob : { 1.0f, 5.0f, 10.0f })
    {
        auto ca = make (R::R4, knob, 5.0f);
        auto gA = runSine (ca, { { -60.0f, 0.2 }, { 0.0f, 0.5 } });
        const float finalA = gA.back();
        int ia = 0;
        for (size_t i = (size_t) (0.2 * fs / 32); i < gA.size(); ++i) if (gA[i] >= 0.63f * finalA) { ia = (int) i; break; }
        const double tA = 1000.0 * ((double) ia * 32.0 / fs - 0.2);

        auto cr = make (R::R4, 3.0f, knob);
        auto gR = runSine (cr, { { 0.0f, 1.0 }, { -60.0f, 4.0 } });
        const float startR = gR[(size_t) (1.0 * fs / 32) - 1];
        int ir = 0;
        for (size_t i = (size_t) (1.0 * fs / 32); i < gR.size(); ++i) if (gR[i] <= 0.37f * startR) { ir = (int) i; break; }
        const double tR = 1000.0 * ((double) ir * 32.0 / fs - 1.0);
        std::printf ("    perilla %4.1f: ataque al 63 %% %.2f ms (nominal %.2f) · release al 37 %% %.0f ms (nominal %.0f)\n",
                     knob, tA, FuriaCompressor::attackMsFor (knob), tR, FuriaCompressor::releaseMsFor (knob));
        CHECK (tA >= prevA && tR > prevR, "los tiempos deben crecer con la perilla");
        prevA = tA; prevR = tR;
    }
}

static void testOpto()
{
    auto releaseTime = [] (double hold)
    {
        auto c = make (R::Opto, 3.0f, 3.0f);
        auto g = runSine (c, { { 0.0f, hold }, { -60.0f, 6.0 } });
        const size_t start = (size_t) (hold * fs / 32);
        for (size_t i = start; i < g.size(); ++i) if (g[i] < 1.0f) return 1000.0 * (double) (i - start) * 32.0 / fs;
        return 1.0e9;
    };
    const double shortR = releaseTime (0.1), longR = releaseTime (4.0);
    std::printf ("    Opto: release hasta <1 dB tras ráfaga %.0f ms, tras 4 s comprimiendo %.0f ms\n", shortR, longR);
    CHECK (longR > 2.0 * shortR, "el release del modo Opto depende del programa");
}

static void testDetector()
{
    auto grFor = [] (double f, bool hp, bool band)
    {
        auto c = make (R::R4);
        c.setDetector (hp, band, true);
        return runSine (c, { { -6.0f, 1.0 } }, f).back();
    };
    const float lowNoHp = grFor (50, false, false), lowHp = grFor (50, true, false);
    const float hiNoBand = grFor (6000, false, false), hiBand = grFor (6000, false, true);
    std::printf ("    50 Hz: GR %.1f dB sin HP, %.1f dB con HP · 6 kHz: %.1f dB sin realce, %.1f dB con realce\n", lowNoHp, lowHp, hiNoBand, hiBand);
    CHECK (lowHp < lowNoHp - 2.0f, "el HP del detector reduce la compresión de graves");
    CHECK (hiBand > hiNoBand + 1.0f, "el realce de banda comprime más los agudos (de-esser)");
}

static float harmonicDb (const std::vector<float>& x, double f)
{
    const double w = 2.0 * pi * f / fs, cw = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (float v : x) { const double s0 = v + cw * s1 - s2; s2 = s1; s1 = s0; }
    return (float) (10.0 * std::log10 (s1 * s1 + s2 * s2 - cw * s1 * s2 + 1e-30));
}

static void testDistortion()
{
    for (auto mode : { FuriaCompressor::Dist::Second, FuriaCompressor::Dist::Third })
    {
        std::vector<float> y;
        for (int i = 0; i < 48000; ++i) y.push_back (FuriaCompressor::distort (0.5f * (float) std::sin (2 * pi * 1000.0 * i / fs), mode));
        const float f = harmonicDb (y, 1000), h2 = harmonicDb (y, 2000) - f, h3 = harmonicDb (y, 3000) - f;
        std::printf ("    %s: H2 %.1f dBc, H3 %.1f dBc\n", mode == FuriaCompressor::Dist::Second ? "Dist 2" : "Dist 3", h2, h3);
        if (mode == FuriaCompressor::Dist::Second) CHECK (h2 > h3, "Dist 2: domina el 2º armónico");
        else CHECK (h3 > h2 + 20.0f, "Dist 3: domina el 3º armónico");
    }
    CHECK (std::fabs (FuriaCompressor::distort (0.001f, FuriaCompressor::Dist::Second) - 0.001f) < 2e-5f, "ganancia 1 en pequeña señal");
}

static void testStability()
{
    std::mt19937 rng (4);
    std::normal_distribution<float> n (0.0f, 1.0f);
    for (int r = 0; r <= (int) R::Opto; ++r)
    {
        auto c = make ((R) r, 1.0f, 1.0f);
        c.setInput (10.0f);
        c.setDetector (true, true, false);
        c.setAudioHp (true);
        bool finite = true;
        float buf[2][256];
        for (int b = 0; b < 300; ++b)
        {
            for (int i = 0; i < 256; ++i) { buf[0][i] = 2.0f * n (rng); buf[1][i] = 2.0f * n (rng); }
            float* ch[] = { buf[0], buf[1] };
            c.processDynamics (ch, 2, 256);
            for (int i = 0; i < 256; ++i) finite &= std::isfinite (buf[0][i]) && std::isfinite (buf[1][i]);
        }
        CHECK (finite, "salida finita (ratio %d)", r);
    }
}

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        { "curvas de ratio", testStaticCurves },
        { "1:1 transparente", testOneToOneTransparent },
        { "ataque y release", testAttackRelease },
        { "modo Opto", testOpto },
        { "detector (HP y realce)", testDetector },
        { "distorsión 2º / 3º armónico", testDistortion },
        { "estabilidad", testStability },
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
