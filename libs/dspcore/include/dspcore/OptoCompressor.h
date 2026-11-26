#pragma once
//
// Compresor óptico en topología feedback (como el hardware: el sidechain
// sale de la salida del atenuador, no de la entrada).
//
// Solo aplica la reducción de ganancia. El make-up y la etapa de salida
// (válvula / transistor) van en Saturation.h, porque el wrapper las corre
// sobremuestreadas.
//
#include "OptoCell.h"
#include <array>
#include <atomic>
#include <cmath>

namespace dspcore
{

enum class OptoModel { LA2A = 0, LA3A = 1 };
enum class OptoMode  { Compress = 0, Limit = 1 };

class OptoCompressor
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        for (auto& c : cells) c.prepare (fs, paramsFor (model));
        for (auto& f : emphasisLp) f.prepare (fs, 1000.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : cells) c.reset();
        for (auto& f : emphasisLp) f.reset();
        prevOut.fill (0.0f);
        grDb.store (0.0f);
    }

    void setModel (OptoModel m) noexcept
    {
        if (m == model) return;
        model = m;
        // Se cambian las constantes de tiempo sin resetear: el cambio no produce clicks.
        for (auto& c : cells) c.setParams (paramsFor (model));
    }

    /** 0–100, como el dial del equipo. 0 = sin compresión. */
    void setPeakReduction (float pr) noexcept
    {
        pr = std::fmax (0.0f, std::fmin (100.0f, pr));
        // Ley logarítmica: PR 20 ≈ 0 dB de drive, PR 100 ≈ +40 dB.
        // El fade cerca de 0 garantiza que PR = 0 es exactamente bypass.
        const float fade = std::fmin (1.0f, pr / 5.0f);
        drive = fade * std::pow (10.0f, (-10.0f + 0.5f * pr) / 20.0f);
    }

    void setMode (OptoMode m) noexcept   { mode = m; }

    /** 0–100. Realce de agudos en el sidechain (el "HF emphasis" del LA-2A). */
    void setEmphasis (float e) noexcept  { emphasis = 3.0f * std::fmax (0.0f, std::fmin (100.0f, e)) / 100.0f; }

    void setStereoLink (bool l) noexcept { link = l; }

    /** Procesa in-place. channels debe tener numChannels punteros. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = numChannels > maxChannels ? maxChannels : numChannels;
        const float exponent = mode == OptoMode::Compress ? 3.5f : 14.0f; // ≈ 3:1 / ≈ 10:1 medidos en la zona de 6–15 dB de GR
        float lastGain = 1.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            std::array<float, maxChannels> det {};
            for (int c = 0; c < numChannels; ++c)
            {
                const float y  = prevOut[(size_t) c];
                const float lp = emphasisLp[(size_t) c].process (y);
                det[(size_t) c] = std::fabs (y + emphasis * (y - lp)) * drive;
            }

            if (link || numChannels == 1)
            {
                float d = det[0];
                for (int c = 1; c < numChannels; ++c) d = std::fmax (d, det[(size_t) c]);
                const float gain = 1.0f / (1.0f + cells[0].process (d, exponent));
                for (int c = 0; c < numChannels; ++c)
                {
                    float& s = channels[c][n];
                    s *= gain;
                    prevOut[(size_t) c] = s;
                }
                lastGain = gain;
            }
            else
            {
                float minGain = 1.0f;
                for (int c = 0; c < numChannels; ++c)
                {
                    const float gain = 1.0f / (1.0f + cells[(size_t) c].process (det[(size_t) c], exponent));
                    float& s = channels[c][n];
                    s *= gain;
                    prevOut[(size_t) c] = s;
                    minGain = std::fmin (minGain, gain);
                }
                lastGain = minGain;
            }
        }

        grDb.store (-20.0f * std::log10 (std::fmax (lastGain, 1.0e-6f)), std::memory_order_relaxed);
    }

    /** Reducción de ganancia actual en dB (positivo). Thread-safe para el medidor de la UI. */
    float getGainReductionDb() const noexcept { return grDb.load (std::memory_order_relaxed); }

    static OptoCellParams paramsFor (OptoModel m) noexcept
    {
        return m == OptoModel::LA2A ? OptoCellParams::la2a() : OptoCellParams::la3a();
    }

private:
    double fs = 48000.0;
    OptoModel model = OptoModel::LA2A;
    OptoMode mode = OptoMode::Compress;
    float drive = 0.0f, emphasis = 0.0f;
    bool link = true;

    std::array<OptoCell, maxChannels> cells;
    std::array<OnePoleLowpass, maxChannels> emphasisLp;
    std::array<float, maxChannels> prevOut {};
    std::atomic<float> grDb { 0.0f };
};

} // namespace dspcore
