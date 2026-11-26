// Tests de integración de Furia y captura de la UI.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s  (%s)\n", msg, #cond); } } while (0)

static void setParam (FuriaProcessor& p, const char* id, float plain)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

static void feed (FuriaProcessor& p, int blocks, float amp, double& t)
{
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        for (int n = 0; n < 512; ++n, t += 1.0 / 48000.0)
        {
            const double beat = std::fmod (t * 2.0, 1.0);
            const float env = (float) (0.35 + 0.65 * std::exp (-beat * 6.0));   // golpes rítmicos
            const float v = amp * env * (float) std::sin (2 * M_PI * 110.0 * t);
            buf.setSample (0, n, v); buf.setSample (1, n, v);
        }
        p.processBlock (buf, midi);
    }
}

static void testPowerBypassAndLatency()
{
    std::printf ("[ power apagado = bypass alineado con la latencia ]\n");
    FuriaProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    setParam (p, FuriaIDs::power, 0.0f);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    std::vector<float> in, out;
    for (int b = 0; b < 40; ++b)
    {
        for (int n = 0; n < 512; ++n) { const float v = std::sin (0.011f * (float) (b * 512 + n)) * 0.5f + 0.2f * std::sin (0.37f * (float) (b * 512 + n));
                                         buf.setSample (0, n, v); buf.setSample (1, n, v); in.push_back (v); }
        p.processBlock (buf, midi);
        for (int n = 0; n < 512; ++n) out.push_back (buf.getSample (0, n));
    }
    const int lat = p.getLatencySamples();
    float err = 0;
    for (size_t i = 4096; i < in.size(); ++i) err = std::max (err, std::fabs (out[i] - in[i - (size_t) lat]));
    std::printf ("    latencia %d muestras, error en bypass %.2e\n", lat, err);
    CHECK (err < 1e-3f, "bypass = entrada retrasada la latencia reportada");
}

static void testCompressesAndState()
{
    std::printf ("[ comprime con el input y restaura estado ]\n");
    FuriaProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    setParam (p, FuriaIDs::input, 7.0f);
    double t = 0;
    feed (p, 100, 0.5f, t);
    std::printf ("    input 7, ratio 4:1 → GR %.1f dB\n", p.getGainReductionDb());
    CHECK (p.getGainReductionDb() > 3.0f, "debe comprimir con input alto");

    setParam (p, FuriaIDs::ratio, 7.0f);
    setParam (p, FuriaIDs::dist, 2.0f);
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    FuriaProcessor q;
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK ((int) q.apvts.getRawParameterValue (FuriaIDs::ratio)->load() == 7 && (int) q.apvts.getRawParameterValue (FuriaIDs::dist)->load() == 2, "estado restaurado");
}

static int snapshot (const char* path)
{
    FuriaProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    setParam (p, FuriaIDs::input, 5.0f);
    setParam (p, FuriaIDs::attack, 4.0f);
    setParam (p, FuriaIDs::release, 3.0f);
    setParam (p, FuriaIDs::output, 4.0f);
    setParam (p, FuriaIDs::ratio, 5.0f);
    setParam (p, FuriaIDs::detHp, 1.0f);
    setParam (p, FuriaIDs::dist, 1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    double t = 0;
    for (int step = 0; step < 50; ++step)
    {
        feed (p, 3, 0.35f, t);
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
    if (argc == 3 && juce::String (argv[1]) == "--snapshot") return snapshot (argv[2]);
    testPowerBypassAndLatency();
    testCompressesAndState();
    {
        FuriaProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr, "editor válido");
    }
    std::printf ("\n%s (%d fallos)\n", failures ? "TESTS FALLIDOS" : "TODOS LOS TESTS PASAN", failures);
    return failures ? 1 : 0;
}
