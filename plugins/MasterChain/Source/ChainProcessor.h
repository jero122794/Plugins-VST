#pragma once

#include <mastering/Module.h>
#include <mastering/MeterPanel.h>
#include <dspcore/Loudness.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

/**
    Cadena de mastering. Orden protegido:
      Input → [EQ, Compresor, Saturación, Imagen, Clipper en el orden que elijas] → Limitador → Dither
    La entrada va siempre primero; el limitador y el dither, siempre al final.
*/
class MasterChainProcessor final : public juce::AudioProcessor
{
public:
    MasterChainProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Para la UI
    int getNumSlots() const noexcept { return (int) slots.size(); }
    mastering::ModuleSlot& getSlot (int index) { return *slots[(size_t) index]; }
    int slotAt (int position) const noexcept { return slotAt (packedOrder.load(), position); }
    bool isReorderable (int position) const noexcept { return position >= firstMovable && position <= lastMovable; }
    /** Mueve el módulo de la posición indicada una posición arriba (-1) o abajo (+1). */
    void moveModule (int position, int direction);

    dspcore::LoudnessMeter inputMeter, outputMeter;
    mastering::CorrelationMeter correlation;
    juce::AudioProcessorValueTreeState apvts;

    static constexpr auto compareId = "compare";
    static constexpr auto platformId = "platform";
    static constexpr auto previewId = "preview";

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processChunk (float* const* ch, int numCh, int n);
    void storeOrder();

    std::vector<std::unique_ptr<mastering::ModuleSlot>> slots;
    // Orden empaquetado en un único atómico (4 bits por posición): el hilo de audio
    // nunca ve un intercambio a medias.
    std::atomic<std::uint64_t> packedOrder { 0 };
    static int slotAt (std::uint64_t packed, int position) noexcept { return (int) ((packed >> (4 * position)) & 0xF); }
    static std::uint64_t withSlot (std::uint64_t packed, int position, int slot) noexcept
    {
        packed &= ~(std::uint64_t (0xF) << (4 * position));
        return packed | (std::uint64_t (slot) << (4 * position));
    }
    int firstMovable = 1, lastMovable = 5;

    // A/B a igual sonoridad: la señal original retrasada lo mismo que la cadena
    static constexpr int dryDelaySize = 1 << 17;   // latencia total máxima de la cadena (fase lineal a 192 kHz incluida)
    std::array<std::vector<float>, 2> dryDelay;
    std::array<std::vector<float>, 2> scratch;
    int dryWrite = 0;
    float compareFade = 0.0f, matchGain = 1.0f, previewGain = 1.0f;
    double fs = 48000.0;
    int maxBlock = 512, totalLatency = 0;

    std::atomic<float> *pCompare = nullptr, *pPlatform = nullptr, *pPreview = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterChainProcessor)
};
