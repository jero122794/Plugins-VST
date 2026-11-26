#pragma once
//
// Módulos de mastering: etapa de entrada, compresor de bus (VCA), saturación e imagen estéreo.
// El limitador, el clipper y el dither están en Limiter.h.
//
#include "OnePole.h"
#include "Svf.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dspcore
{

// ============================================================================
/** Ganancia, filtro subsónico / DC, polaridad y suma a mono (para comprobar compatibilidad). */
class InputStage
{
public:
    enum class Subsonic { Off = 0, DC, Hz20, Hz30 };

    void prepare (double sampleRate) { fs = sampleRate; update(); reset(); }
    void reset() { for (auto& c : hp) for (auto& s : c) s.reset(); }

    void setGainDb (float db) noexcept       { gainTarget = std::pow (10.0f, db / 20.0f); }
    void setSubsonic (Subsonic s) noexcept   { if (s != subsonic) { subsonic = s; update(); } }
    void setInvert (bool l, bool r) noexcept { invL = l; invR = r; }
    void setMono (bool m) noexcept           { mono = m; }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            gain += 0.002f * (gainTarget - gain);
            float l = ch[0][i] * gain * (invL ? -1.0f : 1.0f);
            float r = numCh > 1 ? ch[1][i] * gain * (invR ? -1.0f : 1.0f) : 0.0f;
            if (subsonic != Subsonic::Off)
            {
                for (int k = 0; k < 2; ++k)  // 24 dB/oct (Butterworth de 4º orden)
                {
                    l = hp[0][(size_t) k].process (l, coeffs[(size_t) k]);
                    if (numCh > 1) r = hp[1][(size_t) k].process (r, coeffs[(size_t) k]);
                }
            }
            if (mono && numCh > 1) l = r = 0.5f * (l + r);
            ch[0][i] = l;
            if (numCh > 1) ch[1][i] = r;
        }
    }

private:
    void update()
    {
        const double f = subsonic == Subsonic::DC ? 5.0 : subsonic == Subsonic::Hz20 ? 20.0 : 30.0;
        coeffs[0] = SvfCoeffs::design (FilterShape::HighPass, f, fs, 0.5411961, 0.0);
        coeffs[1] = SvfCoeffs::design (FilterShape::HighPass, f, fs, 1.3065630, 0.0);
    }

    double fs = 48000.0;
    float gain = 1.0f, gainTarget = 1.0f;
    Subsonic subsonic = Subsonic::DC;
    bool invL = false, invR = false, mono = false;
    std::array<SvfCoeffs, 2> coeffs {};
    std::array<std::array<Svf, 2>, 2> hp {};
};

// ============================================================================
/**
    Compresor de bus tipo VCA, feedforward, en dominio logarítmico
    (detector "smooth branching" de Giannoulis, Massberg y Reiss, JAES 2012).
    Release automático: dos seguidores (rápido y lento); la compresión sostenida
    carga el lento y alarga el release; los transitorios solo cargan el rápido.
*/
class BusCompressor
{
public:
    void prepare (double sampleRate) { fs = sampleRate; updateTimes(); updateSc(); reset(); }
    void reset() { yFast = ySlow = peakEnv = 0.0f; for (auto& s : scHp) s.reset(); grDb = 0.0f; }

    void setThresholdDb (float t) noexcept { threshold = t; }
    void setRatio (float r) noexcept       { ratio = std::max (1.0f, r); }
    void setKneeDb (float k) noexcept      { knee = std::max (0.0f, k); }
    void setAttackMs (float a) noexcept    { attackMs = a; updateTimes(); }
    void setReleaseMs (float r) noexcept   { releaseMs = r; updateTimes(); }
    void setAutoRelease (bool a) noexcept  { autoRelease = a; updateTimes(); }
    void setMakeupDb (float m) noexcept    { makeupTarget = std::pow (10.0f, m / 20.0f); }
    void setMix (float m) noexcept         { mix = std::clamp (m, 0.0f, 1.0f); }
    void setSidechainHpf (float hz) noexcept { if (hz != scHz) { scHz = hz; updateSc(); } }

    float getGainReductionDb() const noexcept { return grDb; }

    /** Curva estática: reducción de ganancia (dB, ≥ 0) para un nivel de entrada. */
    float staticGainReduction (float xDb) const noexcept
    {
        const float over = xDb - threshold;
        float y;
        if (2.0f * over < -knee)               y = xDb;
        else if (2.0f * std::fabs (over) <= knee && knee > 0.0f)
            y = xDb + (1.0f / ratio - 1.0f) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        else                                    y = threshold + over / ratio;
        return xDb - y;
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float level = 0.0f;
            for (int c = 0; c < std::min (numCh, 2); ++c)
            {
                const float x = scHz > 0.0f ? scHp[(size_t) c].process (ch[c][i], scCoeffs) : ch[c][i];
                level = std::max (level, std::fabs (x));
            }
            // Detector de pico: ataque instantáneo, release de 10 ms (evita seguir el rizado de cada ciclo)
            peakEnv = level > peakEnv ? level : level + aPeak * (peakEnv - level);
            const float xDb = 20.0f * std::log10 (std::max (peakEnv, 1.0e-9f));
            const float target = staticGainReduction (xDb);

            yFast = target > yFast ? target + aAtt * (yFast - target) : target + aRel * (yFast - target);
            float gr = yFast;
            if (autoRelease)
            {
                ySlow = target > ySlow ? target + aSlowAtt * (ySlow - target) : target + aSlowRel * (ySlow - target);
                gr = std::max (yFast, ySlow);
            }

            makeup += 0.002f * (makeupTarget - makeup);
            const float g = std::pow (10.0f, -gr / 20.0f) * makeup;
            for (int c = 0; c < std::min (numCh, 2); ++c)
            {
                const float x = ch[c][i];
                ch[c][i] = x + mix * (x * g - x);
            }
            maxGr = std::max (maxGr, gr);
        }
        grDb = maxGr;
    }

private:
    void updateTimes()
    {
        aAtt = onePoleCoef (attackMs, fs);
        aRel = onePoleCoef (autoRelease ? 100.0f : releaseMs, fs);
        // Seguidor lento: solo se carga con compresión sostenida (segundos); una ráfaga apenas lo mueve
        aSlowAtt = onePoleCoef (2000.0f, fs);
        aSlowRel = onePoleCoef (600.0f, fs);
        aPeak = onePoleCoef (10.0f, fs);
    }
    void updateSc() { if (scHz > 0.0f) scCoeffs = SvfCoeffs::design (FilterShape::HighPass, scHz, fs, 0.7071, 0.0); }

    double fs = 48000.0;
    float threshold = -18.0f, ratio = 2.0f, knee = 6.0f, attackMs = 10.0f, releaseMs = 200.0f;
    bool autoRelease = false;
    float makeup = 1.0f, makeupTarget = 1.0f, mix = 1.0f, scHz = 0.0f;
    float aAtt = 0, aRel = 0, aSlowAtt = 0, aSlowRel = 0, aPeak = 0;
    float yFast = 0, ySlow = 0, peakEnv = 0, grDb = 0;
    SvfCoeffs scCoeffs {};
    std::array<Svf, 2> scHp {};
};

// ============================================================================
/** Saturación (ejecutar sobremuestreada). Las curvas tienen ganancia 1 en pequeña señal. */
class Saturator
{
public:
    enum class Mode { Tape = 0, Tube, Transformer };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        pre  = SvfCoeffs::design (FilterShape::HighShelf, 3500.0, fs, 0.7071, 6.0);
        post = SvfCoeffs::design (FilterShape::HighShelf, 3500.0, fs, 0.7071, -6.0);
        for (auto& l : xfmrLp) l.prepare (fs, 150.0f);
        for (auto& d : dc) d.prepare (fs, 8.0f);
        reset();
    }
    void reset()
    {
        for (auto& s : preF) s.reset();
        for (auto& s : postF) s.reset();
        for (auto& l : xfmrLp) l.reset();
        for (auto& d : dc) d.reset();
    }

    void setMode (Mode m) noexcept        { mode = m; }
    void setDriveDb (float d) noexcept    { drive = std::pow (10.0f, d / 20.0f); }
    void setOutputDb (float d) noexcept   { output = std::pow (10.0f, d / 20.0f); }
    void setMix (float m) noexcept        { mix = std::clamp (m, 0.0f, 1.0f); }
    void setAutoGain (bool a) noexcept    { autoGain = a; }

    float processSample (int c, float x) noexcept
    {
        float y = x * drive;
        switch (mode)
        {
            case Mode::Tape:
                // Pre-énfasis de agudos → saturación simétrica → de-énfasis: los agudos saturan antes, como en cinta
                y = postF[(size_t) c].process (std::tanh (preF[(size_t) c].process (y, pre)), post);
                break;
            case Mode::Tube:
            {
                constexpr float k = 1.0f, b = 0.25f;
                const float tb = std::tanh (k * b);
                y = (std::tanh (k * (y + b)) - tb) / (k * (1.0f - tb * tb));
                break;
            }
            case Mode::Transformer:
            {
                const float lf = xfmrLp[(size_t) c].process (y);
                y = (y - lf) + std::tanh (lf * 1.5f) / 1.5f + 0.15f * (std::tanh (y) - y);
                break;
            }
        }
        y = dc[(size_t) c].process (y);
        if (autoGain) y /= drive;
        y *= output;
        return x + mix * (y - x);
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        for (int c = 0; c < std::min (numCh, 2); ++c)
            for (int i = 0; i < n; ++i)
                ch[c][i] = processSample (c, ch[c][i]);
    }

private:
    double fs = 48000.0;
    Mode mode = Mode::Tape;
    float drive = 1.0f, output = 1.0f, mix = 1.0f;
    bool autoGain = true;
    SvfCoeffs pre {}, post {};
    std::array<Svf, 2> preF {}, postF {};
    std::array<OnePoleLowpass, 2> xfmrLp {};
    std::array<DcBlocker, 2> dc {};
};

// ============================================================================
/** Ancho estéreo M/S y graves en mono (Linkwitz-Riley de 24 dB/oct sobre el canal Side). */
class StereoImager
{
public:
    void prepare (double sampleRate) { fs = sampleRate; update(); reset(); }
    void reset() { for (auto& s : hp) s.reset(); }

    void setWidth (float w) noexcept       { width = std::max (0.0f, w); }
    void setBassMonoHz (float hz) noexcept { if (hz != bassHz) { bassHz = hz; update(); } }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (numCh < 2) return;
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (ch[0][i] + ch[1][i]);
            float s = 0.5f * (ch[0][i] - ch[1][i]);
            if (bassHz > 0.0f)
                for (int k = 0; k < 2; ++k) s = hp[(size_t) k].process (s, coeffs);  // LR4 = 2 × Butterworth 2º orden
            s *= width;
            ch[0][i] = m + s;
            ch[1][i] = m - s;
        }
    }

private:
    void update() { if (bassHz > 0.0f) coeffs = SvfCoeffs::design (FilterShape::HighPass, bassHz, fs, 0.7071, 0.0); }

    double fs = 48000.0;
    float width = 1.0f, bassHz = 0.0f;
    SvfCoeffs coeffs {};
    std::array<Svf, 2> hp {};
};

} // namespace dspcore
