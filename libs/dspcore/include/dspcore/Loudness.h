#pragma once
//
// Medición de sonoridad según ITU-R BS.1770-4 / EBU R128 / EBU Tech 3341-3342.
//
//  - Momentary (400 ms), Short-term (3 s), Integrated (bloques de 400 ms con
//    75 % de solape, gate absoluto -70 LUFS y relativo -10 LU), LRA (EBU Tech 3342:
//    short-term, gate -70 / -20 LU, percentiles 10–95).
//  - True peak: sobremuestreo 4x con FIR polifásico (BS.1770-4, anexo 2).
//
// Memoria constante: el integrado y el LRA usan histogramas de 0.01 LU que
// guardan además la SUMA EXACTA de energías por bin, de modo que la media
// es exacta salvo en el bin frontera del gate relativo.
//
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dspcore
{

/** Biquad en forma directa II transpuesta (coeficientes fijos: adecuada para el K-weighting). */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }
};

/** Filtro K (pre-filtro shelving + RLB high-pass) para cualquier frecuencia de muestreo. */
class KWeighting
{
public:
    void prepare (double fs) noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        {   // Etapa 1: high shelf (+4 dB, ~1.7 kHz)
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {   // Etapa 2: high-pass RLB (~38 Hz)
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
            hp.a1 = 2.0 * (K * K - 1.0) / a0;
            hp.a2 = (1.0 - K / Q + K * K) / a0;
        }
        reset();
    }
    void reset() noexcept { shelf.reset(); hp.reset(); }
    double process (double x) noexcept { return hp.process (shelf.process (x)); }

private:
    Biquad shelf, hp;
};

/** Detector de true peak: interpola 4x y devuelve el pico (lineal) de cada muestra de entrada. */
class TruePeakDetector
{
public:
    static constexpr int factor = 4, tapsPerPhase = 24, numTaps = factor * tapsPerPhase;

    TruePeakDetector() { design(); }

    void reset() noexcept { hist.fill (0.0f); pos = 0; }

    /** Retardo de grupo (muestras de entrada) entre la entrada y el pico que se reporta. */
    static constexpr int latency() noexcept { return tapsPerPhase / 2; }

    float process (float x) noexcept
    {
        hist[(size_t) pos] = x;
        float peak = 0.0f;
        for (int ph = 0; ph < factor; ++ph)
        {
            float acc = 0.0f;
            const float* c = coeffs[(size_t) ph].data();
            for (int k = 0; k < tapsPerPhase; ++k)
                acc += c[k] * hist[(size_t) ((pos - k) & (histSize - 1))];
            peak = std::max (peak, std::fabs (acc));
        }
        // El true peak nunca puede ser menor que la propia muestra (alineada con el retardo del filtro)
        peak = std::max (peak, std::fabs (hist[(size_t) ((pos - latency()) & (histSize - 1))]));
        pos = (pos + 1) & (histSize - 1);
        return peak;
    }

private:
    static constexpr int histSize = 32;  // potencia de 2 ≥ tapsPerPhase

    void design()
    {
        // Sinc con ventana de Kaiser, corte en la Nyquist original; ganancia 1 por fase.
        constexpr double pi = 3.14159265358979323846, beta = 7.0;
        auto bessel0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
            return sum;
        };
        // Centro sobre la rejilla 4x: las fases caen en desfases de 0, 1/4, 2/4 y 3/4 de muestra.
        // Con (numTaps-1)/2 caerían en 1/8, 3/8... y a fs/4 se perderían 0.17 dB de pico.
        const double centre = numTaps / 2.0;
        std::array<double, numTaps> h {};
        for (int n = 0; n < numTaps; ++n)
        {
            const double t = (n - centre) / factor;
            const double sinc = std::fabs (t) < 1e-12 ? 1.0 : std::sin (pi * t) / (pi * t);
            const double r = (n - centre) / centre;
            h[(size_t) n] = sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
        }
        for (int ph = 0; ph < factor; ++ph)
        {
            double sum = 0.0;
            for (int k = 0; k < tapsPerPhase; ++k) sum += h[(size_t) (k * factor + ph)];
            for (int k = 0; k < tapsPerPhase; ++k)
                coeffs[(size_t) ph][(size_t) k] = (float) (h[(size_t) (k * factor + ph)] / sum);
        }
    }

    std::array<std::array<float, tapsPerPhase>, factor> coeffs {};
    std::array<float, histSize> hist {};
    int pos = 0;
};

/** Histograma de sonoridad con suma exacta de energías por bin. */
class LoudnessHistogram
{
public:
    static constexpr double minLu = -70.0, maxLu = 10.0, binWidth = 0.01;
    static constexpr int numBins = (int) ((maxLu - minLu) / binWidth);

    void reset() { counts.assign ((size_t) numBins, 0); energy.assign ((size_t) numBins, 0.0); total = 0; }

    void add (double lufs, double meanSquare)
    {
        if (lufs < minLu) return;  // gate absoluto
        const int b = std::clamp ((int) ((lufs - minLu) / binWidth), 0, numBins - 1);
        ++counts[(size_t) b];
        energy[(size_t) b] += meanSquare;
        ++total;
    }

    long long getTotal() const noexcept { return total; }
    const std::vector<long long>& getCounts() const noexcept { return counts; }
    static double binCentre (int b) noexcept { return minLu + (b + 0.5) * binWidth; }

    /** Media de energía de los bloques ≥ gateLufs → LUFS. */
    double gatedLoudness (double gateLufs) const
    {
        const int start = std::clamp ((int) std::ceil ((gateLufs - minLu) / binWidth), 0, numBins);
        double e = 0.0;
        long long n = 0;
        for (int b = start; b < numBins; ++b) { e += energy[(size_t) b]; n += counts[(size_t) b]; }
        return n > 0 ? toLufs (e / (double) n) : -INFINITY;
    }

    /** Percentil (0–1) de los valores ≥ gateLufs. */
    double percentile (double gateLufs, double p) const
    {
        const int start = std::clamp ((int) std::ceil ((gateLufs - minLu) / binWidth), 0, numBins);
        long long n = 0;
        for (int b = start; b < numBins; ++b) n += counts[(size_t) b];
        if (n == 0) return -INFINITY;
        const double target = p * (double) (n - 1);
        long long acc = 0;
        for (int b = start; b < numBins; ++b)
        {
            acc += counts[(size_t) b];
            if ((double) acc > target) return binCentre (b);
        }
        return binCentre (numBins - 1);
    }

    static double toLufs (double meanSquare) noexcept
    {
        return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10 (meanSquare) : -INFINITY;
    }

private:
    std::vector<long long> counts;
    std::vector<double> energy;
    long long total = 0;
};

/**
    Medidor completo estéreo (o mono). process() desde el hilo de audio; los getters
    son atómicos y se pueden leer desde la UI. El integrado y el LRA se recalculan
    cada 100 ms. reset() solo desde el hilo de audio (o con el audio parado): la UI
    debe pedirlo con requestReset().
*/
class LoudnessMeter
{
public:
    static constexpr int uiHistBins = 160;           // 0.5 LU por bin
    static constexpr double uiHistMin = -70.0, uiHistStep = 0.5;

    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        numCh = std::clamp (numChannels, 1, 2);
        for (auto& k : kw) k.prepare (fs);
        stepSamples = (int) std::lround (fs * 0.1);   // 100 ms
        ring.assign (30, 0.0);                        // 30 pasos de 100 ms = 3 s
        reset();
    }

    void requestReset() noexcept { resetRequested.store (true); }

    void reset()
    {
        for (auto& k : kw) k.reset();
        for (auto& t : tp) t.reset();
        std::fill (ring.begin(), ring.end(), 0.0);
        ringPos = 0;
        stepsDone = 0;
        accum = 0.0;
        accumCount = 0;
        tpHold = spHold = 0.0f;
        integrated.reset();
        shortTermHist.reset();
        for (auto& b : uiHist) b.store (0);
        momentary.store (-INFINITY); shortTerm.store (-INFINITY);
        maxMomentary.store (-INFINITY); maxShortTerm.store (-INFINITY);
        integratedValue.store (-INFINITY); lraValue.store (0.0);
        truePeak.store (0.0f); samplePeak.store (0.0f);
        resetRequested.store (false);
    }

    void process (const float* const* ch, int n)
    {
        if (resetRequested.load()) reset();
        for (int i = 0; i < n; ++i)
        {
            double sq = 0.0;
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                const double y = kw[(size_t) c].process (x);
                sq += y * y;  // ponderación 1.0 para L/R; en mono se suma un solo canal
                tpHold = std::max (tpHold, tp[(size_t) c].process (x));
                spHold = std::max (spHold, std::fabs (x));
            }
            accum += sq;
            if (++accumCount == stepSamples)
                endStep();
        }
        truePeak.store (tpHold);
        samplePeak.store (spHold);
    }

    double getMomentary() const noexcept    { return momentary.load(); }
    double getShortTerm() const noexcept    { return shortTerm.load(); }
    double getMaxMomentary() const noexcept { return maxMomentary.load(); }
    double getMaxShortTerm() const noexcept { return maxShortTerm.load(); }
    double getIntegrated() const noexcept   { return integratedValue.load(); }
    double getLoudnessRange() const noexcept { return lraValue.load(); }
    double getTruePeakDb() const noexcept   { return 20.0 * std::log10 (std::max (truePeak.load(), 1.0e-12f)); }
    double getSamplePeakDb() const noexcept { return 20.0 * std::log10 (std::max (samplePeak.load(), 1.0e-12f)); }

    /** Histograma de short-term para la UI (bins de 0.5 LU desde -70). */
    std::uint32_t getUiHistogramBin (int b) const noexcept { return uiHist[(size_t) b].load (std::memory_order_relaxed); }

private:
    void endStep()
    {
        ring[(size_t) ringPos] = accum;
        ringPos = (ringPos + 1) % (int) ring.size();
        ++stepsDone;
        accum = 0.0;
        accumCount = 0;

        auto window = [&] (int steps)
        {
            double e = 0.0;
            for (int k = 1; k <= steps; ++k)
                e += ring[(size_t) ((ringPos - k + (int) ring.size()) % (int) ring.size())];
            return e / ((double) steps * stepSamples);
        };

        if (stepsDone >= 4)
        {
            const double ms = window (4);
            const double m = LoudnessHistogram::toLufs (ms);
            momentary.store (m);
            if (m > maxMomentary.load()) maxMomentary.store (m);
            integrated.add (m, ms);   // bloques de 400 ms con 75 % de solape
        }
        if (stepsDone >= 30)
        {
            const double ms = window (30);
            const double s = LoudnessHistogram::toLufs (ms);
            shortTerm.store (s);
            if (s > maxShortTerm.load()) maxShortTerm.store (s);
            shortTermHist.add (s, ms);   // EBU Tech 3342: short-term muestreado 10 veces por segundo
            if (s >= uiHistMin)
            {
                const int b = std::clamp ((int) ((s - uiHistMin) / uiHistStep), 0, uiHistBins - 1);
                uiHist[(size_t) b].fetch_add (1, std::memory_order_relaxed);
            }
        }

        // Integrado y LRA (cada 100 ms; el coste es recorrer los histogramas)
        const double ungated = integrated.gatedLoudness (-70.0);
        integratedValue.store (std::isfinite (ungated) ? integrated.gatedLoudness (ungated - 10.0) : -INFINITY);

        const double stUngated = shortTermHist.gatedLoudness (-70.0);
        if (std::isfinite (stUngated))
        {
            const double gate = stUngated - 20.0;
            const double lo = shortTermHist.percentile (gate, 0.10), hi = shortTermHist.percentile (gate, 0.95);
            lraValue.store (std::isfinite (lo) && std::isfinite (hi) ? hi - lo : 0.0);
        }
    }

    double fs = 48000.0;
    int numCh = 2;
    std::array<KWeighting, 2> kw;
    std::array<TruePeakDetector, 2> tp;
    int stepSamples = 4800;
    std::vector<double> ring;
    int ringPos = 0;
    long long stepsDone = 0;
    double accum = 0.0;
    int accumCount = 0;
    float tpHold = 0.0f, spHold = 0.0f;
    LoudnessHistogram integrated, shortTermHist;

    std::atomic<double> momentary { -INFINITY }, shortTerm { -INFINITY }, maxMomentary { -INFINITY }, maxShortTerm { -INFINITY };
    std::atomic<double> integratedValue { -INFINITY }, lraValue { 0.0 };
    std::atomic<float> truePeak { 0.0f }, samplePeak { 0.0f };
    std::atomic<bool> resetRequested { false };
    std::array<std::atomic<std::uint32_t>, uiHistBins> uiHist {};
};

} // namespace dspcore
