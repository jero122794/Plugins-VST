// Tests del DSP del compresor óptico. Sin JUCE: se ejecutan en cualquier máquina con `ctest`.
#include <dspcore/OptoCompressor.h>
#include <dspcore/Saturation.h>

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
constexpr float pi = 3.14159265358979f;

static float dbToLin (float db) { return std::pow (10.0f, db / 20.0f); }

/** Ejecuta n muestras de un seno de amplitud amp y devuelve el GR por muestra (dB). */
static std::vector<float> runSine (OptoCompressor& comp, float amp, int n, float freq = 1000.0f, double* phase = nullptr)
{
    std::vector<float> gr ((size_t) n);
    double ph = phase ? *phase : 0.0;
    for (int i = 0; i < n; ++i)
    {
        float s = amp * (float) std::sin (ph);
        ph += 2.0 * 3.14159265358979 * freq / fs;
        float* chans[] = { &s };
        comp.process (chans, 1, 1);
        gr[(size_t) i] = comp.getGainReductionDb();
    }
    if (phase) *phase = ph;
    return gr;
}

static float steadyGr (OptoModel m, OptoMode mode, float pr, float ampDb)
{
    OptoCompressor c;
    c.prepare (fs);
    c.setModel (m);
    c.setMode (mode);
    c.setPeakReduction (pr);
    auto gr = runSine (c, dbToLin (ampDb), (int) (fs * 3.0));
    return gr.back();
}

/** Muestras hasta que gr cruza el nivel (hacia arriba si rising). */
static int timeToCross (const std::vector<float>& gr, float level, bool rising)
{
    for (size_t i = 0; i < gr.size(); ++i)
        if (rising ? gr[i] >= level : gr[i] <= level)
            return (int) i;
    return -1;
}

static double ms (int samples) { return 1000.0 * samples / fs; }

// ---------------------------------------------------------------------------

static void testBypassAtZero()
{
    OptoCompressor c;
    c.prepare (fs);
    c.setPeakReduction (0.0f);
    double maxErr = 0;
    for (int i = 0; i < 48000; ++i)
    {
        const float in = 0.9f * std::sin (2 * pi * 440.0f * i / (float) fs);
        float s = in;
        float* ch[] = { &s };
        c.process (ch, 1, 1);
        maxErr = std::fmax (maxErr, std::fabs (s - in));
    }
    CHECK (maxErr == 0.0, "con Peak Reduction = 0 la salida debe ser idéntica (err %g)", maxErr);
}

static void testMonotonicPeakReduction()
{
    float prev = -1.0f;
    for (float pr : { 10.0f, 30.0f, 50.0f, 70.0f, 90.0f })
    {
        const float gr = steadyGr (OptoModel::LA2A, OptoMode::Compress, pr, -12.0f);
        std::printf ("    PR %3.0f @ -12 dBFS -> GR %5.2f dB\n", pr, gr);
        CHECK (gr > prev, "el GR debe crecer con Peak Reduction (PR %.0f: %.2f <= %.2f)", pr, gr, prev);
        prev = gr;
    }
}

static void testRatio()
{
    for (auto mode : { OptoMode::Compress, OptoMode::Limit })
    {
        // Por encima de la rodilla (de -12 a 0 dBFS con PR 70)
        const float g1 = steadyGr (OptoModel::LA2A, mode, 70.0f, -12.0f);
        const float g2 = steadyGr (OptoModel::LA2A, mode, 70.0f, 0.0f);
        const float outDelta = 12.0f - (g2 - g1);
        const float ratio = 12.0f / outDelta;
        std::printf ("    %s: ratio efectivo ≈ %.1f:1\n", mode == OptoMode::Compress ? "Compress" : "Limit", ratio);
        if (mode == OptoMode::Compress)
            CHECK (ratio > 2.0f && ratio < 4.5f, "Compress debería rondar 3:1, sale %.2f", ratio);
        else
            CHECK (ratio > 6.0f, "Limit debería superar 6:1, sale %.2f", ratio);
    }
}

static void testAttack()
{
    double t[2];
    for (int m = 0; m < 2; ++m)
    {
        OptoCompressor c;
        c.prepare (fs);
        c.setModel ((OptoModel) m);
        c.setPeakReduction (70.0f);
        auto gr = runSine (c, dbToLin (-12.0f), (int) (fs * 1.0));
        const int i90 = timeToCross (gr, 0.9f * gr.back(), true);
        t[m] = ms (i90);
        std::printf ("    %s: ataque al 90%% en %.1f ms (GR final %.1f dB)\n", m == 0 ? "LA-2A" : "LA-3A", t[m], gr.back());
    }
    CHECK (t[0] > 5.0 && t[0] < 80.0, "ataque LA-2A fuera de rango: %.1f ms", t[0]);
    CHECK (t[1] < t[0], "el LA-3A debe atacar más rápido que el LA-2A (%.1f vs %.1f ms)", t[1], t[0]);
}

/** Comprime durante holdSec, luego pasa a una señal de sonda muy baja y devuelve el GR tras el corte. */
static std::vector<float> releaseCurve (OptoModel m, double holdSec, double measureSec)
{
    OptoCompressor c;
    c.prepare (fs);
    c.setModel (m);
    c.setPeakReduction (60.0f);
    double ph = 0.0;
    runSine (c, dbToLin (-6.0f), (int) (fs * holdSec), 1000.0f, &ph);
    return runSine (c, dbToLin (-80.0f), (int) (fs * measureSec), 1000.0f, &ph);
}

static void testTwoStageRelease()
{
    auto gr = releaseCurve (OptoModel::LA2A, 2.0, 1.0);
    const float g0 = gr.front();
    const float g60 = gr[(size_t) (0.060 * fs)];
    const float frac = g60 / g0;
    std::printf ("    LA-2A: GR %.1f dB -> %.1f dB a los 60 ms (%.0f%%)\n", g0, g60, 100 * frac);
    CHECK (frac < 0.75f, "la primera fase de release debería recuperar buena parte en 60 ms (queda %.0f%%)", 100 * frac);
    CHECK (frac > 0.25f, "debería quedar una cola lenta tras 60 ms (queda %.0f%%)", 100 * frac);
}

static void testProgramDependentRelease()
{
    auto shortHold = releaseCurve (OptoModel::LA2A, 0.15, 20.0);
    auto longHold  = releaseCurve (OptoModel::LA2A, 8.0, 20.0);
    const int tShort = timeToCross (shortHold, 0.5f, false);
    const int tLong  = timeToCross (longHold, 0.5f, false);
    std::printf ("    LA-2A release hasta 0.5 dB: tras ráfaga corta %.0f ms, tras 8 s comprimiendo %.0f ms\n", ms (tShort), ms (tLong));
    CHECK (tShort > 0 && tLong > 0, "no llegó a recuperarse (%d, %d)", tShort, tLong);
    CHECK (tLong > 2 * tShort, "el release debe depender del programa (%.0f vs %.0f ms)", ms (tLong), ms (tShort));
}

static void testLa3aReleasesFaster()
{
    auto a = releaseCurve (OptoModel::LA2A, 4.0, 20.0);
    auto b = releaseCurve (OptoModel::LA3A, 4.0, 20.0);
    const int ta = timeToCross (a, 0.5f, false), tb = timeToCross (b, 0.5f, false);
    std::printf ("    release completo (0.5 dB): LA-2A %.0f ms, LA-3A %.0f ms\n", ms (ta), ms (tb));
    CHECK (tb > 0 && tb < ta, "el LA-3A debe recuperarse antes que el LA-2A");
}

static void testStereoLink()
{
    for (bool link : { true, false })
    {
        OptoCompressor c;
        c.prepare (fs);
        c.setPeakReduction (70.0f);
        c.setStereoLink (link);
        float rIn = 0.01f, rOut = 0;
        for (int i = 0; i < (int) fs; ++i)
        {
            float l = 0.5f * std::sin (2 * pi * 1000.0f * i / (float) fs);
            float r = rIn;
            float* ch[] = { &l, &r };
            c.process (ch, 2, 1);
            rOut = r;
        }
        const float rGainDb = 20.0f * std::log10 (rOut / rIn);
        std::printf ("    link=%d: ganancia en R (sin señal propia) %.2f dB\n", (int) link, rGainDb);
        if (link) CHECK (rGainDb < -3.0f, "con link, R debe comprimirse con L");
        else      CHECK (rGainDb > -0.5f, "sin link, R no debe comprimirse");
    }
}

static void testStability()
{
    std::mt19937 rng (1234);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    for (auto mode : { OptoMode::Compress, OptoMode::Limit })
    {
        OptoCompressor c;
        c.prepare (fs);
        c.setMode (mode);
        c.setPeakReduction (100.0f);
        c.setEmphasis (100.0f);
        float maxOut = 0;
        bool finite = true;
        std::vector<float> buf (512);
        for (int b = 0; b < 400; ++b)
        {
            for (auto& s : buf) s = 4.0f * nd (rng); // +12 dB por encima de full scale
            float* ch[] = { buf.data() };
            c.process (ch, 1, (int) buf.size());
            for (float s : buf) { finite &= std::isfinite (s); maxOut = std::fmax (maxOut, std::fabs (s)); }
        }
        CHECK (finite, "salida no finita");
        CHECK (maxOut < 16.0f, "salida desbocada: %g", maxOut);
    }
}

// ---------------------------------------------------------------------------
// Saturación: contenido armónico con Goertzel

static float harmonicDb (const std::vector<float>& x, double f)
{
    const double w = 2.0 * 3.14159265358979 * f / fs, cw = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (float v : x) { const double s0 = v + cw * s1 - s2; s2 = s1; s1 = s0; }
    const double p = s1 * s1 + s2 * s2 - cw * s1 * s2;
    return (float) (10.0 * std::log10 (p + 1e-30));
}

static void testHarmonics()
{
    float h2[2], h3[2];
    for (int t = 0; t < 2; ++t)
    {
        OutputStage st;
        st.prepare (fs);
        st.setType ((StageType) t);
        st.setColor (1.0f);
        std::vector<float> y;
        const int n = 48000;
        for (int i = 0; i < 2 * n; ++i)
        {
            const float v = st.process (0.5f * std::sin (2 * pi * 1000.0f * i / (float) fs));
            if (i >= n) y.push_back (v);  // descarta el transitorio
        }
        const float f = harmonicDb (y, 1000), a2 = harmonicDb (y, 2000), a3 = harmonicDb (y, 3000);
        h2[t] = a2 - f;
        h3[t] = a3 - f;
        std::printf ("    %s @ -6 dBFS: H2 %.1f dBc, H3 %.1f dBc\n", t == 0 ? "Tube (2A)" : "SolidState (3A)", h2[t], h3[t]);
    }
    CHECK (h2[0] > h2[1] + 6.0f, "la válvula debe tener bastante más 2º armónico");
    CHECK (h3[1] - h2[1] > h3[0] - h2[0], "en estado sólido debe predominar el 3º sobre el 2º");
    CHECK (h2[0] < -15.0f, "la válvula no debería distorsionar de forma grosera a -6 dBFS (H2 %.1f dBc)", h2[0]);
}

static void testColorZeroIsTransparent()
{
    OutputStage st;
    st.prepare (fs);
    st.setColor (0.0f);
    float err = 0;
    for (int i = 0; i < 1000; ++i)
    {
        const float x = 0.7f * std::sin (0.01f * i);
        err = std::fmax (err, std::fabs (st.process (x) - x));
    }
    CHECK (err == 0.0f, "color 0 debe ser transparente");
}

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        { "bypass con PR=0", testBypassAtZero },
        { "PR monotónico", testMonotonicPeakReduction },
        { "ratio Compress/Limit", testRatio },
        { "ataque", testAttack },
        { "release en dos fases", testTwoStageRelease },
        { "release dependiente del programa", testProgramDependentRelease },
        { "LA-3A más rápido que LA-2A", testLa3aReleasesFaster },
        { "stereo link", testStereoLink },
        { "estabilidad", testStability },
        { "armónicos válvula vs transistor", testHarmonics },
        { "color 0 transparente", testColorZeroIsTransparent },
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
