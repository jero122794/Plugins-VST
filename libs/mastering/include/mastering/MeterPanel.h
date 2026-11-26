#pragma once
//
// Panel de medición de mastering: LUFS (M/S/I), LRA, true peak, PLR, correlación,
// histograma de loudness, historial y normalización por plataforma.
//
#include "Module.h"
#include <dspcore/Loudness.h>
#include <studioui/StudioLookAndFeel.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace mastering
{

/** Plataformas y su objetivo de normalización (LUFS integrados). [Probable]: valores publicados por cada servicio a 2025. */
inline const juce::StringArray& platformNames()
{
    static const juce::StringArray n { "Ninguna", "Spotify (-14 LUFS)", "Apple Music (-16 LUFS)", "YouTube (-14 LUFS)",
                                       "Tidal (-14 LUFS)", "Amazon Music (-14 LUFS)", "Deezer (-15 LUFS)" };
    return n;
}
inline float platformTarget (int index)
{
    static constexpr float t[] = { 0.0f, -14.0f, -16.0f, -14.0f, -14.0f, -14.0f, -15.0f };
    return t[juce::jlimit (0, 6, index)];
}

/** Correlación de fase L/R suavizada (~300 ms). +1 = mono, 0 = sin relación, -1 = fase invertida. */
struct CorrelationMeter
{
    void process (const float* const* ch, int numCh, int n) noexcept
    {
        if (numCh < 2) { value.store (1.0f); return; }
        constexpr double a = 0.9995;   // ~300 ms a 48 kHz
        for (int i = 0; i < n; ++i)
        {
            const double l = ch[0][i], r = ch[1][i];
            lr = a * lr + (1 - a) * l * r;
            ll = a * ll + (1 - a) * l * l;
            rr = a * rr + (1 - a) * r * r;
        }
        const double den = std::sqrt (ll * rr);
        value.store (den > 1e-10 ? (float) (lr / den) : 1.0f);
    }
    double lr = 0, ll = 0, rr = 0;
    std::atomic<float> value { 1.0f };
};

class MeterPanel final : public juce::Component, private juce::Timer
{
public:
    struct Sources
    {
        dspcore::LoudnessMeter* output = nullptr;
        dspcore::LoudnessMeter* input = nullptr;      // opcional: para mostrar la ganancia de sonoridad
        std::atomic<float>* correlation = nullptr;
        std::atomic<float>* platform = nullptr;       // índice de platformNames()
    };

    explicit MeterPanel (Sources);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawHistogram (juce::Graphics&, juce::Rectangle<float>);
    void drawHistory (juce::Graphics&, juce::Rectangle<float>);

    Sources src;
    juce::TextButton resetButton { "Reset" }, histButton { "Histograma" }, historyButton { "Historial" };
    bool showHistory = false;

    // Historial de short-term (últimos 60 s, muestreado por el timer a 10 Hz)
    static constexpr int historyLen = 600;
    std::array<float, historyLen> history {};
    int historyPos = 0, timerTicks = 0;

    juce::Rectangle<int> graphArea;
};

} // namespace mastering
