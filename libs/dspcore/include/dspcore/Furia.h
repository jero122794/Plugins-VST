#pragma once
//
// Furia: compresor "de carácter" (familia de los compresores digitales-analógicos de los 90):
// input contra umbral fijo, ratios de 1:1 (solo color) a "Nuke", modo Opto, detector con HP,
// realce de banda y enlace estéreo, y distorsión de 2º o 3º armónico.
//
// Este archivo contiene la parte lineal y la de dinámica. La distorsión es una no linealidad
// sin memoria (Furia::distort) que el plugin ejecuta sobremuestreada.
//
#include "OnePole.h"
#include "Svf.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dspcore
{

class FuriaCompressor
{
public:
    enum class Ratio { R1 = 0, R2, R3, R4, R6, R10, R20, Nuke, Opto };
    enum class Dist { Off = 0, Second, Third };

    static constexpr float thresholdDb = -18.0f;   // umbral fijo: el input decide cuánto se comprime

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& s : scHp) s.reset();
        for (auto& s : scBand) s.reset();
        for (auto& s : audioHp) s.reset();
        scHpC = SvfCoeffs::design (FilterShape::HighPass, 100.0, fs, 0.5, 0.0);      // suave (Q 0.5)
        scBandC = SvfCoeffs::design (FilterShape::Bell, 6000.0, fs, 1.0, 6.0);       // +6 dB a 6 kHz en el detector
        audioHpC = SvfCoeffs::design (FilterShape::HighPass, 80.0, fs, 0.707, 0.0);
        updateTimes();
        reset();
    }

    void reset()
    {
        env.fill (0.0f);
        gr.fill (0.0f);
        slow.fill (0.0f);
        grDb = 0.0f;
        peakOut = 0.0f;
    }

    // --- Controles (escala 0–10 como en el hardware)
    void setInput (float knob) noexcept   { inputGain = std::pow (10.0f, (-10.0f + 4.0f * std::clamp (knob, 0.0f, 10.0f)) / 20.0f); }
    void setOutput (float knob) noexcept  { outputGain = std::pow (10.0f, (std::clamp (knob, 0.0f, 10.0f) - 5.0f) * 4.0f / 20.0f); }
    void setAttack (float knob) noexcept  { attackKnob = std::clamp (knob, 1.0f, 10.0f); updateTimes(); }
    void setRelease (float knob) noexcept { releaseKnob = std::clamp (knob, 1.0f, 10.0f); updateTimes(); }
    void setRatio (Ratio r) noexcept      { ratio = r; updateTimes(); }
    void setMix (float m) noexcept        { mix = std::clamp (m, 0.0f, 1.0f); }
    void setDetector (bool hp, bool band, bool link) noexcept { detHp = hp; detBand = band; stereoLink = link; }
    void setAudioHp (bool on) noexcept    { audioHpOn = on; }

    /** Tiempos (ms) que corresponden a las perillas: útiles para la UI y los tests. */
    static float attackMsFor (float knob) noexcept  { return 0.05f * std::pow (30.0f / 0.05f, (std::clamp (knob, 1.0f, 10.0f) - 1.0f) / 9.0f); }
    static float releaseMsFor (float knob) noexcept { return 50.0f * std::pow (3500.0f / 50.0f, (std::clamp (knob, 1.0f, 10.0f) - 1.0f) / 9.0f); }

    /** Curva estática: reducción de ganancia (dB) para un nivel de detector (dB). */
    float staticGr (float xDb) const noexcept
    {
        if (ratio == Ratio::R1) return 0.0f;
        static constexpr float slopes[] = { 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 10.0f, 20.0f, 1000.0f, 10.0f };
        const float r = slopes[(int) ratio];
        const float knee = ratio == Ratio::Nuke ? 1.0f : (ratio == Ratio::R2 || ratio == Ratio::R3 ? 12.0f : 8.0f);
        const float over = xDb - thresholdDb;
        if (2.0f * over <= -knee) return 0.0f;
        if (2.0f * over < knee)
        {
            const float o = over + knee * 0.5f;
            return (1.0f - 1.0f / r) * o * o / (2.0f * knee);
        }
        return (1.0f - 1.0f / r) * over;
    }

    /** Dinámica (sin distorsión): procesa in-place. La entrada ya debe traer el input aplicado (inputGain()). */
    void processDynamics (float* const* ch, int numCh, int n) noexcept
    {
        numCh = std::min (numCh, 2);
        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            std::array<float, 2> det {};
            for (int c = 0; c < numCh; ++c)
            {
                float x = ch[c][i] * inputGain;
                if (audioHpOn) x = audioHp[(size_t) c].process (x, audioHpC);
                ch[c][i] = x;
                float s = x;
                if (detHp) s = scHp[(size_t) c].process (s, scHpC);
                if (detBand) s = scBand[(size_t) c].process (s, scBandC);
                det[(size_t) c] = std::fabs (s);
            }
            if (stereoLink && numCh == 2) det[0] = det[1] = std::max (det[0], det[1]);

            for (int c = 0; c < numCh; ++c)
            {
                // Detector de pico con release corto (sigue la envolvente sin el rizado de cada ciclo)
                float& e = env[(size_t) c];
                e = det[(size_t) c] > e ? det[(size_t) c] : det[(size_t) c] + peakRel * (e - det[(size_t) c]);
                const float target = staticGr (20.0f * std::log10 (std::max (e, 1.0e-9f)));

                // Balística en dB: ataque/release; en Opto el release se alarga con la compresión sostenida
                float& g = gr[(size_t) c];
                float rel = relCoef;
                if (ratio == Ratio::Opto)
                {
                    float& m = slow[(size_t) c];
                    m = target > m ? target + optoCharge * (m - target) : target + optoDischarge * (m - target);
                    rel = std::min (0.99999f, relCoef + (1.0f - relCoef) * std::clamp (m / 12.0f, 0.0f, 1.0f) * 0.9f);
                }
                g = target > g ? target + attCoef * (g - target) : target + rel * (g - target);
                const float gain = std::pow (10.0f, -g / 20.0f);
                const float wet = ch[c][i] * gain;
                ch[c][i] = wet;   // el mix y la salida se aplican tras la distorsión
                maxGr = std::max (maxGr, g);
            }
        }
        grDb = maxGr;
    }

    /** Distorsión sin memoria (ejecutar sobremuestreada). Más intensa cuanto más fuerte llega la señal. */
    static float distort (float x, Dist mode) noexcept
    {
        switch (mode)
        {
            case Dist::Off: return x;
            case Dist::Second:
            {
                // Asimétrica → 2º armónico (cálido, tipo válvula); ganancia 1 en pequeña señal
                constexpr float b = 0.3f;
                const float tb = std::tanh (b);
                return (std::tanh (x + b) - tb) / (1.0f - tb * tb);
            }
            case Dist::Third:
                // Simétrica → 3º armónico (tipo cinta)
                return std::tanh (x * 1.4f) / 1.4f;
        }
        return x;
    }

    float getGainReductionDb() const noexcept { return grDb; }
    float getInputGain() const noexcept { return inputGain; }
    float getOutputGain() const noexcept { return outputGain; }
    float getMix() const noexcept { return mix; }

private:
    void updateTimes()
    {
        float a = attackMsFor (attackKnob), r = releaseMsFor (releaseKnob);
        if (ratio == Ratio::Nuke) { a = std::min (a, 1.0f); r = std::max (r * 0.5f, 30.0f); }   // muro: ataque rápido
        attCoef = onePoleCoef (a, fs);
        relCoef = onePoleCoef (r, fs);
        peakRel = onePoleCoef (8.0f, fs);
        optoCharge = onePoleCoef (1500.0f, fs);
        optoDischarge = onePoleCoef (4000.0f, fs);
    }

    double fs = 48000.0;
    float inputGain = 1.0f, outputGain = 1.0f, mix = 1.0f;
    float attackKnob = 5.0f, releaseKnob = 5.0f;
    Ratio ratio = Ratio::R4;
    bool detHp = false, detBand = false, stereoLink = true, audioHpOn = false;
    float attCoef = 0, relCoef = 0, peakRel = 0, optoCharge = 0, optoDischarge = 0;
    SvfCoeffs scHpC {}, scBandC {}, audioHpC {};
    std::array<Svf, 2> scHp {}, scBand {}, audioHp {};
    std::array<float, 2> env {}, gr {}, slow {};
    float grDb = 0.0f, peakOut = 0.0f;
};

} // namespace dspcore
