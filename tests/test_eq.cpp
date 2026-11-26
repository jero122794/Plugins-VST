// Tests del EQ dinámico. Sin JUCE.
#include <dspcore/DynamicEq.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
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
using Eq = DynamicEq<8>;

/** Ganancia medida (dB) de un seno de frecuencia f y amplitud amp, en estado estacionario. */
static float measureDb (Eq& eq, double f, float amp = 0.25f, int numCh = 1, float* rOutDb = nullptr,
                        float rAmp = -1.0f, double seconds = 0.6)
{
    const int n = (int) (fs * seconds), skip = n / 2;
    double inE = 0, outE = 0, inR = 0, outR = 0;
    for (int i = 0; i < n; ++i)
    {
        const float x = amp * (float) std::sin (2.0 * 3.14159265358979 * f * i / fs);
        const float xr = rAmp < 0 ? x : rAmp * (float) std::sin (2.0 * 3.14159265358979 * f * i / fs);
        float l = x, r = xr;
        eq.process (&l, &r, numCh, 1);
        if (i >= skip) { inE += (double) x * x; outE += (double) l * l; inR += (double) xr * xr; outR += (double) r * r; }
    }
    if (rOutDb) *rOutDb = (float) (10.0 * std::log10 ((outR + 1e-30) / (inR + 1e-30)));
    return (float) (10.0 * std::log10 ((outE + 1e-30) / inE));
}

static EqBandParams band (EqType t, float f, float g, float q, int slope = 12)
{
    EqBandParams p;
    p.enabled = true; p.type = t; p.freq = f; p.gainDb = g; p.q = q; p.slopeDbPerOct = slope;
    return p;
}

// ---------------------------------------------------------------------------

static void testBypass()
{
    Eq eq; eq.prepare (fs);
    float err = 0;
    for (int i = 0; i < 4800; ++i)
    {
        const float x = 0.5f * std::sin (0.01f * i);
        float l = x, r = -x;
        eq.process (&l, &r, 2, 1);
        err = std::fmax (err, std::fmax (std::fabs (l - x), std::fabs (r + x)));
    }
    CHECK (err == 0.0f, "sin bandas activas debe ser transparente (err %g)", err);
}

/** La curva de la UI (responseDb) debe coincidir con el audio medido. */
static void testUiCurveMatchesAudio()
{
    const std::vector<EqBandParams> cases = {
        band (EqType::Bell, 1000, 6, 1.0f),
        band (EqType::Bell, 12000, -9, 4.0f),
        band (EqType::LowShelf, 120, 5, 0.707f),
        band (EqType::HighShelf, 6000, -4, 0.707f),
        band (EqType::LowCut, 200, 0, 0.707f, 24),
        band (EqType::HighCut, 5000, 0, 1.5f, 48),
        band (EqType::Notch, 3000, 0, 8.0f),
    };
    const char* names[] = { "bell +6 @1k", "bell -9 @12k Q4", "low shelf", "high shelf", "low cut 24", "high cut 48 Q1.5", "notch" };
    float worst = 0;
    for (size_t c = 0; c < cases.size(); ++c)
    {
        float caseWorst = 0;
        for (double f : { 50.0, 150.0, 400.0, 1000.0, 2500.0, 3000.0, 6000.0, 12000.0, 18000.0 })
        {
            Eq eq; eq.prepare (fs);
            eq.setBand (0, cases[c]);
            const float measured = measureDb (eq, f);
            const float predicted = EqBand::responseDb (cases[c], fs, f);
            if (predicted > -60.0f)   // por debajo, la medición la domina el ruido numérico
                caseWorst = std::fmax (caseWorst, std::fabs (measured - predicted));
        }
        std::printf ("    %-18s error máx. curva vs audio: %.3f dB\n", names[c], caseWorst);
        worst = std::fmax (worst, caseWorst);
    }
    CHECK (worst < 0.1f, "la curva dibujada debe coincidir con el audio (peor %.3f dB)", worst);
}

static void testBellAccuracy()
{
    EqBandParams p = band (EqType::Bell, 1000, 6, 1.0f);
    const float atCentre = EqBand::responseDb (p, fs, 1000);
    const float far = EqBand::responseDb (p, fs, 50);
    std::printf ("    bell +6 dB: %.2f dB en el centro, %.2f dB a 50 Hz\n", atCentre, far);
    CHECK (std::fabs (atCentre - 6.0f) < 0.01f, "ganancia en el centro");
    CHECK (std::fabs (far) < 0.2f, "lejos del centro ~0 dB");
}

static void testCutSlopes()
{
    for (int slope : { 12, 24, 36, 48 })
    {
        auto p = band (EqType::LowCut, 1000, 0, 0.707f, slope);
        const float oneOctDown = EqBand::responseDb (p, fs, 250) - EqBand::responseDb (p, fs, 125);
        std::printf ("    low cut %d dB/oct: pendiente medida %.1f dB/oct (zona de rechazo)\n", slope, oneOctDown);
        CHECK (std::fabs (oneOctDown - (float) slope) < 1.0f, "pendiente %d", slope);
        CHECK (std::fabs (EqBand::responseDb (p, fs, 1000) + 3.01f) < 0.1f, "-3 dB en la frecuencia de corte (Butterworth)");
    }
}

static void testHighFrequencyBellAt48k()
{
    // Cramping: con bilineal, una campana alta se estrecha cerca de Nyquist. Se documenta su magnitud.
    auto p = band (EqType::Bell, 16000, 6, 1.0f);
    std::printf ("    bell +6 @16k (fs 48k): centro %.2f dB, a 20k %.2f dB\n",
                 EqBand::responseDb (p, fs, 16000), EqBand::responseDb (p, fs, 20000));
    auto p96 = EqBand::responseDb (p, 96000.0, 20000);
    std::printf ("    mismo bell en HQ (96k): a 20k %.2f dB\n", p96);
    CHECK (p96 > EqBand::responseDb (p, fs, 20000), "HQ debe reducir el cramping");
}

static void testDynamicBand()
{
    auto p = band (EqType::Bell, 1000, 0, 1.0f);
    p.dynRangeDb = -12.0f;
    p.thresholdDb = -30.0f;
    p.attackMs = 5.0f;
    p.releaseMs = 100.0f;

    Eq loud; loud.prepare (fs); loud.setBand (0, p);
    const float gLoud = measureDb (loud, 1000, std::pow (10.0f, -6.0f / 20.0f));
    Eq quiet; quiet.prepare (fs); quiet.setBand (0, p);
    const float gQuiet = measureDb (quiet, 1000, std::pow (10.0f, -50.0f / 20.0f));
    Eq other; other.prepare (fs); other.setBand (0, p);
    const float gOther = measureDb (other, 100, std::pow (10.0f, -6.0f / 20.0f));

    std::printf ("    dinámico (-12 dB, umbral -30): 1k fuerte %.2f dB, 1k débil %.2f dB, 100 Hz fuerte %.2f dB\n", gLoud, gQuiet, gOther);
    CHECK (gLoud < -9.0f, "señal fuerte en la banda debe atenuarse cerca del rango");
    CHECK (std::fabs (gQuiet) < 0.1f, "señal débil no debe tocarse");
    CHECK (std::fabs (gOther) < 0.5f, "el detector debe ser selectivo en frecuencia");
    CHECK (loud.band (0).getDynamicGainDb() < -9.0f, "la ganancia dinámica se publica para la UI");
}

static void testMidSide()
{
    auto p = band (EqType::Bell, 1000, 9, 1.0f);
    p.placement = Placement::Side;
    Eq eq; eq.prepare (fs); eq.setBand (0, p);
    float rDb = 0;
    const float lDb = measureDb (eq, 1000, 0.25f, 2, &rDb);     // mono (L = R): Side = 0
    std::printf ("    bell en Side con señal mono: L %.3f dB, R %.3f dB\n", lDb, rDb);
    CHECK (std::fabs (lDb) < 0.01f && std::fabs (rDb) < 0.01f, "una banda en Side no debe afectar a material mono");

    p.placement = Placement::Mid;
    Eq eq2; eq2.prepare (fs); eq2.setBand (0, p);
    const float midDb = measureDb (eq2, 1000, 0.25f, 2, &rDb);
    CHECK (std::fabs (midDb - 9.0f) < 0.1f, "en Mid, la señal mono recibe toda la ganancia (%.2f)", midDb);
}

static void testLeftOnly()
{
    auto p = band (EqType::Bell, 1000, -6, 1.0f);
    p.placement = Placement::Left;
    Eq eq; eq.prepare (fs); eq.setBand (0, p);
    float rDb = 0;
    const float lDb = measureDb (eq, 1000, 0.25f, 2, &rDb);
    CHECK (std::fabs (lDb + 6.0f) < 0.1f && std::fabs (rDb) < 0.01f, "Left: L %.2f, R %.2f", lDb, rDb);
}

static void testModulationStability()
{
    // Barrido brusco de frecuencia y ganancia con ruido: no debe explotar
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    Eq eq; eq.prepare (fs);
    bool finite = true;
    float peak = 0;
    for (int blk = 0; blk < 3000; ++blk)
    {
        for (int b = 0; b < 8; ++b)
        {
            auto p = band ((EqType) (b % 6), 20.0f * std::pow (1000.0f, 0.5f + 0.5f * u (rng)), 18.0f * u (rng), 0.2f + 9.0f * (0.5f + 0.5f * u (rng)), 12 * (1 + b % 4));
            p.dynRangeDb = b % 2 ? 12.0f * u (rng) : 0.0f;
            p.thresholdDb = -40.0f;
            eq.setBand (b, p);
        }
        float l[32], r[32];
        for (int i = 0; i < 32; ++i) { l[i] = 0.5f * u (rng); r[i] = 0.5f * u (rng); }
        eq.process (l, r, 2, 32);
        for (int i = 0; i < 32; ++i) { finite &= std::isfinite (l[i]) && std::isfinite (r[i]); peak = std::fmax (peak, std::fabs (l[i])); }
    }
    std::printf ("    pico con 8 bandas moduladas al azar: %.1f\n", peak);
    CHECK (finite, "salida no finita con modulación");
    CHECK (peak < 200.0f, "salida desbocada");
}

static void testOutputGain()
{
    Eq eq; eq.prepare (fs);
    eq.setOutputGainDb (-6.0f);
    eq.prepare (fs);  // sin rampa inicial
    const float g = measureDb (eq, 1000);
    CHECK (std::fabs (g + 6.0f) < 0.05f, "ganancia de salida (%.2f)", g);
}

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        { "bypass", testBypass },
        { "curva UI = audio", testUiCurveMatchesAudio },
        { "precisión del bell", testBellAccuracy },
        { "pendientes de corte", testCutSlopes },
        { "cramping cerca de Nyquist / HQ", testHighFrequencyBellAt48k },
        { "banda dinámica", testDynamicBand },
        { "mid/side", testMidSide },
        { "solo canal izquierdo", testLeftOnly },
        { "estabilidad con modulación", testModulationStability },
        { "ganancia de salida", testOutputGain },
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
