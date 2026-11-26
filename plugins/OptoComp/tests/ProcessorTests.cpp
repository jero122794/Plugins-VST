// Tests de integración: instancian el AudioProcessor real (como lo haría un DAW)
// y comprueban lo que suele romperse en un plugin: tamaños de bloque, mono/estéreo,
// cambios de oversampling en caliente, latencia, NaN y guardado de estado.
#include "../Source/PluginProcessor.h"

#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s  (%s)\n", msg, #cond); } } while (0)

static void setParam (OptoCompProcessor& p, const char* id, float plainValue)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

/** Procesa `seconds` de un seno con bloques de tamaño `block` y devuelve el pico de salida. */
static float render (OptoCompProcessor& p, int numCh, int block, double seconds, float amp, bool& finite)
{
    juce::AudioBuffer<float> buf (numCh, block);
    juce::MidiBuffer midi;
    const double fs = p.getSampleRate();
    float peak = 0.0f;
    long long t = 0;
    for (int b = 0; b < (int) (seconds * fs / block); ++b)
    {
        for (int n = 0; n < block; ++n, ++t)
            for (int c = 0; c < numCh; ++c)
                buf.setSample (c, n, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) t / fs));
        p.processBlock (buf, midi);
        for (int c = 0; c < numCh; ++c)
            for (int n = 0; n < block; ++n)
            {
                const float s = buf.getSample (c, n);
                finite &= std::isfinite (s);
                peak = std::fmax (peak, std::fabs (s));
            }
    }
    return peak;
}

static void testLayoutsAndBlockSizes()
{
    std::printf ("[ layouts y tamaños de bloque ]\n");
    for (int numCh : { 1, 2 })
        for (int block : { 1, 32, 512, 4096 })   // 4096 > tamaño anunciado: debe trocearse
        {
            OptoCompProcessor p;
            const auto set = numCh == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (set);
            layout.outputBuses.add (set);
            CHECK (p.setBusesLayout (layout), "layout soportado");
            p.setRateAndBufferSizeDetails (48000.0, 512);
            p.prepareToPlay (48000.0, 512);
            setParam (p, ParamID::peakReduction, 80.0f);
            bool finite = true;
            const float peak = render (p, numCh, block, 1.0, 0.8f, finite);
            CHECK (finite, "salida finita");
            CHECK (peak > 0.01f && peak < 2.0f, "nivel de salida razonable");
        }
}

static void testOversamplingSwitchAndLatency()
{
    std::printf ("[ oversampling en caliente y latencia ]\n");
    OptoCompProcessor p;
    p.setRateAndBufferSizeDetails (44100.0, 256);
    p.prepareToPlay (44100.0, 256);
    for (int os : { 0, 1, 2, 1, 0 })
    {
        setParam (p, ParamID::oversampling, (float) os);
        bool finite = true;
        render (p, 2, 256, 0.2, 0.5f, finite);
        std::printf ("    %dx -> latencia %d muestras\n", 1 << os, p.getLatencySamples());
        CHECK (finite, "salida finita al cambiar oversampling");
        if (os == 0) CHECK (p.getLatencySamples() == 0, "1x sin latencia");
        else         CHECK (p.getLatencySamples() >= 0 && p.getLatencySamples() < 64, "latencia acotada");
    }
}

static void testTransparentWhenNeutral()
{
    std::printf ("[ neutro = transparente ]\n");
    OptoCompProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    setParam (p, ParamID::peakReduction, 0.0f);
    setParam (p, ParamID::color, 0.0f);
    setParam (p, ParamID::oversampling, 0.0f);
    setParam (p, ParamID::gain, 0.0f);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    float maxErr = 0.0f;
    for (int b = 0; b < 20; ++b)
    {
        juce::AudioBuffer<float> ref (2, 512);
        for (int n = 0; n < 512; ++n)
            for (int c = 0; c < 2; ++c)
            {
                const float v = 0.5f * std::sin (0.05f * (float) (b * 512 + n) + (float) c);
                buf.setSample (c, n, v);
                ref.setSample (c, n, v);
            }
        p.processBlock (buf, midi);
        if (b > 2)
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < 512; ++n)
                    maxErr = std::fmax (maxErr, std::fabs (buf.getSample (c, n) - ref.getSample (c, n)));
    }
    std::printf ("    error máximo %.2e\n", maxErr);
    CHECK (maxErr < 1.0e-5f, "con PR 0, color 0, 1x y gain 0 dB debe ser transparente");
}

static void testStateRoundTrip()
{
    std::printf ("[ guardar / restaurar estado ]\n");
    OptoCompProcessor a, b;
    setParam (a, ParamID::model, 1.0f);
    setParam (a, ParamID::peakReduction, 63.0f);
    setParam (a, ParamID::mode, 1.0f);
    setParam (a, ParamID::color, 12.0f);
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    for (auto* id : { ParamID::model, ParamID::peakReduction, ParamID::mode, ParamID::color })
        CHECK (std::fabs (a.apvts.getRawParameterValue (id)->load() - b.apvts.getRawParameterValue (id)->load()) < 1e-4f, id);
}

static void testEditorCreates()
{
    std::printf ("[ el editor se crea y se destruye ]\n");
    OptoCompProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    CHECK (ed != nullptr && ed->getWidth() > 0, "editor válido");
}

/** `--snapshot ruta.png [modelo]`: procesa audio con la UI abierta y guarda una captura. */
static int snapshot (const char* path, int model)
{
    OptoCompProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    setParam (p, ParamID::model, (float) model);
    setParam (p, ParamID::peakReduction, 62.0f);
    setParam (p, ParamID::gain, 4.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    long long t = 0;
    for (int step = 0; step < 60; ++step)
    {
        for (int b = 0; b < 9; ++b)
        {
            for (int n = 0; n < 512; ++n, ++t)
            {
                const double sec = (double) t / 48000.0;
                const float env = 0.35f + 0.25f * (float) std::sin (sec * 3.0);
                const float v = env * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * sec);
                buf.setSample (0, n, v);
                buf.setSample (1, n, v);
            }
            p.processBlock (buf, midi);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    }
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    out.deleteFile();
    juce::FileOutputStream os (out);
    return os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os) ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 3 && juce::String (argv[1]) == "--snapshot")
        return snapshot (argv[2], argc >= 4 ? juce::String (argv[3]).getIntValue() : 0);
    testLayoutsAndBlockSizes();
    testOversamplingSwitchAndLatency();
    testTransparentWhenNeutral();
    testStateRoundTrip();
    testEditorCreates();
    std::printf ("\n%s (%d fallos)\n", failures ? "TESTS FALLIDOS" : "TODOS LOS TESTS PASAN", failures);
    return failures ? 1 : 0;
}
