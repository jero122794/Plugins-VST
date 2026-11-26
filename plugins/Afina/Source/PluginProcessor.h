#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <dspcore/Pitch.h>

#include <array>
#include <atomic>

namespace AfinaIDs
{
    inline constexpr auto key       = "key";
    inline constexpr auto scale     = "scale";
    inline constexpr auto retune    = "retune";
    inline constexpr auto amount    = "amount";
    inline constexpr auto transpose = "transpose";
    inline constexpr auto range     = "range";
    inline juce::String note (int i) { return "note" + juce::String (i); }   // notas activables (0 = C)

    inline const juce::StringArray keyNames   { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    inline const juce::StringArray scaleNames { juce::String::fromUTF8 ("Cromática"), "Mayor", "Menor natural", "Menor armónica",
                                                juce::String::fromUTF8 ("Dórica"), juce::String::fromUTF8 ("Pentatónica mayor"),
                                                juce::String::fromUTF8 ("Pentatónica menor"), "Blues" };
    inline const juce::StringArray rangeNames { "Soprano", "Alto / Tenor", "Voz grave" };
}

/**
    Afina: corrección de afinación en tiempo real por escala.
    En estéreo se corrige la suma mono y se envía a ambos canales (la voz principal es mono).
*/
class AfinaProcessor final : public juce::AudioProcessor
{
public:
    AfinaProcessor();

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

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Notas permitidas (máscara absoluta: bit 0 = C) = escala en la tonalidad ∧ notas activadas a mano. */
    std::uint16_t currentNotes() const noexcept;
    static std::uint16_t scaleMask (int index) noexcept;

    float getDetectedHz() const noexcept  { return correctors[(size_t) activeRange].getDetectedHz(); }
    float getCorrectedHz() const noexcept { return correctors[(size_t) activeRange].getCorrectedHz(); }
    float getInputLevel() const noexcept  { return inputLevel.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::array<dspcore::PitchCorrector, 3> correctors;   // uno por rango de voz: cambiar no reserva memoria
    std::atomic<int> activeRange { 1 };
    std::atomic<float> inputLevel { 0.0f };

    std::atomic<float> *pKey {}, *pScale {}, *pRetune {}, *pAmount {}, *pTranspose {}, *pRange {};
    std::array<std::atomic<float>*, 12> pNotes {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfinaProcessor)
};
