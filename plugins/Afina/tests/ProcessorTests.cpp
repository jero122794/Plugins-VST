// Tests de integración de Afina: el AudioProcessor real, como lo usaría un DAW.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s  (%s)\n", msg, #cond); } } while (0)

static void setParam (AfinaProcessor& p, const juce::String& id, float plain)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

/** "Cantante" sintético: pulsos glotales con formantes, melodía desafinada y vibrato. */
struct Singer
{
    double fs = 48000.0, ph = 0.0, t = 0.0;
    double a1 = 0, a2 = 0, b1 = 0, b2 = 0, y1 = 0, y2 = 0, z1 = 0, z2 = 0;
    Singer()
    {
        auto coef = [this] (double f, double bw, double& c1, double& c2) { const double r = std::exp (-M_PI * bw / fs); c1 = -2 * r * std::cos (2 * M_PI * f / fs); c2 = r * r; };
        coef (650.0, 90.0, a1, a2);
        coef (1100.0, 110.0, b1, b2);
    }
    float next (double f0)
    {
        t += 1.0 / fs;
        const double f = f0 * (1.0 + 0.006 * std::sin (2 * M_PI * 5.5 * t));   // vibrato ±10 cents
        ph += f / fs;
        double pulse = 0.0;
        if (ph >= 1.0) { ph -= 1.0; pulse = 1.0; }
        const double v1 = pulse - a1 * y1 - a2 * y2; y2 = y1; y1 = v1;
        const double v2 = pulse - b1 * z1 - b2 * z2; z2 = z1; z1 = v2;
        return (float) (0.02 * (v1 + 0.6 * v2));
    }
};

/** Melodía en Do mayor cantada con desafinaciones de 20–40 cents. */
static double melody (double sec)
{
    static const double notes[] = { 60, 62, 64, 65, 67, 65, 64, 62 };
    static const double detune[] = { +0.32, -0.25, +0.38, -0.30, +0.22, +0.35, -0.28, +0.30 };
    const int i = (int) std::fmod (sec / 0.75, 8.0);
    return 440.0 * std::pow (2.0, (notes[i] + detune[i] - 69.0) / 12.0);
}

static void testCorrectionThroughParameters()
{
    std::printf ("[ corrección a través de los parámetros (Do mayor, rango Alto/Tenor) ]\n");
    AfinaProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    setParam (p, AfinaIDs::scale, 1.0f);    // mayor
    setParam (p, AfinaIDs::key, 0.0f);      // C
    setParam (p, AfinaIDs::retune, 0.0f);
    Singer s;
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    // E4 cantado +35 cents durante 1.5 s
    const double f = 329.63 * std::pow (2.0, 0.35 / 12.0);
    for (int b = 0; b < 140; ++b)
    {
        for (int n = 0; n < 512; ++n) { const float v = s.next (f); buf.setSample (0, n, v); buf.setSample (1, n, v); }
        p.processBlock (buf, midi);
    }
    const float out = p.getCorrectedHz();
    std::printf ("    E4 +35 cents → objetivo %.2f Hz (E4 = 329.63)\n", out);
    CHECK (std::fabs (1200.0f * std::log2 (out / 329.63f)) < 15.0f, "la corrección apunta a E4 (vibrato incluido)");
}

static void testLatencyPerRange()
{
    std::printf ("[ latencia por rango de voz ]\n");
    AfinaProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    int prev = 1 << 30;
    for (int r = 0; r < 3; ++r)
    {
        setParam (p, AfinaIDs::range, (float) r);
        buf.clear();
        p.processBlock (buf, midi);
        std::printf ("    %-14s %5d muestras (%.1f ms)\n", AfinaIDs::rangeNames[r].toRawUTF8(), p.getLatencySamples(), p.getLatencySamples() / 48.0);
        if (r > 0) CHECK (p.getLatencySamples() >= prev, "más grave = más latencia");
        prev = p.getLatencySamples();
    }
}

static void testStateAndNotes()
{
    std::printf ("[ estado y notas excluidas ]\n");
    AfinaProcessor a, b;
    setParam (a, AfinaIDs::scale, 2.0f);
    setParam (a, AfinaIDs::key, 9.0f);       // La menor
    setParam (a, AfinaIDs::note (7), 0.0f);  // excluir G
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    const auto expected = dspcore::Scale::absolute (dspcore::Scale::minor, 9) & ~(1u << 7);
    CHECK (b.currentNotes() == expected, "La menor sin G restaurado");
}

static int snapshot (const char* path)
{
    AfinaProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    setParam (p, AfinaIDs::scale, 1.0f);
    setParam (p, AfinaIDs::retune, 30.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    Singer s;
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    double t = 0.0;
    for (int step = 0; step < 230; ++step)
    {
        for (int b = 0; b < 3; ++b)
        {
            for (int n = 0; n < 512; ++n, t += 1.0 / 48000.0)
            {
                const double gap = std::fmod (t, 0.75);
                const float v = gap > 0.68 ? 0.0f : s.next (melody (t));   // pequeños silencios entre notas
                buf.setSample (0, n, v); buf.setSample (1, n, v);
            }
            p.processBlock (buf, midi);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (33);
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
    if (argc == 3 && juce::String (argv[1]) == "--snapshot")
        return snapshot (argv[2]);
    testCorrectionThroughParameters();
    testLatencyPerRange();
    testStateAndNotes();
    {
        std::printf ("[ el editor se crea y se destruye ]\n");
        AfinaProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr && ed->getWidth() > 0, "editor válido");
    }
    std::printf ("\n%s (%d fallos)\n", failures ? "TESTS FALLIDOS" : "TODOS LOS TESTS PASAN", failures);
    return failures ? 1 : 0;
}
