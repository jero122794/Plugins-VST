// Tests del corrector de afinación: detección (YIN), corrección, escalas, transparencia y formantes.
#include <dspcore/Pitch.h>

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
static float cents (float a, float b) { return 1200.0f * std::log2 (a / b); }

// --- Señales de prueba -------------------------------------------------------

static std::vector<float> sine (double f, double sec, float amp = 0.5f)
{
    std::vector<float> x ((size_t) (sec * fs));
    for (size_t i = 0; i < x.size(); ++i) x[i] = amp * (float) std::sin (2 * pi * f * (double) i / fs);
    return x;
}

static std::vector<float> saw (double f, double sec, float amp = 0.4f)
{
    std::vector<float> x ((size_t) (sec * fs));
    double ph = 0;
    for (auto& v : x) { v = amp * (float) (2.0 * ph - 1.0); ph += f / fs; ph -= std::floor (ph); }
    return x;
}

/** Vocal sintética: tren de pulsos glotales filtrado por dos formantes (resonadores de 2º orden). */
static std::vector<float> vowel (double f0, double sec, double F1 = 700.0, double F2 = 1200.0)
{
    std::vector<float> x ((size_t) (sec * fs));
    struct Res { double a1, a2, y1 = 0, y2 = 0; double process (double v) { const double y = v - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; } };
    auto make = [] (double f, double bw) { const double r = std::exp (-pi * bw / fs); return Res { -2.0 * r * std::cos (2 * pi * f / fs), r * r }; };
    Res r1 = make (F1, 90.0), r2 = make (F2, 110.0);
    double ph = 0;
    for (auto& v : x)
    {
        ph += f0 / fs;
        double pulse = 0.0;
        if (ph >= 1.0) { ph -= 1.0; pulse = 1.0; }
        v = (float) (0.02 * (r1.process (pulse) + 0.6 * r2.process (pulse)));
    }
    return x;
}

static float detectMedian (const std::vector<float>& x, double skipSec = 0.3)
{
    YinDetector d;
    d.prepare (fs);
    std::vector<float> est;
    for (size_t i = 0; i < x.size(); ++i)
        if (d.push (x[i], 256) && (double) i / fs > skipSec && d.getFrequency() > 0.0f)
            est.push_back (d.getFrequency());
    if (est.empty()) return 0.0f;
    std::nth_element (est.begin(), est.begin() + (long) est.size() / 2, est.end());
    return est[est.size() / 2];
}

static std::vector<float> run (PitchCorrector& pc, const std::vector<float>& x)
{
    std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = pc.process (x[i]);
    return y;
}

// ---------------------------------------------------------------------------

static void testDetection()
{
    struct Case { const char* name; std::vector<float> x; double f; };
    const std::vector<Case> cases = {
        { "seno 110 Hz", sine (110, 1.0), 110 }, { "seno 440 Hz", sine (440, 1.0), 440 }, { "seno 880 Hz", sine (880, 1.0), 880 },
        { "sierra 98 Hz", saw (98, 1.0), 98 }, { "sierra 196 Hz", saw (196, 1.0), 196 },
        { "vocal 150 Hz", vowel (150, 1.0), 150 }, { "vocal 233 Hz", vowel (233.08, 1.0), 233.08 },
    };
    float worst = 0;
    for (auto& c : cases)
    {
        const float f = detectMedian (c.x);
        const float err = std::fabs (cents (f, (float) c.f));
        worst = std::max (worst, err);
        std::printf ("    %-14s detectado %8.2f Hz (%+.2f cents)\n", c.name, f, cents (f, (float) c.f));
    }
    CHECK (worst < 3.0f, "error de detección < 3 cents (peor %.2f)", worst);
}

static void testUnvoiced()
{
    std::mt19937 rng (1);
    std::normal_distribution<float> n (0.0f, 0.2f);
    std::vector<float> noise ((size_t) fs);
    for (auto& v : noise) v = n (rng);
    YinDetector d;
    d.prepare (fs);
    int voiced = 0, total = 0;
    for (float v : noise) if (d.push (v, 256)) { ++total; voiced += d.getFrequency() > 0.0f; }
    std::printf ("    ruido blanco: %d de %d ventanas marcadas como tonales\n", voiced, total);
    CHECK (voiced < total / 10, "el ruido debe detectarse como no tonal");
    YinDetector s;
    s.prepare (fs);
    bool any = false;
    for (int i = 0; i < 48000; ++i) if (s.push (0.0f, 256)) any |= s.getFrequency() > 0.0f;
    CHECK (! any, "el silencio no tiene tono");
}

static void testHardTune()
{
    // A3 desafinado +39 cents → 220 Hz exactos con corrección instantánea
    for (auto& [name, x] : std::vector<std::pair<const char*, std::vector<float>>> {
             { "sierra 225 Hz", saw (225.0, 2.0) }, { "vocal 225 Hz", vowel (225.0, 2.0) } })
    {
        PitchCorrector pc;
        pc.prepare (fs);
        pc.setNotes (Scale::chromatic);
        pc.setRetuneMs (0.0f);
        const auto y = run (pc, x);
        const float f = detectMedian (y, 0.6);
        std::printf ("    %-14s → salida %.2f Hz (%+.1f cents respecto a A3 = 220)\n", name, f, cents (f, 220.0f));
        CHECK (std::fabs (cents (f, 220.0f)) < 5.0f, "corrección a A3 (%+.1f cents)", cents (f, 220.0f));
    }
}

static void testScale()
{
    // 285 Hz (entre C#4 y D4) en Do mayor: C# no está permitido → D4 (293.66 Hz)
    PitchCorrector pc;
    pc.prepare (fs);
    pc.setNotes (Scale::absolute (Scale::major, 0));
    pc.setRetuneMs (0.0f);
    const float f = detectMedian (run (pc, vowel (285.0, 2.0)), 0.6);
    std::printf ("    285 Hz en Do mayor → %.2f Hz (D4 = 293.66; %+.1f cents)\n", f, cents (f, 293.66f));
    CHECK (std::fabs (cents (f, 293.66f)) < 6.0f, "debe ir a D4");
    CHECK (std::fabs (Scale::nearest (61.4f, Scale::absolute (Scale::major, 0)) - 62.0f) < 1e-4f, "C#+40c en Do mayor → D");
    CHECK (std::fabs (Scale::nearest (61.4f, Scale::absolute (Scale::major, 1)) - 61.0f) < 1e-4f, "en Do# mayor, C# es válido");
}

static void testRetuneSpeed()
{
    // Con velocidad lenta, al principio de la nota la afinación todavía no ha llegado al objetivo
    auto glideAt = [] (float retuneMs, double atSec)
    {
        PitchCorrector pc;
        pc.prepare (fs);
        pc.setRetuneMs (retuneMs);
        const auto y = run (pc, vowel (225.0, 1.5));
        const int lat = pc.getLatency();
        std::vector<float> seg (y.begin() + lat + (long) (atSec * fs), y.begin() + lat + (long) ((atSec + 0.15) * fs));
        return detectMedian (seg, 0.0);
    };
    const float fast = glideAt (0.0f, 0.2), slow = glideAt (400.0f, 0.2);
    std::printf ("    a los 200 ms: retune 0 ms → %.1f Hz, retune 400 ms → %.1f Hz\n", fast, slow);
    CHECK (std::fabs (cents (fast, 220.0f)) < 6.0f, "retune 0: ya corregido");
    CHECK (cents (slow, 220.0f) > 10.0f, "retune lento: todavía en camino");
}

static void testTransparency()
{
    // Nota ya afinada: la salida debe ser casi idéntica a la entrada retrasada
    PitchCorrector pc;
    pc.prepare (fs);
    const auto x = vowel (220.0, 2.0);
    const auto y = run (pc, x);
    const int lat = pc.getLatency();
    double xy = 0, xx = 0, yy = 0;
    for (size_t i = (size_t) (0.8 * fs); i < x.size() - (size_t) lat; ++i)
    {
        xy += (double) x[i] * y[i + (size_t) lat]; xx += (double) x[i] * x[i]; yy += (double) y[i + (size_t) lat] * y[i + (size_t) lat];
    }
    const double corr = xy / std::sqrt (xx * yy), gainDb = 10.0 * std::log10 (yy / xx);
    std::printf ("    nota afinada: correlación con la entrada %.4f, nivel %+.2f dB, latencia %d muestras (%.1f ms)\n",
                 corr, gainDb, lat, 1000.0 * lat / fs);
    CHECK (corr > 0.98, "transparencia (correlación %.4f)", corr);
    CHECK (std::fabs (gainDb) < 0.5, "sin cambio de nivel");

    // Con una señal NO periódica (ruido: tramo sordo) la latencia reportada tiene que ser exacta.
    // Con señales periódicas, un error de latencia de un número entero de periodos pasaría desapercibido.
    std::mt19937 rng (3);
    std::normal_distribution<float> n (0.0f, 0.1f);
    std::vector<float> noise ((size_t) fs);
    for (auto& v : noise) v = n (rng);
    PitchCorrector pn;
    pn.prepare (fs);
    const auto yn = run (pn, noise);
    const int L = pn.getLatency();
    xy = xx = yy = 0;
    for (size_t i = 10000; i < noise.size() - (size_t) L; ++i)
    {
        xy += (double) noise[i] * yn[i + (size_t) L]; xx += (double) noise[i] * noise[i]; yy += (double) yn[i + (size_t) L] * yn[i + (size_t) L];
    }
    const double cn = xy / std::sqrt (xx * yy);
    std::printf ("    ruido (sordo): correlación con la entrada retrasada exactamente %d muestras: %.4f\n", L, cn);
    CHECK (cn > 0.99, "latencia exacta con señal no periódica (%.4f)", cn);
}

/** Envolvente espectral por suavizado del espectro (cepstrum simplificado): devuelve la frecuencia del pico entre 400 y 1000 Hz. */
static float formantPeak (const std::vector<float>& x, size_t start)
{
    const int n = 8192;
    std::vector<std::complex<double>> s ((size_t) n);
    for (int i = 0; i < n; ++i)
        s[(size_t) i] = x[start + (size_t) i] * (0.5 - 0.5 * std::cos (2 * pi * i / n));
    fft (s, false);
    std::vector<double> mag ((size_t) n / 2);
    for (int i = 0; i < n / 2; ++i) mag[(size_t) i] = std::abs (s[(size_t) i]);
    // Envolvente: máximo local en ±150 Hz (une los armónicos) y luego media
    const int halfWin = (int) (150.0 * n / fs);
    float bestF = 0; double best = -1;
    for (int i = (int) (400.0 * n / fs); i < (int) (1000.0 * n / fs); ++i)
    {
        double env = 0;
        for (int k = i - halfWin; k <= i + halfWin; ++k) env = std::max (env, mag[(size_t) k]);
        if (env > best) { best = env; bestF = (float) (i * fs / n); }
    }
    return bestF;
}

static void testFormants()
{
    // Subir 3 semitonos una vocal con F1 = 700 Hz: el tono sube un 19 %, el formante debe quedarse
    PitchCorrector pc;
    pc.prepare (fs);
    pc.setRetuneMs (0.0f);
    pc.setTransposeSemitones (3.0f);
    const auto x = vowel (130.81, 2.0);   // C3
    const auto y = run (pc, x);
    const float fIn = formantPeak (x, (size_t) fs), fOut = formantPeak (y, (size_t) fs);
    const float pIn = detectMedian (x), pOut = detectMedian (y, 0.6);
    std::printf ("    +3 semitonos: tono %.1f → %.1f Hz (%+.0f cents); formante %.0f → %.0f Hz (%+.0f %%)\n",
                 pIn, pOut, cents (pOut, pIn), fIn, fOut, 100.0f * (fOut / fIn - 1.0f));
    CHECK (std::fabs (cents (pOut, pIn) - 300.0f) < 8.0f, "transposición de 3 semitonos");
    CHECK (std::fabs (fOut / fIn - 1.0f) < 0.10f, "el formante debe quedarse en su sitio (un remuestreo lo movería +19 %%)");
}

static void testNoteTransitions()
{
    // Melodía con silencios entre notas: los arranques no deben producir saltos de octava en el objetivo
    static const double notes[] = { 60, 62, 64, 65, 67, 65, 64, 62 };
    PitchCorrector pc;
    pc.prepare (fs, 80.0f, 1100.0f);
    pc.setNotes (Scale::absolute (Scale::major, 0));
    pc.setRetuneMs (30.0f);
    // Generador de vocal continuo (los resonadores no se reinician entre notas, como en la voz real)
    struct Res { double a1, a2, y1 = 0, y2 = 0; double process (double v) { const double y = v - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; } };
    auto make = [] (double f, double bw) { const double r = std::exp (-pi * bw / fs); return Res { -2.0 * r * std::cos (2 * pi * f / fs), r * r }; };
    Res r1 = make (650.0, 90.0), r2 = make (1100.0, 110.0);
    double ph = 0;
    int frames = 0, bad = 0, worstIdx = -1;
    float worst = 0;
    const int total = (int) (fs * 6.0);
    for (int i = 0; i < total; ++i)
    {
        const double sec = i / fs;
        const int k = (int) std::fmod (sec / 0.75, 8.0);
        const double inNote = std::fmod (sec, 0.75);
        const double f0 = 440.0 * std::pow (2.0, (notes[k] + 0.3 - 69.0) / 12.0);
        double pulse = 0.0;
        if (inNote < 0.68) { ph += f0 / fs; if (ph >= 1.0) { ph -= 1.0; pulse = 1.0; } }
        const float x = (float) (0.02 * (r1.process (pulse) + 0.6 * r2.process (pulse)));
        pc.process (x);
        if (i % 24 == 0 && inNote < 0.66)   // desde el primer instante de la nota: el ataque también cuenta
        {
            const float c = pc.getCorrectedHz();
            if (c <= 0.0f) continue;
            ++frames;
            const float err = std::fabs (12.0f * std::log2 (c / (float) (440.0 * std::pow (2.0, (notes[k] - 69.0) / 12.0))));
            if (err > 1.0f) { ++bad; if (err > worst) { worst = err; worstIdx = i; } }
        }
    }
    std::printf ("    %d estimaciones; %d desviadas más de 1 semitono (peor %.1f st en t = %.3f s)\n", frames, bad, worst, worstIdx / fs);
    CHECK (bad == 0, "sin saltos de octava en los arranques de nota");
}

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        { "detección (YIN)", testDetection },
        { "ruido y silencio", testUnvoiced },
        { "corrección instantánea", testHardTune },
        { "escala", testScale },
        { "velocidad de corrección", testRetuneSpeed },
        { "transparencia", testTransparency },
        { "formantes", testFormants },
        { "arranques de nota (errores de octava)", testNoteTransitions },
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
