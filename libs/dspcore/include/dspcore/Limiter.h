#pragma once
//
// Limitador true-peak con lookahead, clipper suave y dither.
//
#include "Loudness.h"   // TruePeakDetector
#include "OnePole.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dspcore
{

/**
    Limitador "brickwall" con lookahead.

    Garantía (por construcción): con la ganancia g[n] necesaria para cada pico,
      1) h = mínimo deslizante de g sobre L+1 muestras
      2) r = release exponencial (sube despacio, baja al instante; r ≤ h)
      3) s = media móvil de r sobre L+1 muestras
    y el audio retrasado L muestras, la ganancia aplicada en cada pico es ≤ la
    necesaria: la media de L+1 valores que son todos ≤ g_pico. El ataque es
    una rampa lineal de L muestras: sin distorsión por ataque instantáneo.
*/
class TruePeakLimiter
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate, float maxLookaheadMs = 10.0f)
    {
        fs = sampleRate;
        maxL = (int) std::ceil (maxLookaheadMs * 0.001 * fs) + 1;
        const int detD = TruePeakDetector::latency();
        delaySize = 1;
        while (delaySize < maxL + detD + 2) delaySize <<= 1;
        for (auto& d : delay) d.assign ((size_t) delaySize, 0.0f);
        minQ.assign ((size_t) (maxL + 2), {});
        boxBuf.assign ((size_t) (maxL + 2), 1.0);
        setLookaheadMs (lookaheadMs);
        setReleaseMs (releaseMs);
        reset();
    }

    void reset()
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& t : det) t.reset();
        writePos = 0;
        sampleIndex = 0;
        qHead = qTail = 0;
        std::fill (boxBuf.begin(), boxBuf.end(), 1.0);
        boxPos = 0;
        boxSum = (double) (L + 1);
        rel = 1.0;
        grDb = 0.0f;
        maxGrDb = 0.0f;
    }

    void setCeilingDb (float db) noexcept   { ceiling = std::pow (10.0f, db / 20.0f); }
    void setInputGainDb (float db) noexcept { inGainTarget = std::pow (10.0f, db / 20.0f); }
    void setTruePeak (bool on) noexcept     { truePeak = on; }
    void setReleaseMs (float ms) noexcept   { releaseMs = ms; relCoef = onePoleCoef (ms, fs); }

    /** Cambia el lookahead. Cambia la latencia: llamar fuera del procesado o asumir un click. */
    void setLookaheadMs (float ms)
    {
        lookaheadMs = ms;
        const int newL = std::clamp ((int) std::lround (ms * 0.001 * fs), 1, std::max (1, maxL - 1));
        if (newL != L)
        {
            L = newL;
            qHead = qTail = 0;
            std::fill (boxBuf.begin(), boxBuf.end(), 1.0);
            boxPos = 0;
            boxSum = (double) (L + 1);
        }
    }

    int getLatencySamples() const noexcept { return L + TruePeakDetector::latency(); }
    float getGainReductionDb() const noexcept { return grDb; }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        numCh = std::min (numCh, maxChannels);
        const int D = TruePeakDetector::latency();
        float blockMaxGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            inGain += 0.002f * (inGainTarget - inGain);

            // --- Detección (retrasada D muestras por el filtro de interpolación)
            float peak = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i] * inGain;
                delay[(size_t) c][(size_t) writePos] = x;
                const float tp = det[(size_t) c].process (x);
                const float sp = std::fabs (delay[(size_t) c][(size_t) ((writePos - D) & (delaySize - 1))]);
                peak = std::max (peak, truePeak ? tp : sp);
            }
            const double g = peak > ceiling ? (double) ceiling / (double) peak : 1.0;

            // --- Mínimo deslizante sobre L+1 muestras (cola monótona)
            pushMin (sampleIndex, g);
            while (minQ[(size_t) qHead].index <= sampleIndex - (L + 1)) qHead = (qHead + 1) % (int) minQ.size();
            const double h = minQ[(size_t) qHead].value;

            // --- Release: baja al instante, sube con constante de tiempo
            rel = h < rel ? h : h + (double) relCoef * (rel - h);

            // --- Media móvil (rampa de ataque)
            boxSum += rel - boxBuf[(size_t) boxPos];
            boxBuf[(size_t) boxPos] = rel;
            boxPos = (boxPos + 1) % (L + 1);
            const float gain = (float) std::min (1.0, boxSum / (double) (L + 1));

            // --- Salida: audio retrasado L + D
            const int readPos = (writePos - L - D) & (delaySize - 1);
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = delay[(size_t) c][(size_t) readPos] * gain;

            blockMaxGr = std::max (blockMaxGr, -20.0f * std::log10 (std::max (gain, 1.0e-6f)));
            writePos = (writePos + 1) & (delaySize - 1);
            ++sampleIndex;
        }
        grDb = blockMaxGr;
        maxGrDb = std::max (maxGrDb, blockMaxGr);
    }

private:
    struct Entry { long long index; double value; };

    void pushMin (long long idx, double v) noexcept
    {
        const int size = (int) minQ.size();
        while (qTail != qHead && minQ[(size_t) ((qTail - 1 + size) % size)].value >= v)
            qTail = (qTail - 1 + size) % size;
        minQ[(size_t) qTail] = { idx, v };
        qTail = (qTail + 1) % size;
    }

    double fs = 48000.0;
    float ceiling = 0.891f, inGain = 1.0f, inGainTarget = 1.0f;
    float lookaheadMs = 1.5f, releaseMs = 80.0f, relCoef = 0.0f;
    bool truePeak = true;
    int L = 72, maxL = 481;

    std::array<std::vector<float>, maxChannels> delay;
    std::array<TruePeakDetector, maxChannels> det;
    int delaySize = 1024, writePos = 0;
    long long sampleIndex = 0;

    std::vector<Entry> minQ;
    int qHead = 0, qTail = 0;
    std::vector<double> boxBuf;
    int boxPos = 0;
    double boxSum = 0.0, rel = 1.0;
    float grDb = 0.0f, maxGrDb = 0.0f;
};

/**
    Clipper suave: lineal hasta (1-knee)·techo y curva tanh hasta el techo.
    knee = 0 → clip duro. Debe ejecutarse sobremuestreado (lo hace el módulo).
*/
class SoftClipper
{
public:
    void setCeilingDb (float db) noexcept { ceiling = std::pow (10.0f, db / 20.0f); update(); }
    void setKnee (float k) noexcept       { knee = std::clamp (k, 0.0f, 1.0f); update(); }

    float process (float x) const noexcept
    {
        const float ax = std::fabs (x);
        if (ax <= a) return x;
        const float y = range > 0.0f ? a + range * std::tanh ((ax - a) / range) : ceiling;
        return std::copysign (std::min (y, ceiling), x);
    }

private:
    void update() noexcept { a = ceiling * (1.0f - knee); range = ceiling - a; }
    float ceiling = 1.0f, knee = 0.5f, a = 0.5f, range = 0.5f;
};

/** Dither TPDF con noise shaping opcional (error feedback de 1er orden). */
class Dither
{
public:
    void setBits (int b) noexcept    { bits = b; lsb = b > 0 ? std::ldexp (1.0f, -(b - 1)) : 0.0f; }
    void setShaping (bool s) noexcept { shaping = s; }
    void reset() noexcept            { err.fill (0.0f); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (bits <= 0 || bits >= 32) return;
        for (int c = 0; c < std::min (numCh, 2); ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i];
                const float y = shaping ? x - err[(size_t) c] : x;
                const float tpdf = (uniform() - uniform()) * lsb;
                float q = std::round ((y + tpdf) / lsb) * lsb;
                q = std::clamp (q, -1.0f, 1.0f - lsb);
                err[(size_t) c] = q - y;
                ch[c][i] = q;
            }
    }

private:
    float uniform() noexcept   // xorshift32 → [0,1)
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float) (state >> 8) * (1.0f / 16777216.0f);
    }

    int bits = 0;
    float lsb = 0.0f;
    bool shaping = false;
    std::array<float, 2> err {};
    std::uint32_t state = 0x9E3779B9u;
};

} // namespace dspcore
