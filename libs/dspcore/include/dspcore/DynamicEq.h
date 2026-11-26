#pragma once
//
// EQ paramétrico/dinámico de N bandas en serie, fase mínima.
//
//  - Bell, shelves, cortes de 12/24/36/48 dB/oct (Butterworth en cascada) y notch.
//  - Cada banda puede procesar Stereo / Mid / Side / Left / Right.
//  - Modo dinámico por banda: un detector pasa-banda centrado en la propia banda
//    mide el nivel; por encima del umbral la ganancia se desplaza hasta
//    "dynRange" dB (negativo = comprime esa zona, positivo = la expande).
//  - Los parámetros se suavizan a "control rate" (cada 16 muestras) para que
//    mover un nodo no produzca zipper noise.
//
#include "OnePole.h"
#include "Svf.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace dspcore
{

enum class EqType { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch };
enum class Placement { Stereo = 0, Mid, Side, Left, Right };

struct EqBandParams
{
    bool enabled = false;
    EqType type = EqType::Bell;
    float freq = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.707f;
    int slopeDbPerOct = 12;          // solo cortes: 12, 24, 36, 48
    Placement placement = Placement::Stereo;
    float dynRangeDb = 0.0f;         // 0 = banda estática
    float thresholdDb = -20.0f;
    float attackMs = 10.0f;
    float releaseMs = 150.0f;

    bool isCut() const noexcept     { return type == EqType::LowCut || type == EqType::HighCut; }
    bool hasGain() const noexcept   { return type == EqType::Bell || type == EqType::LowShelf || type == EqType::HighShelf; }
    bool isDynamic() const noexcept { return hasGain() && std::fabs (dynRangeDb) > 0.01f; }
    int numSections() const noexcept { return isCut() ? std::clamp (slopeDbPerOct / 12, 1, 4) : 1; }
};

class EqBand
{
public:
    static constexpr int maxSections = 4;
    static constexpr float dynSlope = 0.75f;  // ≈ 4:1 por encima del umbral

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        smoothCoef = onePoleCoef (20.0f, fs / controlInterval);
        snapToTarget = true;
        reset();
    }

    void reset() noexcept
    {
        for (auto& ch : sections) for (auto& s : ch) s.reset();
        for (auto& d : detector) d.reset();
        env.fill (0.0f);
        dynGainDb.store (0.0f);
    }

    void setParams (const EqBandParams& p) noexcept
    {
        const bool topologyChanged = p.type != target.type || p.slopeDbPerOct != target.slopeDbPerOct
                                  || p.placement != target.placement || p.enabled != target.enabled;
        target = p;
        if (topologyChanged)
        {
            // Cambiar de tipo/pendiente/ubicación: se reinicia el estado para no arrastrar energía.
            for (auto& ch : sections) for (auto& s : ch) s.reset();
            snapToTarget = true;
        }
    }

    const EqBandParams& getParams() const noexcept { return target; }
    float getDynamicGainDb() const noexcept { return dynGainDb.load (std::memory_order_relaxed); }

    /** Actualiza coeficientes (suavizado + dinámica). Se llama cada controlInterval muestras. */
    void updateControl() noexcept
    {
        if (! target.enabled)
            return;

        const float tf = std::log (target.freq), tg = target.gainDb, tq = std::log (target.q);
        if (snapToTarget)
        {
            logFreq = tf; gain = tg; logQ = tq;
            snapToTarget = false;
        }
        else
        {
            logFreq = tf + smoothCoef * (logFreq - tf);
            gain    = tg + smoothCoef * (gain - tg);
            logQ    = tq + smoothCoef * (logQ - tq);
        }

        float dyn = 0.0f;
        if (target.isDynamic())
        {
            const float levelDb = 20.0f * std::log10 (std::max ({ env[0], env[1], 1.0e-9f }));
            const float over = levelDb - target.thresholdDb;
            if (over > 0.0f)
                dyn = std::copysign (std::min (std::fabs (target.dynRangeDb), over * dynSlope), target.dynRangeDb);
        }
        dynGainDb.store (dyn, std::memory_order_relaxed);

        const double f = std::exp ((double) logFreq), q = std::exp ((double) logQ);
        designSections (f, q, gain + dyn);

        if (target.isDynamic())
        {
            detCoeffs = SvfCoeffs::design (FilterShape::BandPass, f, fs, std::max (q, 0.5), 0.0);
            atkCoef = onePoleCoef (target.attackMs, fs);
            relCoef = onePoleCoef (target.releaseMs, fs);
        }
    }

    /** Procesa una muestra estéreo in-place (r se ignora si numCh == 1). */
    void process (float& l, float& r, int numCh) noexcept
    {
        if (! target.enabled)
            return;

        const bool dynamic = target.isDynamic();
        const auto pl = numCh == 1 ? Placement::Left : target.placement;

        switch (pl)
        {
            case Placement::Stereo:
                if (dynamic) { detect (0, l); detect (1, r); }
                l = run (0, l);
                r = run (1, r);
                break;
            case Placement::Mid:
            case Placement::Side:
            {
                float m = 0.5f * (l + r), s = 0.5f * (l - r);
                float& x = pl == Placement::Mid ? m : s;
                if (dynamic) detect (0, x);
                x = run (0, x);
                l = m + s;
                r = m - s;
                break;
            }
            case Placement::Left:
                if (dynamic) detect (0, l);
                l = run (0, l);
                break;
            case Placement::Right:
                if (dynamic) detect (0, r);
                r = run (0, r);
                break;
        }
    }

    /** Respuesta en dB de unos parámetros (lo que dibuja la UI). extraGainDb = estado dinámico. */
    static float responseDb (const EqBandParams& p, double sampleRate, double freqHz, float extraGainDb = 0.0f) noexcept
    {
        if (! p.enabled)
            return 0.0f;
        std::array<SvfCoeffs, maxSections> c;
        const int n = design (p.type, p.freq, p.q, p.gainDb + extraGainDb, p.numSections(), sampleRate, c);
        const double w = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
        double mag = 1.0;
        for (int i = 0; i < n; ++i)
            mag *= std::abs (c[(size_t) i].response (w));
        return (float) (20.0 * std::log10 (std::max (mag, 1.0e-12)));
    }

    static constexpr int controlInterval = 16;

private:
    static int design (EqType type, double f, double q, double gainDb, int sectionsForCut, double fs,
                       std::array<SvfCoeffs, maxSections>& out) noexcept
    {
        switch (type)
        {
            case EqType::Bell:      out[0] = SvfCoeffs::design (FilterShape::Bell, f, fs, q, gainDb); return 1;
            case EqType::LowShelf:  out[0] = SvfCoeffs::design (FilterShape::LowShelf, f, fs, q, gainDb); return 1;
            case EqType::HighShelf: out[0] = SvfCoeffs::design (FilterShape::HighShelf, f, fs, q, gainDb); return 1;
            case EqType::Notch:     out[0] = SvfCoeffs::design (FilterShape::Notch, f, fs, q, 0.0); return 1;
            case EqType::LowCut:
            case EqType::HighCut:
            {
                // Butterworth de orden 2N; la Q del usuario escala la resonancia (0.707 = Butterworth puro).
                const int n = sectionsForCut, order = 2 * n;
                const auto shape = type == EqType::LowCut ? FilterShape::HighPass : FilterShape::LowPass;
                for (int i = 0; i < n; ++i)
                {
                    const double bq = 1.0 / (2.0 * std::cos ((2.0 * i + 1.0) * 3.14159265358979323846 / (2.0 * order)));
                    const double qi = i == n - 1 ? bq * q / 0.70710678 : bq;  // resonancia solo en la última sección
                    out[(size_t) i] = SvfCoeffs::design (shape, f, fs, qi, 0.0);
                }
                return n;
            }
        }
        return 0;
    }

    void designSections (double f, double q, double gainDb) noexcept
    {
        numActive = design (target.type, f, q, gainDb, target.numSections(), fs, coeffs);
    }

    float run (int ch, float x) noexcept
    {
        auto& st = sections[(size_t) ch];
        for (int i = 0; i < numActive; ++i)
            x = st[(size_t) i].process (x, coeffs[(size_t) i]);
        return x;
    }

    void detect (int ch, float x) noexcept
    {
        const float b = std::fabs (detector[(size_t) ch].process (x, detCoeffs));
        float& e = env[(size_t) ch];
        e = flushDenormal (b + (b > e ? atkCoef : relCoef) * (e - b));
    }

    double fs = 48000.0;
    EqBandParams target;
    bool snapToTarget = true;
    float smoothCoef = 0.0f, logFreq = 0.0f, gain = 0.0f, logQ = 0.0f;

    std::array<SvfCoeffs, maxSections> coeffs {};
    int numActive = 0;
    std::array<std::array<Svf, maxSections>, 2> sections {};

    SvfCoeffs detCoeffs {};
    std::array<Svf, 2> detector {};
    std::array<float, 2> env {};
    float atkCoef = 0.0f, relCoef = 0.0f;
    std::atomic<float> dynGainDb { 0.0f };
};

template <int NumBands>
class DynamicEq
{
public:
    static constexpr int numBands = NumBands;

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        for (auto& b : bands) b.prepare (fs);
        outGain = outTarget;
        counter = 0;
    }

    void reset() noexcept { for (auto& b : bands) b.reset(); counter = 0; }

    void setBand (int i, const EqBandParams& p) noexcept { bands[(size_t) i].setParams (p); }
    void setOutputGainDb (float db) noexcept { outTarget = std::pow (10.0f, db / 20.0f); }

    const EqBand& band (int i) const noexcept { return bands[(size_t) i]; }
    double getSampleRate() const noexcept { return fs; }

    void process (float* l, float* r, int numCh, int numSamples) noexcept
    {
        float dummy = 0.0f;
        for (int n = 0; n < numSamples; ++n)
        {
            if (counter == 0)
                for (auto& b : bands) b.updateControl();
            counter = (counter + 1) % EqBand::controlInterval;

            float& L = l[n];
            float& R = numCh > 1 ? r[n] : dummy;
            for (auto& b : bands)
                b.process (L, R, numCh);

            outGain += 0.001f * (outTarget - outGain);
            L *= outGain;
            if (numCh > 1) R *= outGain;
        }
    }

private:
    double fs = 48000.0;
    std::array<EqBand, static_cast<std::size_t> (NumBands)> bands;
    float outGain = 1.0f, outTarget = 1.0f;
    int counter = 0;
};

} // namespace dspcore
