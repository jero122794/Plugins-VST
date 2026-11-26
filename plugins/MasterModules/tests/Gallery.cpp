// Galería de los plugins sueltos. Sin argumentos: crea y destruye cada editor (smoke test).
// Con "--snapshot <carpeta>": procesa música sintética y guarda una captura de cada plugin.
#include "../Source/ModulePlugin.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace
{
    /** Programa sintético con aspecto de música: bombo, caja, bajo, hi-hats abiertos en estéreo y dinámica por compases. */
    struct Program
    {
        std::mt19937 rng { 5 };
        std::uniform_real_distribution<float> u { -1.0f, 1.0f };
        double fs = 48000.0;
        long long t = 0;
        float lpL = 0, lpR = 0;

        void render (float* l, float* r, int n)
        {
            for (int i = 0; i < n; ++i, ++t)
            {
                const double sec = (double) t / fs;
                const double beat = std::fmod (sec * 2.0, 1.0);             // 120 BPM
                const int bar = (int) (sec / 2.0);
                const float section = (bar % 4 == 3) ? 1.0f : 0.55f;        // un compás de cada cuatro, más fuerte
                const float kick = (float) (std::exp (-beat * 18.0) * std::sin (2 * M_PI * (50.0 + 90.0 * std::exp (-beat * 30.0)) * beat * 0.5));
                const double half = std::fmod (sec * 2.0 + 0.5, 1.0);
                const float snare = (std::fmod (sec, 1.0) > 0.5 ? 1.0f : 0.0f) * (float) std::exp (-half * 14.0) * u (rng);
                const float bass = 0.35f * (float) std::sin (2 * M_PI * (bar % 2 ? 55.0 : 41.2) * sec);
                const double eighth = std::fmod (sec * 4.0, 1.0);
                const float hatEnv = (float) std::exp (-eighth * 25.0);
                lpL += 0.6f * (u (rng) - lpL);
                lpR += 0.6f * (u (rng) - lpR);
                const float hatL = hatEnv * (u (rng) - lpL), hatR = hatEnv * (u (rng) - lpR);   // descorrelados: ancho estéreo
                const float mono = section * (0.8f * kick + 0.45f * snare + bass);
                l[i] = 0.5f * (mono + 0.25f * section * hatL);
                r[i] = 0.5f * (mono + 0.25f * section * hatR);
            }
        }
    };

    void set (ModulePluginProcessor& p, const char* id, float v)
    {
        if (auto* param = p.apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (v));
    }

    void configure (ModulePluginProcessor& p)
    {
        const auto id = p.info.id;
        if (id == "limiter") { set (p, "gain", 7.0f); set (p, "release", 60.0f); }
        if (id == "comp")    { set (p, "threshold", -22.0f); set (p, "ratio", 3.0f); set (p, "makeup", 4.0f); }
        if (id == "sat")     { set (p, "mode", 1.0f); set (p, "drive", 10.0f); }
        if (id == "imager")  { set (p, "width", 130.0f); set (p, "bassMono", 120.0f); }
        if (id == "clipper") { set (p, "drive", 5.0f); set (p, "ceiling", -1.0f); }
        if (id == "meter")   { set (p, "platform", 1.0f); }
    }

    int snapshotAll (const juce::File& dir)
    {
        dir.createDirectory();
        int failures = 0;
        for (auto id : { "limiter", "comp", "sat", "imager", "clipper", "meter" })
        {
            ModulePluginProcessor p (id);
            configure (p);
            p.setRateAndBufferSizeDetails (48000.0, 512);
            p.prepareToPlay (48000.0, 512);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());

            Program prog;
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            for (int step = 0; step < 260; ++step)        // ~25 s de audio, con la UI refrescándose entre medias
            {
                for (int b = 0; b < 9; ++b)
                {
                    prog.render (buf.getWritePointer (0), buf.getWritePointer (1), 512);
                    p.processBlock (buf, midi);
                }
                juce::MessageManager::getInstance()->runDispatchLoopUntil (34);
            }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
            auto file = dir.getChildFile (juce::String ("module_") + id + ".png");
            file.deleteFile();
            juce::FileOutputStream os (file);
            if (! (os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os))) ++failures;
            std::printf ("  %s -> %s\n", id, file.getFullPathName().toRawUTF8());
        }
        return failures;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc == 3 && juce::String (argv[1]) == "--snapshot")
        return snapshotAll (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    for (auto id : { "limiter", "comp", "sat", "imager", "clipper", "meter" })
    {
        ModulePluginProcessor p (id);
        p.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        if (ed == nullptr || ed->getWidth() == 0) { std::printf ("editor de %s inválido\n", id); return 1; }
    }
    std::printf ("Los 6 editores se crean correctamente\n");
    return 0;
}
