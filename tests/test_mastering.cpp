// Tests de los módulos de mastering: limitador, clipper, dither, compresor de bus,
// saturación, imagen estéreo y etapa de entrada.
#include <dspcore/Limiter.h>
#include <dspcore/MasterDsp.h>

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
constexpr double pi = 3.14159265358979323846;
static float lin (float d) { return std::pow (10.0f, d / 20.0f); }

/** "Música" sintética exigente: ruido rosado + transitorios + agudos, con picos muy por encima de 0 dBFS. */
static void makeProgram (std::vector<float>& l, std::vector<float>& r, int n, unsigned seed, float gain)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    l.resize ((size_t) n); r.resize ((size_t) n);
    float b0 = 0, b1 = 0, b2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const float w = u (rng);
        b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
        float pink = 0.25f * (b0 + b1 + b2 + w * 0.1848f);
        const float kick = (i % 24000) < 2400 ? 1.6f * std::sin (2 * (float) pi * 55.0f * (float) (i % 24000) / (float) fs) * std::exp (-(float) (i % 24000) / 3000.0f) : 0.0f;
        const float hat = (i % 6000) < 300 ? 0.6f * u (rng) : 0.0f;
        const float hi = 0.3f * std::sin (2 * (float) pi * 15000.0f * (float) i / (float) fs);
        l[(size_t) i] = gain * (pink + kick + hat + hi);
        r[(size_t) i] = gain * (0.9f * pink + kick - hat + hi);
    }
}

// ---------------------------------------------------------------------------

static void testLimiterCeiling()
{
    for (bool tpMode : { true, false })
        for (float ceilingDb : { -1.0f, -0.1f })
        {
            TruePeakLimiter lim;
            lim.prepare (fs);
            lim.setCeilingDb (ceilingDb);
            lim.setLookaheadMs (1.5f);
            lim.setReleaseMs (60.0f);
            lim.setTruePeak (tpMode);
            lim.setInputGainDb (0.0f);

            std::vector<float> l, r;
            makeProgram (l, r, (int) fs * 10, 42, 2.0f);   // picos de ~+12 dBFS
            for (size_t off = 0; off < l.size(); off += 512)
            {
                float* ch[] = { l.data() + off, r.data() + off };
                lim.process (ch, 2, (int) std::min<size_t> (512, l.size() - off));
            }

            LoudnessMeter meter;   // mide el resultado con el detector validado contra EBU 3341
            meter.prepare (fs, 2);
            const float* ch[] = { l.data(), r.data() };
            meter.process (ch, (int) l.size());
            const double tp = meter.getTruePeakDb(), sp = meter.getSamplePeakDb();
            std::printf ("    modo %s, techo %+.1f: sample peak %+.3f dB, true peak %+.3f dBTP\n",
                         tpMode ? "TP " : "SP ", ceilingDb, sp, tp);
            CHECK (sp <= ceilingDb + 0.001, "sample peak por encima del techo");
            if (tpMode) CHECK (tp <= ceilingDb + 0.1, "true peak por encima del techo (+%.3f)", tp - ceilingDb);
        }
}

static void testLimiterTransparentBelowCeiling()
{
    TruePeakLimiter lim;
    lim.prepare (fs);
    lim.setCeilingDb (-1.0f);
    const int lat = lim.getLatencySamples();
    std::vector<float> in ((size_t) fs), l, r;
    for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * (float) std::sin (2 * pi * 440.0 * (double) i / fs);
    l = r = in;
    float* ch[] = { l.data(), r.data() };
    lim.process (ch, 2, (int) l.size());
    float err = 0;
    for (size_t i = (size_t) lat; i < in.size(); ++i) err = std::max (err, std::fabs (l[i] - in[i - (size_t) lat]));
    std::printf ("    -6 dBFS por debajo del techo: error vs entrada retrasada %d muestras = %.2e\n", lat, err);
    CHECK (err < 1e-6f, "debe ser transparente bajo el techo (y la latencia reportada exacta)");
}

static void testLimiterReleaseAndGr()
{
    TruePeakLimiter lim;
    lim.prepare (fs);
    lim.setCeilingDb (-1.0f);
    lim.setReleaseMs (50.0f);
    std::vector<float> l ((size_t) fs), r;
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = (i > 4800 && i < 9600 ? 1.0f : 0.1f) * (float) std::sin (2 * pi * 1000.0 * (double) i / fs);
    r = l;
    float* ch[] = { l.data(), r.data() };
    lim.process (ch, 2, 9000);
    const float grDuring = lim.getGainReductionDb();
    float* ch2[] = { l.data() + 9000, r.data() + 9000 };
    lim.process (ch2, 2, 9600 + 24000 - 9000);   // continuo: el limitador ve el final del pico
    float* ch3[] = { l.data() + 33600, r.data() + 33600 };
    lim.process (ch3, 2, 100);
    std::printf ("    GR durante el pico %.2f dB, 0.5 s después %.3f dB\n", grDuring, lim.getGainReductionDb());
    CHECK (std::fabs (grDuring - 1.0f) < 0.2f, "GR ≈ 1 dB para un seno de 0 dBFS con techo -1");
    CHECK (lim.getGainReductionDb() < 0.01f, "release completo tras el pico");
}

static void testClipper()
{
    SoftClipper c;
    c.setCeilingDb (-1.0f);
    c.setKnee (0.3f);
    float maxOut = 0, err = 0;
    for (int i = 0; i < 10000; ++i)
    {
        const float x = 3.0f * std::sin (0.01f * (float) i);
        maxOut = std::max (maxOut, std::fabs (c.process (x)));
        const float small = 0.5f * std::sin (0.01f * (float) i);
        err = std::max (err, std::fabs (c.process (small) - small));
    }
    std::printf ("    salida máx. %.4f (techo %.4f), error bajo el knee %.1e\n", maxOut, lin (-1.0f), err);
    CHECK (maxOut <= lin (-1.0f) + 1e-6f, "el clipper no debe superar el techo");
    CHECK (err == 0.0f, "transparente por debajo del knee");
}

static void testDither()
{
    for (bool shaping : { false, true })
    {
        Dither d;
        d.setBits (16);
        d.setShaping (shaping);
        const int n = 1 << 16;
        std::vector<float> l ((size_t) n), r ((size_t) n), orig ((size_t) n);
        for (int i = 0; i < n; ++i) orig[(size_t) i] = l[(size_t) i] = r[(size_t) i] = 0.3f * (float) std::sin (2 * pi * 997.0 * i / fs);
        float* ch[] = { l.data(), r.data() };
        d.process (ch, 2, n);
        bool onGrid = true;
        double e2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const double q = l[(size_t) i] * 32768.0;
            onGrid &= std::fabs (q - std::round (q)) < 1e-3;
            e2 += (l[(size_t) i] - orig[(size_t) i]) * (double) (l[(size_t) i] - orig[(size_t) i]);
        }
        const double noiseDb = 10.0 * std::log10 (e2 / n);
        std::printf ("    16 bits %s: valores en la rejilla de 16 bits: %s, ruido total %.1f dBFS RMS\n",
                     shaping ? "con noise shaping" : "TPDF plano      ", onGrid ? "sí" : "NO", noiseDb);
        CHECK (onGrid, "la salida debe estar cuantizada a 16 bits");
        if (! shaping) CHECK (std::fabs (noiseDb + 96.3) < 1.0, "TPDF a 16 bits: ruido ≈ -96.3 dBFS RMS");
    }
}

static void testBusCompressorStatic()
{
    BusCompressor c;
    c.prepare (fs);
    c.setThresholdDb (-20.0f);
    c.setRatio (4.0f);
    c.setKneeDb (0.0f);
    const float a = c.staticGainReduction (-30.0f), b = c.staticGainReduction (-10.0f), k = c.staticGainReduction (-20.0f);
    std::printf ("    curva 4:1 a -20: GR(-30)=%.2f, GR(-20)=%.2f, GR(-10)=%.2f dB\n", a, k, b);
    CHECK (a == 0.0f && std::fabs (b - 7.5f) < 1e-4f, "curva estática 4:1");

    // Dinámico: seno a -10 dBFS → GR ≈ 7.5 dB en régimen
    c.setAttackMs (5.0f);
    c.setReleaseMs (100.0f);
    std::vector<float> l ((size_t) fs), r;
    for (size_t i = 0; i < l.size(); ++i) l[i] = lin (-10.0f) * (float) std::sin (2 * pi * 1000.0 * (double) i / fs);
    r = l;
    float* ch[] = { l.data(), r.data() };
    c.process (ch, 2, (int) l.size());
    std::printf ("    seno -10 dBFS, umbral -20, 4:1 → GR %.2f dB\n", c.getGainReductionDb());
    CHECK (std::fabs (c.getGainReductionDb() - 7.5f) < 0.6f, "GR en régimen");
}

static void testBusCompressorAutoRelease()
{
    auto releaseTime = [] (bool autoRel, double holdSec)
    {
        BusCompressor c;
        c.prepare (fs);
        c.setThresholdDb (-30.0f); c.setRatio (4.0f); c.setKneeDb (0.0f);
        c.setAttackMs (1.0f); c.setReleaseMs (100.0f); c.setAutoRelease (autoRel);
        std::vector<float> l, r;
        const int hold = (int) (holdSec * fs), tail = (int) (6.0 * fs);
        l.resize ((size_t) (hold + tail));
        for (int i = 0; i < hold + tail; ++i)
            l[(size_t) i] = (i < hold ? 0.5f : 0.001f) * (float) std::sin (2 * pi * 1000.0 * i / fs);
        r = l;
        int t = -1;
        for (int i = 0; i < hold + tail; i += 64)
        {
            float* ch[] = { l.data() + i, r.data() + i };
            c.process (ch, 2, std::min (64, hold + tail - i));
            if (i > hold && t < 0 && c.getGainReductionDb() < 1.0f) t = i - hold;
        }
        return 1000.0 * t / fs;
    };
    const double shortBurst = releaseTime (true, 0.05), longHold = releaseTime (true, 3.0), manual = releaseTime (false, 3.0);
    std::printf ("    release hasta <1 dB: auto tras ráfaga %.0f ms, auto tras 3 s %.0f ms, manual 100 ms %.0f ms\n", shortBurst, longHold, manual);
    CHECK (longHold > 3.0 * shortBurst, "el release automático debe alargarse con compresión sostenida");
}

static void testSaturator()
{
    for (int m = 0; m < 3; ++m)
    {
        Saturator s;
        s.prepare (fs * 4);
        s.setMode ((Saturator::Mode) m);
        s.setDriveDb (12.0f);
        s.setAutoGain (true);
        // Pequeña señal: ganancia ≈ 1 con auto gain
        double inE = 0, outE = 0;
        for (int i = 0; i < 40000; ++i)
        {
            const float x = 0.001f * (float) std::sin (2 * pi * 1000.0 * i / (fs * 4));
            const float y = s.processSample (0, x);
            if (i > 20000) { inE += x * (double) x; outE += y * (double) y; }
        }
        const double g = 10.0 * std::log10 (outE / inE);
        std::printf ("    modo %d: ganancia en pequeña señal con auto-gain %.2f dB\n", m, g);
        CHECK (std::fabs (g) < 0.5, "auto gain mantiene el nivel en pequeña señal");
    }
    Saturator s;
    s.prepare (fs);
    s.setMix (0.0f);
    float err = 0;
    for (int i = 0; i < 1000; ++i) { const float x = 0.9f * std::sin (0.05f * i); err = std::max (err, std::fabs (s.processSample (0, x) - x)); }
    CHECK (err == 0.0f, "mix 0 = transparente");
}

static void testImager()
{
    auto run = [] (float width, float bassHz, double f, float& outSideRatio)
    {
        StereoImager im;
        im.prepare (fs);
        im.setWidth (width);
        im.setBassMonoHz (bassHz);
        const int n = (int) fs;
        std::vector<float> l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.3f * (float) std::sin (2 * pi * f * i / fs), s = 0.2f * (float) std::sin (2 * pi * f * i / fs + 1.0);
            l[(size_t) i] = m + s; r[(size_t) i] = m - s;
        }
        float* ch[] = { l.data(), r.data() };
        im.process (ch, 2, n);
        double se = 0, sIn = 0;
        for (int i = n / 2; i < n; ++i)
        {
            const double s = 0.5 * (l[(size_t) i] - r[(size_t) i]);
            const double s0 = 0.2 * std::sin (2 * pi * f * i / fs + 1.0);
            se += s * s; sIn += s0 * s0;
        }
        outSideRatio = (float) (10.0 * std::log10 ((se + 1e-30) / sIn));
    };
    float a, b, c, d;
    run (1.0f, 0.0f, 1000.0, a);
    run (0.0f, 0.0f, 1000.0, b);
    run (1.0f, 120.0f, 40.0, c);
    run (1.0f, 120.0f, 2000.0, d);
    std::printf ("    Side: ancho 100%% %.2f dB, ancho 0%% %.1f dB, graves mono 120 Hz → 40 Hz %.1f dB, 2 kHz %.2f dB\n", a, b, c, d);
    CHECK (std::fabs (a) < 0.01f, "100%% transparente");
    CHECK (b < -100.0f, "0%% = mono");
    CHECK (c < -20.0f, "por debajo del corte el Side se elimina");
    CHECK (std::fabs (d) < 0.1f, "por encima del corte el Side se conserva");
}

static void testInputStage()
{
    auto gainAt = [] (InputStage::Subsonic s, double f)
    {
        InputStage in;
        in.prepare (fs);
        in.setSubsonic (s);
        const int n = (int) (fs * 2);
        std::vector<float> l ((size_t) n), r;
        for (int i = 0; i < n; ++i) l[(size_t) i] = 0.5f * (float) std::sin (2 * pi * f * i / fs);
        r = l;
        double e = 0, e0 = 0;
        float* ch[] = { l.data(), r.data() };
        in.process (ch, 2, n);
        for (int i = n / 2; i < n; ++i) { e += l[(size_t) i] * (double) l[(size_t) i]; const double x = 0.5 * std::sin (2 * pi * f * i / fs); e0 += x * x; }
        return 10.0 * std::log10 (e / e0);
    };
    const double at10 = gainAt (InputStage::Subsonic::Hz30, 10.0), at100 = gainAt (InputStage::Subsonic::Hz30, 100.0);
    std::printf ("    subsónico 30 Hz: 10 Hz %.1f dB, 100 Hz %.2f dB\n", at10, at100);
    CHECK (at10 < -30.0 && std::fabs (at100) < 0.2, "filtro subsónico");
}

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        { "limitador: techo garantizado (medido con el detector EBU)", testLimiterCeiling },
        { "limitador: transparente bajo el techo y latencia exacta", testLimiterTransparentBelowCeiling },
        { "limitador: GR reportada", testLimiterReleaseAndGr },
        { "clipper", testClipper },
        { "dither", testDither },
        { "compresor de bus: curva y régimen", testBusCompressorStatic },
        { "compresor de bus: release automático", testBusCompressorAutoRelease },
        { "saturación", testSaturator },
        { "imagen estéreo", testImager },
        { "etapa de entrada", testInputStage },
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
