// Tests de integración de MasterChain: el AudioProcessor real, como lo usa un DAW.
#include "../Source/ChainProcessor.h"
#include "../Source/ChainEditor.h"

#include <cmath>
#include <cstdio>
#include <random>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s  (%s)\n", msg, #cond); } } while (0)

static void setParam (MasterChainProcessor& p, const juce::String& id, float plain)
{
    auto* param = p.apvts.getParameter (id);
    if (param == nullptr) { std::printf ("  parámetro inexistente: %s\n", id.toRawUTF8()); ++failures; return; }
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

static void prepare (MasterChainProcessor& p, double fs = 48000.0, int block = 512)
{
    p.setRateAndBufferSizeDetails (fs, block);
    p.prepareToPlay (fs, block);
}

/** Programa sintético exigente (ruido rosado + bombo + agudos), estéreo. */
static void program (std::vector<float>& l, std::vector<float>& r, int n, float gain, unsigned seed = 9)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    l.resize ((size_t) n); r.resize ((size_t) n);
    float b0 = 0, b1 = 0, b2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const float w = u (rng);
        b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
        const float pink = 0.25f * (b0 + b1 + b2 + w * 0.1848f);
        const int k = i % 24000;
        const float kick = k < 2400 ? 1.5f * std::sin (6.2831853f * 55.0f * (float) k / 48000.0f) * std::exp (-(float) k / 3000.0f) : 0.0f;
        l[(size_t) i] = gain * (pink + kick + 0.2f * std::sin (6.2831853f * 9000.0f * (float) i / 48000.0f));
        r[(size_t) i] = gain * (0.8f * pink + kick - 0.1f * w);
    }
}

static void run (MasterChainProcessor& p, std::vector<float>& l, std::vector<float>& r, int block = 512)
{
    juce::MidiBuffer midi;
    for (size_t off = 0; off < l.size(); off += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, l.size() - off);
        float* ch[] = { l.data() + off, r.data() + off };
        juce::AudioBuffer<float> buf (ch, 2, n);
        p.processBlock (buf, midi);
    }
}

static void setAllModules (MasterChainProcessor& p, bool on)
{
    for (int s = 0; s < p.getNumSlots(); ++s)
        setParam (p, p.getSlot (s).moduleInfo.prefix + "on", on ? 1.0f : 0.0f);
}

// ---------------------------------------------------------------------------

static void testLatencyCompensationAllBypassed()
{
    std::printf ("[ todo en bypass = entrada retrasada exactamente la latencia reportada ]\n");
    MasterChainProcessor p;
    prepare (p);
    setAllModules (p, false);
    setParam (p, "eq_phase", 1.0f);        // EQ en fase lineal: ~8700 muestras de latencia
    setParam (p, "lim_lookahead", 3.0f);
    std::vector<float> l, r;
    program (l, r, 48000, 0.3f);
    run (p, l, r);                          // calentamiento: la latencia se estabiliza
    std::vector<float> l2, r2, ref;
    program (l2, r2, 48000, 0.3f, 77);
    ref = l2;
    run (p, l2, r2);
    const int lat = p.getLatencySamples();
    float err = 0;
    for (size_t i = (size_t) lat + 2000; i < l2.size(); ++i) err = std::max (err, std::fabs (l2[i] - ref[i - (size_t) lat]));
    std::printf ("    latencia reportada %d muestras, error máximo %.2e\n", lat, err);
    CHECK (lat > 0, "la cadena debe reportar latencia (limitador + EQ HQ + sobremuestreo)");
    CHECK (err < 1e-6f, "la salida en bypass debe ser la entrada retrasada exactamente la latencia");
}

static void testCeilingWithFullChain()
{
    std::printf ("[ cadena completa con material muy fuerte: techo true-peak ]\n");
    MasterChainProcessor p;
    prepare (p);
    setParam (p, "comp_on", 1.0f);
    setParam (p, "sat_on", 1.0f);
    setParam (p, "img_on", 1.0f);
    setParam (p, "clip_on", 1.0f);
    setParam (p, "img_width", 140.0f);
    setParam (p, "sat_drive", 9.0f);
    setParam (p, "clip_drive", 4.0f);
    setParam (p, "lim_gain", 6.0f);
    setParam (p, "lim_ceiling", -1.0f);
    std::vector<float> l, r;
    program (l, r, 48000 * 8, 1.5f);
    run (p, l, r);
    dspcore::LoudnessMeter check;
    check.prepare (48000.0, 2);
    const float* ch[] = { l.data() + 48000, r.data() + 48000 };
    check.process (ch, (int) l.size() - 48000);
    std::printf ("    true peak de salida %.3f dBTP, integrado %.1f LUFS, finito: %s\n", check.getTruePeakDb(), check.getIntegrated(),
                 std::all_of (l.begin(), l.end(), [] (float v) { return std::isfinite (v); }) ? "sí" : "NO");
    CHECK (check.getTruePeakDb() <= -0.9, "el techo de -1 dBTP debe respetarse con toda la cadena");
    CHECK (std::isfinite (check.getIntegrated()), "salida válida");
}

static void testReorderAndState()
{
    std::printf ("[ reordenar, posiciones fijas y guardar/restaurar ]\n");
    MasterChainProcessor a;
    const int eqSlot = a.slotAt (1);
    a.moveModule (1, +1);                                 // EQ baja una posición
    CHECK (a.slotAt (2) == eqSlot, "el EQ pasa a la posición 2");
    const int limPos = a.getNumSlots() - 2, limSlot = a.slotAt (limPos);
    a.moveModule (limPos, -1);                            // el limitador no se puede mover
    CHECK (a.slotAt (limPos) == limSlot, "el limitador es fijo");
    a.moveModule (1, -1);                                 // no se puede pasar por delante de la entrada
    CHECK (a.slotAt (0) == 0, "la entrada es fija");
    setParam (a, "lim_ceiling", -0.5f);

    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    MasterChainProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = true;
    for (int pos = 0; pos < a.getNumSlots(); ++pos) same &= a.slotAt (pos) == b.slotAt (pos);
    CHECK (same, "el orden se restaura");
    CHECK (std::fabs (b.apvts.getRawParameterValue ("lim_ceiling")->load() + 0.5f) < 1e-3f, "los parámetros se restauran");
}

static void testCompareIsLoudnessMatched()
{
    std::printf ("[ A/B igualado en sonoridad ]\n");
    MasterChainProcessor p;
    prepare (p);
    setParam (p, "lim_gain", 9.0f);   // el procesado queda bastante más fuerte que el original
    std::vector<float> l, r;
    program (l, r, 48000 * 6, 0.15f);
    run (p, l, r);
    const double processed = p.outputMeter.getShortTerm(), original = p.inputMeter.getShortTerm();
    setParam (p, MasterChainProcessor::compareId, 1.0f);
    std::vector<float> l2, r2;
    program (l2, r2, 48000 * 4, 0.15f, 9);
    run (p, l2, r2);
    dspcore::LoudnessMeter heard;
    heard.prepare (48000.0, 2);
    const float* ch[] = { l2.data() + 48000, r2.data() + 48000 };
    heard.process (ch, (int) l2.size() - 48000);
    std::printf ("    original %.1f, procesado %.1f, lo que se oye en A/B %.1f LUFS\n", original, processed, heard.getShortTerm());
    CHECK (processed - original > 5.0, "el limitador sube la sonoridad");
    CHECK (std::fabs (heard.getShortTerm() - processed) < 1.0, "en A/B el original suena a la misma sonoridad que el procesado");
}

static void testBypassTogglesAreClean()
{
    std::printf ("[ activar/desactivar módulos durante la reproducción ]\n");
    MasterChainProcessor p;
    prepare (p);
    std::vector<float> l, r;
    program (l, r, 48000 * 3, 0.2f);
    juce::MidiBuffer midi;
    bool finite = true;
    int toggles = 0;
    for (size_t off = 0; off < l.size(); off += 512)
    {
        if ((off / 512) % 20 == 0)
        {
            const int s = (int) ((off / 512 / 20) % (size_t) p.getNumSlots());
            const auto id = p.getSlot (s).moduleInfo.prefix + "on";
            setParam (p, id, p.apvts.getRawParameterValue (id)->load() > 0.5f ? 0.0f : 1.0f);
            ++toggles;
        }
        float* ch[] = { l.data() + off, r.data() + off };
        juce::AudioBuffer<float> buf (ch, 2, (int) std::min<size_t> (512, l.size() - off));
        p.processBlock (buf, midi);
        for (int i = 0; i < buf.getNumSamples(); ++i) finite &= std::isfinite (ch[0][i]) && std::isfinite (ch[1][i]);
    }
    std::printf ("    %d cambios de bypass, salida finita: %s\n", toggles, finite ? "sí" : "NO");
    CHECK (finite, "sin NaN al conmutar");
}

static int snapshot (const char* path, const char* slotId)
{
    MasterChainProcessor p;
    prepare (p);
    setParam (p, "comp_on", 1.0f);
    setParam (p, "lim_gain", 6.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    if (auto* e = dynamic_cast<MasterChainEditor*> (ed.get()))
        for (int s = 0; s < p.getNumSlots(); ++s)
            if (p.getSlot (s).moduleInfo.id == slotId) e->selectSlot (s);

    std::vector<float> l, r;
    for (int sec = 0; sec < 20; ++sec)
    {
        program (l, r, 48000, 0.25f + 0.1f * std::sin ((float) sec * 0.7f), (unsigned) sec);
        run (p, l, r);
        if (sec % 2 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    out.deleteFile();
    juce::FileOutputStream os (out);
    return os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os) ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc == 4 && juce::String (argv[1]) == "--snapshot")
        return snapshot (argv[2], argv[3]);

    testLatencyCompensationAllBypassed();
    testCeilingWithFullChain();
    testReorderAndState();
    testCompareIsLoudnessMatched();
    testBypassTogglesAreClean();
    {
        std::printf ("[ el editor se crea con cada módulo seleccionado ]\n");
        MasterChainProcessor p;
        prepare (p);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* e = dynamic_cast<MasterChainEditor*> (ed.get());
        for (int s = 0; s < p.getNumSlots(); ++s) e->selectSlot (s);
        CHECK (ed->getWidth() > 0, "editor válido");
    }
    std::printf ("\n%s (%d fallos)\n", failures ? "TESTS FALLIDOS" : "TODOS LOS TESTS PASAN", failures);
    return failures ? 1 : 0;
}
