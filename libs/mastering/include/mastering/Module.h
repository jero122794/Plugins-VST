#pragma once
//
// Framework de módulos de mastering. Un mismo módulo sirve para:
//   - un plugin suelto (parámetros sin prefijo)
//   - una sección de la cadena MasterChain (parámetros con prefijo, p. ej. "lim_")
//
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace mastering
{

using APVTS = juce::AudioProcessorValueTreeState;
using Layout = APVTS::ParameterLayout;

class Module
{
public:
    virtual ~Module() = default;

    /** Obtiene punteros a sus parámetros. Se llama una vez, antes de prepare(). */
    virtual void bind (APVTS& state, const juce::String& prefix)
    {
        apvts = &state;
        paramPrefix = prefix;
    }

    virtual void prepare (double sampleRate, int maxBlock, int numChannels) = 0;

    /** Procesa in-place. Lee sus propios parámetros. numSamples ≤ maxBlock. */
    virtual void process (float* const* ch, int numCh, int numSamples) noexcept = 0;

    virtual int getLatency() const noexcept { return 0; }

    /** Valor para el medidor del módulo (reducción de ganancia, clip…), en dB positivos. */
    virtual float getMeterDb() const noexcept { return 0.0f; }
    virtual juce::String getMeterLabel() const { return {}; }

    virtual std::unique_ptr<juce::Component> createPanel() = 0;

    /** Procesa y registra los picos de entrada/salida para los medidores de la UI. Usar esto, no process(). */
    void run (float* const* ch, int numCh, int numSamples) noexcept
    {
        inPeak.store (std::max (inPeak.load (std::memory_order_relaxed), peakOf (ch, numCh, numSamples)), std::memory_order_relaxed);
        process (ch, numCh, numSamples);
        outPeak.store (std::max (outPeak.load (std::memory_order_relaxed), peakOf (ch, numCh, numSamples)), std::memory_order_relaxed);
        const float m = getMeterDb();
        meterHold.store (std::max (meterHold.load (std::memory_order_relaxed), m), std::memory_order_relaxed);
        blocksProcessed.fetch_add (1, std::memory_order_relaxed);
    }

    /** Cambia cada vez que llega audio: la UI lo usa para no confundir "sin audio nuevo" con silencio. */
    std::uint32_t getBlocksProcessed() const noexcept { return blocksProcessed.load (std::memory_order_relaxed); }

    /** Pico desde la última lectura (la UI lee ~30 veces por segundo: no se pierde ningún pico). */
    float takeInputPeak() noexcept  { return inPeak.exchange (0.0f); }
    float takeOutputPeak() noexcept { return outPeak.exchange (0.0f); }
    float takeMeterHold() noexcept  { return meterHold.exchange (0.0f); }

protected:
    static float peakOf (float* const* ch, int numCh, int n) noexcept
    {
        float p = 0.0f;
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i) p = std::max (p, std::fabs (ch[c][i]));
        return p;
    }

    std::atomic<float>* param (const char* name) const { return apvts->getRawParameterValue (paramPrefix + name); }
    juce::String pid (const char* name) const { return paramPrefix + name; }

    APVTS* apvts = nullptr;
    juce::String paramPrefix;

private:
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, meterHold { 0.0f };
    std::atomic<std::uint32_t> blocksProcessed { 0 };
};

struct ModuleInfo
{
    juce::String id;          // "limiter"
    juce::String prefix;      // "lim_"
    juce::String name;        // "Limiter"
    juce::String description;
    void (*addParameters) (Layout&, const juce::String& prefix);
    std::unique_ptr<Module> (*create)();
    bool inChain = true;
};

/** Registro de módulos en el orden por defecto de la cadena. */
const std::vector<ModuleInfo>& moduleRegistry();
const ModuleInfo* findModule (const juce::String& id);

/**
    Envoltorio de cada módulo dentro de la cadena: bypass con crossfade y
    latencia compensada (la señal en bypass se retrasa lo mismo que la procesada,
    así que activar/desactivar un módulo no desalinea nada).
*/
class ModuleSlot
{
public:
    ModuleSlot (const ModuleInfo& info, std::unique_ptr<Module> m) : moduleInfo (info), module (std::move (m)) {}

    void bind (APVTS& state)
    {
        module->bind (state, moduleInfo.prefix);
        onParam = state.getRawParameterValue (moduleInfo.prefix + "on");
    }

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        module->prepare (sampleRate, maxBlock, numChannels);
        for (auto& d : dry) d.assign ((size_t) maxBlock, 0.0f);
        for (auto& d : delay) d.assign ((size_t) delaySize, 0.0f);
        fadeStep = (float) (1.0 / (0.02 * sampleRate));
        fade = isOn() ? 1.0f : 0.0f;
        writePos = 0;
    }

    bool isOn() const noexcept { return onParam->load() > 0.5f; }
    int getLatency() const noexcept { return module->getLatency(); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        const int L = juce::jlimit (0, delaySize - 1, module->getLatency());
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i)
            {
                const int w = (writePos + i) & (delaySize - 1);
                delay[(size_t) c][(size_t) w] = ch[c][i];
                dry[(size_t) c][(size_t) i] = delay[(size_t) c][(size_t) ((w - L) & (delaySize - 1))];
            }
        writePos = (writePos + n) & (delaySize - 1);

        // El módulo procesa siempre (también en bypass): al reactivarlo su estado está al día.
        module->run (ch, numCh, n);

        const bool on = isOn();
        if (on && fade >= 1.0f)
            return;
        const float target = on ? 1.0f : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            fade = target > fade ? std::min (target, fade + fadeStep) : std::max (target, fade - fadeStep);
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = dry[(size_t) c][(size_t) i] + fade * (ch[c][i] - dry[(size_t) c][(size_t) i]);
        }
    }

    const ModuleInfo& moduleInfo;
    std::unique_ptr<Module> module;

private:
    static constexpr int delaySize = 1 << 16;   // cubre el EQ en fase lineal hasta 192 kHz (32768 taps / 2 + bloque)
    std::atomic<float>* onParam = nullptr;
    std::array<std::vector<float>, 2> dry, delay;
    int writePos = 0;
    float fade = 1.0f, fadeStep = 0.001f;
};

} // namespace mastering
