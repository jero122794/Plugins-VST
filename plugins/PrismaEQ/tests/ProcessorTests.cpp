// Tests de integración de Prisma EQ: el AudioProcessor real, como lo usaría un DAW.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <cmath>
#include <cstdio>
#include <random>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s  (%s)\n", msg, #cond); } } while (0)

static void setParam (PrismaEQProcessor& p, const juce::String& id, float plainValue)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

static void prepare (PrismaEQProcessor& p, int numCh, double fs = 48000.0, int block = 512)
{
    const auto set = numCh == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    p.setBusesLayout (layout);
    p.setRateAndBufferSizeDetails (fs, block);
    p.prepareToPlay (fs, block);
}

/** Ganancia (dB) de un seno a través del plugin, en estado estacionario. */
static float gainDb (PrismaEQProcessor& p, double f, int numCh, int block, bool& finite)
{
    const double fs = p.getSampleRate();
    juce::AudioBuffer<float> buf (numCh, block);
    juce::MidiBuffer midi;
    double inE = 0, outE = 0;
    long long t = 0;
    const int blocks = (int) (fs * 0.5 / block);
    for (int b = 0; b < blocks; ++b)
    {
        for (int n = 0; n < block; ++n, ++t)
            for (int c = 0; c < numCh; ++c)
                buf.setSample (c, n, 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * (double) t / fs));
        if (b > blocks / 2)
            for (int n = 0; n < block; ++n) inE += buf.getSample (0, n) * buf.getSample (0, n);
        p.processBlock (buf, midi);
        if (b > blocks / 2)
            for (int n = 0; n < block; ++n) { const float s = buf.getSample (0, n); finite &= std::isfinite (s); outE += s * s; }
    }
    return (float) (10.0 * std::log10 ((outE + 1e-30) / (inE + 1e-30)));
}

static void testNeutralAndBands()
{
    std::printf ("[ neutro y bandas desde parámetros, mono/estéreo, bloques irregulares ]\n");
    for (int numCh : { 1, 2 })
        for (int block : { 1, 64, 4096 })
        {
            PrismaEQProcessor p;
            prepare (p, numCh);
            bool finite = true;
            CHECK (std::fabs (gainDb (p, 1000, numCh, block, finite)) < 0.01f, "sin bandas: 0 dB");
            setParam (p, PrismaIDs::band (2, "on"), 1.0f);
            setParam (p, PrismaIDs::band (2, "type"), 0.0f);
            setParam (p, PrismaIDs::band (2, "freq"), 1000.0f);
            setParam (p, PrismaIDs::band (2, "gain"), 6.0f);
            setParam (p, PrismaIDs::band (2, "q"), 1.0f);
            const float g = gainDb (p, 1000, numCh, block, finite);
            CHECK (std::fabs (g - 6.0f) < 0.1f, "bell +6 dB aplicado");
            CHECK (finite, "salida finita");
        }
}

static void testHqLatency()
{
    std::printf ("[ HQ en caliente ]\n");
    PrismaEQProcessor p;
    prepare (p, 2);
    bool finite = true;
    for (int on : { 0, 1, 0, 1 })
    {
        setParam (p, PrismaIDs::hq, (float) on);
        gainDb (p, 1000, 2, 256, finite);
        std::printf ("    HQ=%d -> latencia %d, frecuencia interna %.0f Hz\n", on, p.getLatencySamples(), p.getEqSampleRate());
        CHECK (std::abs (p.getEqSampleRate() - (on ? 96000.0 : 48000.0)) < 1.0, "frecuencia interna del EQ");
    }
    CHECK (finite, "salida finita al conmutar HQ");
}

static void testStateRoundTrip()
{
    std::printf ("[ guardar / restaurar estado ]\n");
    PrismaEQProcessor a, b;
    setParam (a, PrismaIDs::band (4, "on"), 1.0f);
    setParam (a, PrismaIDs::band (4, "freq"), 3456.0f);
    setParam (a, PrismaIDs::band (4, "dynRange"), -7.5f);
    setParam (a, PrismaIDs::band (4, "placement"), 2.0f);
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    const auto pa = a.readBand (4), pb = b.readBand (4);
    CHECK (pb.enabled && std::fabs (pa.freq - pb.freq) < 0.5f && std::fabs (pa.dynRangeDb - pb.dynRangeDb) < 0.01f
           && pb.placement == dspcore::Placement::Side, "estado restaurado");
}

static int snapshot (const char* path)
{
    PrismaEQProcessor p;
    prepare (p, 2);
    struct B { int i, type; float f, g, q; float dyn; };
    for (auto b : { B { 0, 3, 40.0f, 0.0f, 0.707f, 0.0f }, B { 1, 1, 120.0f, 3.0f, 0.707f, 0.0f },
                    B { 3, 0, 450.0f, -4.5f, 1.4f, 0.0f }, B { 5, 0, 3200.0f, 0.0f, 2.0f, -8.0f }, B { 6, 2, 9000.0f, 2.5f, 0.707f, 0.0f } })
    {
        setParam (p, PrismaIDs::band (b.i, "on"), 1.0f);
        setParam (p, PrismaIDs::band (b.i, "type"), (float) b.type);
        setParam (p, PrismaIDs::band (b.i, "freq"), b.f);
        setParam (p, PrismaIDs::band (b.i, "gain"), b.g);
        setParam (p, PrismaIDs::band (b.i, "q"), b.q);
        setParam (p, PrismaIDs::band (b.i, "dynRange"), b.dyn);
        setParam (p, PrismaIDs::band (b.i, "threshold"), -40.0f);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    if (auto* e = dynamic_cast<PrismaEQEditor*> (ed.get())) e->selectBand (5);

    // Ruido rosado (aprox. Voss-McCartney) para alimentar el analizador
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    float rows[8] = {};
    long long counter = 0;
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int frame = 0; frame < 30; ++frame)
    {
        for (int b = 0; b < 4; ++b)
        {
            for (int n = 0; n < 512; ++n, ++counter)
            {
                float sum = 0;
                for (int k = 0; k < 8; ++k)
                {
                    if ((counter & ((1 << k) - 1)) == 0) rows[k] = u (rng);
                    sum += rows[k];
                }
                buf.setSample (0, n, 0.05f * sum);
                buf.setSample (1, n, 0.05f * sum);
            }
            p.processBlock (buf, midi);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (35);
    }

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    out.deleteFile();
    juce::FileOutputStream os (out);
    return os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os) ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc == 3 && juce::String (argv[1]) == "--snapshot")
        return snapshot (argv[2]);

    testNeutralAndBands();
    testHqLatency();
    testStateRoundTrip();
    {
        std::printf ("[ el editor se crea y se destruye ]\n");
        PrismaEQProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr && ed->getWidth() > 0, "editor válido");
    }
    std::printf ("\n%s (%d fallos)\n", failures ? "TESTS FALLIDOS" : "TODOS LOS TESTS PASAN", failures);
    return failures ? 1 : 0;
}
