#pragma once
//
// Corrección de afinación en tiempo real.
//
//  1. Detección: YIN (de Cheveigné y Kawahara, JASA 2002) con la función de diferencia
//     calculada mediante autocorrelación por FFT. Interpolación parabólica del mínimo.
//  2. Objetivo: nota más cercana dentro de la escala elegida, con una velocidad de
//     corrección (0 ms = corrección instantánea, efecto "robótico").
//  3. Resíntesis: TD-PSOLA. Granos de dos periodos (ventana de Hann) tomados en marcas
//     de análisis alineadas con los picos de la onda y recolocados con el periodo objetivo.
//     Conserva los formantes: la voz no suena "a pitufo" en desplazamientos moderados.
//
#include "LinearPhase.h"   // fft()

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace dspcore
{

// ============================================================================
/** Detector YIN por bloques. Llamar a push() con audio; analiza cada `hop` muestras. */
class YinDetector
{
public:
    void prepare (double sampleRate, float minHz = 60.0f, float maxHz = 1000.0f)
    {
        fs = sampleRate;
        maxLag = (int) std::ceil (fs / minHz);
        minLag = std::max (2, (int) std::floor (fs / maxHz));
        window = 1;
        while (window < 2 * maxLag) window <<= 1;          // la ventana cubre al menos 2 periodos del tono más grave
        fftSize = 2 * window;
        buffer.assign ((size_t) window, 0.0f);
        frame.assign ((size_t) window, 0.0f);
        diff.assign ((size_t) maxLag + 2, 0.0f);
        squares.assign ((size_t) window + 1, 0.0);
        specA.assign ((size_t) fftSize, {});
        specB.assign ((size_t) fftSize, {});
        write = 0;
        hopCounter = 0;
        frequency = 0.0f;
        confidence = 0.0f;
    }

    void setThreshold (float t) noexcept { threshold = t; }
    int getWindow() const noexcept { return window; }

    /** Devuelve true cuando hay una nueva estimación (cada `hop` muestras). */
    bool push (float x, int hop) noexcept
    {
        buffer[(size_t) write] = x;
        write = (write + 1) % window;
        if (++hopCounter < hop) return false;
        hopCounter = 0;
        analyse();
        return true;
    }

    float getFrequency() const noexcept  { return frequency; }    // 0 = sin tono (sordo o silencio)
    float getConfidence() const noexcept { return confidence; }   // 1 - aperiodicidad

private:
    void analyse()
    {
        // x[0..N): la ventana, de la muestra más antigua a la más reciente
        const int N = window, W = window - maxLag;   // W: ventana de integración de YIN
        double energy = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const float v = buffer[(size_t) ((write + i) % N)];
            frame[(size_t) i] = v;
            energy += (double) v * v;
        }
        if (energy / N < 1.0e-7)   // silencio (~ -70 dBFS RMS)
        {
            frequency = 0.0f; confidence = 0.0f;
            return;
        }

        // Término cruzado exacto: c(τ) = Σ_{j<W} x[j]·x[j+τ] = IFFT( conj(FFT(x[0..W))) · FFT(x[0..N)) )
        for (int i = 0; i < fftSize; ++i)
        {
            specA[(size_t) i] = i < W ? frame[(size_t) i] : 0.0f;
            specB[(size_t) i] = i < N ? frame[(size_t) i] : 0.0f;
        }
        fft (specA, false);
        fft (specB, false);
        for (int i = 0; i < fftSize; ++i) specA[(size_t) i] = std::conj (specA[(size_t) i]) * specB[(size_t) i];
        fft (specA, true);

        // Energías parciales por suma acumulada
        squares[0] = 0.0;
        for (int i = 0; i < N; ++i) squares[(size_t) i + 1] = squares[(size_t) i] + (double) frame[(size_t) i] * frame[(size_t) i];
        const double e0 = squares[(size_t) W];

        // d(τ) = Σ (x[j] − x[j+τ])²  y su versión normalizada acumulada (CMND)
        double running = 0.0;
        diff[0] = 1.0f;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            const double et = squares[(size_t) (tau + W)] - squares[(size_t) tau];
            const double d = std::max (0.0, e0 + et - 2.0 * specA[(size_t) tau].real());
            running += d;
            diff[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
        }

        // Selección del periodo. La regla original de YIN ("primer mínimo bajo el umbral") cae en T/2 cuando
        // el 2º armónico domina (p. ej. un formante sobre él) → error de octava hacia arriba. En su lugar:
        // mínimo global de los valles, y luego el periodo más corto cuyo valle esté casi tan bajo (evita también 2T).
        float globalMin = 1.0e9f;
        for (int tau = minLag; tau <= maxLag; ++tau) globalMin = std::min (globalMin, diff[(size_t) tau]);
        if (globalMin >= threshold)
        {
            frequency = 0.0f;
            confidence = 1.0f - std::min (1.0f, globalMin);
            return;
        }
        int lag = -1;
        for (int tau = std::max (minLag, 1); tau < maxLag; ++tau)
        {
            const float v = diff[(size_t) tau];
            const bool localMin = v <= diff[(size_t) tau - 1] && v <= diff[(size_t) tau + 1];
            if (localMin && v <= globalMin + octaveTolerance) { lag = tau; break; }
        }
        if (lag < 0)
            for (int tau = minLag; tau <= maxLag; ++tau)
                if (diff[(size_t) tau] <= globalMin) { lag = tau; break; }

        // Interpolación parabólica del mínimo
        float refined = (float) lag;
        if (lag > 1 && lag < maxLag)
        {
            const float a = diff[(size_t) lag - 1], b = diff[(size_t) lag], c = diff[(size_t) lag + 1];
            const float den = a - 2.0f * b + c;
            if (std::fabs (den) > 1.0e-9f) refined += 0.5f * (a - c) / den;
        }
        frequency = (float) (fs / refined);
        confidence = 1.0f - diff[(size_t) lag];
    }

    double fs = 48000.0;
    int minLag = 48, maxLag = 800, window = 2048, fftSize = 4096;
    float threshold = 0.2f;                  // valle global por encima de esto = sin tono
    static constexpr float octaveTolerance = 0.04f;
    std::vector<float> buffer, frame, diff;
    std::vector<double> squares;
    std::vector<std::complex<double>> specA, specB;
    int write = 0, hopCounter = 0;
    float frequency = 0.0f, confidence = 0.0f;
};

// ============================================================================
/** Escalas: máscara de 12 bits (bit 0 = tónica). */
struct Scale
{
    static constexpr std::uint16_t chromatic  = 0b111111111111;
    static constexpr std::uint16_t major      = 0b101010110101;   // 0 2 4 5 7 9 11
    static constexpr std::uint16_t minor      = 0b010110101101;   // 0 2 3 5 7 8 10
    static constexpr std::uint16_t harmonicMinor = 0b100110101101;
    static constexpr std::uint16_t pentatonicMajor = 0b001010010101;
    static constexpr std::uint16_t pentatonicMinor = 0b010010101001;
    static constexpr std::uint16_t blues      = 0b010011101001;
    static constexpr std::uint16_t dorian     = 0b011010101101;

    /** Nota MIDI (fraccionaria) → nota permitida más cercana. key: 0 = C … 11 = B. notesMask en notas absolutas (bit 0 = C). */
    static float nearest (float midi, std::uint16_t notesMask)
    {
        if ((notesMask & 0xFFF) == 0) return midi;
        float best = midi, bestDist = 1.0e9f;
        const int centre = (int) std::lround (midi);
        for (int n = centre - 7; n <= centre + 7; ++n)
        {
            if (! (notesMask >> (((n % 12) + 12) % 12) & 1)) continue;
            const float d = std::fabs ((float) n - midi);
            if (d < bestDist) { bestDist = d; best = (float) n; }
        }
        return best;
    }

    /** Escala relativa a la tónica → máscara de notas absolutas. */
    static std::uint16_t absolute (std::uint16_t scale, int key)
    {
        std::uint16_t m = 0;
        for (int i = 0; i < 12; ++i)
            if (scale >> i & 1) m |= (std::uint16_t) (1u << ((i + key) % 12));
        return m;
    }
};

inline float hzToMidi (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
inline float midiToHz (float m)  { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }

// ============================================================================
/**
    Corrector de afinación mono (en estéreo se procesa la suma y se aplica a ambos canales,
    o un corrector por canal). Latencia fija: getLatency().
*/
class PitchCorrector
{
public:
    void prepare (double sampleRate, float minHz = 60.0f, float maxHz = 1000.0f)
    {
        fs = sampleRate;
        detector.prepare (fs, minHz, maxHz);
        hop = std::max (64, (int) (fs * 0.0025));                 // nueva estimación cada ~2.5 ms
        confirmHops = std::max (3, (int) std::ceil (0.025 * fs / hop));   // ~25 ms de nota antes de corregir
        maxPeriod = (int) std::ceil (fs / minHz);
        // Latencia: la ventana de análisis (para conocer el periodo) + un periodo máximo de margen para los granos
        latency = detector.getWindow() / 2 + 2 * maxPeriod;
        bufSize = 1;
        while (bufSize < 4 * (latency + 4 * maxPeriod)) bufSize <<= 1;
        in.assign ((size_t) bufSize, 0.0f);
        out.assign ((size_t) bufSize, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (in.begin(), in.end(), 0.0f);
        std::fill (out.begin(), out.end(), 0.0f);
        t = 0;
        nextSynthMark = (double) latency;
        currentPeriod = defaultPeriod();
        targetMidi = currentMidi = -1.0f;
        voicedRun = historyPos = 0;
        detectedHz.store (0.0f);
        correctedHz.store (0.0f);
    }

    // --- Parámetros
    void setNotes (std::uint16_t absoluteMask) noexcept { notes = absoluteMask; }
    void setRetuneMs (float ms) noexcept               { retuneMs = std::max (0.0f, ms); }
    void setAmount (float a) noexcept                  { amount = std::clamp (a, 0.0f, 1.0f); }   // 1 = corrección completa
    void setTransposeSemitones (float s) noexcept      { transpose = s; }

    int getLatency() const noexcept { return latency; }

    // --- Para la UI
    float getDetectedHz() const noexcept  { return detectedHz.load (std::memory_order_relaxed); }
    float getCorrectedHz() const noexcept { return correctedHz.load (std::memory_order_relaxed); }

    float process (float x) noexcept
    {
        in[(size_t) (t & (bufSize - 1))] = x;

        // --- Detección y objetivo (cada hop)
        if (detector.push (x, hop))
        {
            // Robustez: (1) en el ataque de cada nota la ventana contiene sobre todo silencio y uno o dos pulsos,
            // y lo único periódico es la resonancia de los formantes → el detector la confunde con el tono.
            // Por eso no se corrige hasta pasados ~25 ms de nota (el ataque pasa intacto, que es transparente).
            // (2) Mediana de las 5 últimas estimaciones contra saltos aislados.
            const float raw = detector.getFrequency();
            voicedRun = raw > 0.0f ? voicedRun + 1 : 0;
            if (raw > 0.0f)
            {
                history[(size_t) historyPos] = hzToMidi (raw);
                historyPos = (historyPos + 1) % (int) history.size();
            }
            float f = 0.0f;
            if (voicedRun >= confirmHops)
            {
                const int n = std::min (voicedRun, (int) history.size());
                std::array<float, 5> tmp {};
                for (int i = 0; i < n; ++i) tmp[(size_t) i] = history[(size_t) ((historyPos - 1 - i + 5) % 5)];
                std::nth_element (tmp.begin(), tmp.begin() + n / 2, tmp.begin() + n);
                f = midiToHz (tmp[(size_t) (n / 2)]);
            }
            if (f > 0.0f)
            {
                const float midi = hzToMidi (f);
                const float goal = Scale::nearest (midi, notes) + transpose;
                // Al empezar una nota, la corrección arranca desde la afinación real del cantante y se desliza
                // hacia el objetivo con la velocidad elegida (con retune 0 llega al instante).
                if (currentMidi < 0.0f) currentMidi = midi + transpose;
                // Velocidad de corrección: el objetivo se alcanza con una constante de tiempo de retuneMs
                const float hopMs = 1000.0f * (float) hop / (float) fs;
                const float a = retuneMs <= 0.0f ? 0.0f : std::exp (-hopMs / retuneMs);
                currentMidi = goal + a * (currentMidi - goal);
                targetMidi = midi + amount * (currentMidi - midi);    // cuánto se corrige
                analysisPeriod = (float) (fs / f);
                ratio = midiToHz (targetMidi) / f;
                voiced = true;
                detectedHz.store (f, std::memory_order_relaxed);
                correctedHz.store (midiToHz (targetMidi), std::memory_order_relaxed);
            }
            else
            {
                voiced = false;
                currentMidi = -1.0f;
                ratio = 1.0f;
                detectedHz.store (0.0f, std::memory_order_relaxed);
                correctedHz.store (0.0f, std::memory_order_relaxed);
            }
        }

        // --- Síntesis PSOLA: colocar granos en las marcas de síntesis
        while ((double) t >= nextSynthMark - (double) (maxPeriod))   // planificar con un periodo de antelación
        {
            const float pa = voiced ? analysisPeriod : defaultPeriod();
            const float ps = voiced ? pa / std::clamp (ratio, 0.5f, 2.0f) : pa;
            const double placed = placeGrain (nextSynthMark, pa, std::fabs (ratio - 1.0f) < 0.002f);
            nextSynthMark = placed + ps;
        }

        // --- Salida. Las marcas de síntesis ya están en tiempo de salida (= entrada + latency):
        // se lee el índice actual, sin volver a restar la latencia.
        float& slot = out[(size_t) (t & (bufSize - 1))];
        const float y = slot;
        slot = 0.0f;
        ++t;
        return y;
    }

private:
    float defaultPeriod() const noexcept { return (float) (fs * 0.005); }   // 5 ms en tramos sordos

    /**
        Grano centrado en la posición de salida `synthPos` (en tiempo de salida, que lleva `latency` de retraso
        respecto a la entrada). La marca de análisis se toma en el mismo instante de entrada, ajustada al pico
        de la onda más cercano para que los granos sumen en fase.
    */
    /**
        @param reanchor  si es true, la rejilla de síntesis se alinea con la marca de análisis (sin desfase):
                         se usa al empezar un tramo sonoro y cuando no hay corrección, para que la latencia sea exacta.
        @returns la posición de salida donde se colocó el grano (la siguiente marca se calcula desde aquí)
    */
    double placeGrain (double synthPos, float period, bool reanchor)
    {
        const int P = std::max (8, (int) std::lround (period));
        const double inputCentre = synthPos - (double) latency;   // instante equivalente en la entrada
        long long ta = (long long) std::llround (inputCentre);
        if (ta - P < t - bufSize + 8 || ta + P >= t) return synthPos;   // fuera del búfer disponible
        const bool onset = voiced && lastMark < 0;

        if (voiced)
        {
            // Alinear la marca con el pico de mayor magnitud en ±P/2, con continuidad respecto a la anterior
            long long best = ta;
            float bestV = -1.0f;
            const long long expected = lastMark > 0 ? lastMark + P : ta;
            const long long centre = std::abs (expected - ta) < P ? expected : ta;
            for (long long k = centre - P / 2; k <= centre + P / 2; ++k)
            {
                const float v = std::fabs (in[(size_t) (k & (bufSize - 1))]);
                if (v > bestV) { bestV = v; best = k; }
            }
            ta = best;
            lastMark = ta;
        }
        else
        {
            lastMark = -1;
        }

        if (voiced && (onset || reanchor))
            synthPos = (double) ta + (double) latency;   // sin desfase entre el grano y su posición original
        const long long ts = (long long) std::llround (synthPos);
        // Hann de 2P muestras: con marcas separadas P la suma es ~1 (OLA)
        for (int k = -P; k < P; ++k)
        {
            const float w = 0.5f + 0.5f * std::cos (3.14159265f * (float) k / (float) P);
            const long long src = ta + k, dst = ts + k;
            if (dst < t) continue;                               // ya emitido
            out[(size_t) (dst & (bufSize - 1))] += w * in[(size_t) (src & (bufSize - 1))];
        }
        return synthPos;
    }

    double fs = 48000.0;
    YinDetector detector;
    int hop = 120, maxPeriod = 800, latency = 2400, bufSize = 16384;
    std::vector<float> in, out;
    long long t = 0, lastMark = -1;
    double nextSynthMark = 0.0;

    std::uint16_t notes = Scale::chromatic;
    float retuneMs = 20.0f, amount = 1.0f, transpose = 0.0f;
    float analysisPeriod = 240.0f, currentPeriod = 240.0f, ratio = 1.0f, targetMidi = -1.0f, currentMidi = -1.0f;
    bool voiced = false;
    int voicedRun = 0, historyPos = 0, confirmHops = 10;
    std::array<float, 5> history {};
    std::atomic<float> detectedHz { 0.0f }, correctedHz { 0.0f };
};

} // namespace dspcore
