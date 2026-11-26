#include <mastering/Module.h>
#include <mastering/Widgets.h>
#include <mastering/MeterPanel.h>

#include <dspcore/Limiter.h>
#include <dspcore/MasterDsp.h>
#include <studioui/EqCore.h>
#include <studioui/EqView.h>

#include <juce_dsp/juce_dsp.h>

namespace mastering
{

using namespace juce;

// ============================================================================
// Ayudas para declarar parámetros

namespace
{
    NormalisableRange<float> range (float lo, float hi, float centre = -1.0f)
    {
        NormalisableRange<float> r (lo, hi);
        if (centre > lo && centre < hi) r.setSkewForCentre (centre);
        return r;
    }

    AudioParameterFloatAttributes unit (const String& suffix, int decimals)
    {
        return AudioParameterFloatAttributes().withLabel (suffix.trim())
            .withStringFromValueFunction ([suffix, decimals] (float v, int) { return String (v, decimals) + suffix; })
            .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); });
    }

    AudioParameterFloatAttributes hzOrOff()
    {
        return AudioParameterFloatAttributes().withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return v < 20.0f ? String ("Off") : String (roundToInt (v)) + " Hz"; })
            .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); });
    }

    void addFloat (Layout& l, const String& prefix, const char* id, const String& name,
                   NormalisableRange<float> r, float def, AudioParameterFloatAttributes attr)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { prefix + id, 1 }, name, r, def, attr));
    }
    void addChoice (Layout& l, const String& prefix, const char* id, const String& name, const StringArray& c, int def)
    {
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { prefix + id, 1 }, name, c, def));
    }
    void addBool (Layout& l, const String& prefix, const char* id, const String& name, bool def)
    {
        l.add (std::make_unique<AudioParameterBool> (ParameterID { prefix + id, 1 }, name, def));
    }

    float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }
    bool changed (float a, float b) noexcept { return std::fabs (a - b) > 1.0e-6f; }

    /** Sobremuestreo 4x reutilizable para los módulos no lineales. */
    struct Oversampler4x
    {
        void prepare (int numCh, int maxBlock)
        {
            os = std::make_unique<dsp::Oversampling<float>> ((size_t) std::max (1, numCh), 2,
                                                             dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            os->initProcessing ((size_t) maxBlock);
        }
        template <typename Fn>
        void process (float* const* ch, int numCh, int n, Fn&& fn)
        {
            dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
            auto up = os->processSamplesUp (block);
            fn (up);
            os->processSamplesDown (block);
        }
        int latency() const noexcept { return os ? roundToInt (os->getLatencyInSamples()) : 0; }
        std::unique_ptr<dsp::Oversampling<float>> os;
    };
}

// ============================================================================
// 1. Entrada

class InputModule final : public Module
{
public:
    static void addParameters (Layout& l, const String& p)
    {
        addFloat (l, p, "gain", "Input Gain", range (-24.0f, 24.0f), 0.0f, unit (" dB", 1));
        addChoice (l, p, "subsonic", "Input Subsonic", { "Off", "DC (5 Hz)", "20 Hz", "30 Hz" }, 1);
        addBool (l, p, "invL", "Input Invert L", false);
        addBool (l, p, "invR", "Input Invert R", false);
        addBool (l, p, "mono", "Input Mono Check", false);
    }

    void bind (APVTS& s, const String& p) override
    {
        Module::bind (s, p);
        gain = param ("gain"); subsonic = param ("subsonic"); invL = param ("invL"); invR = param ("invR"); mono = param ("mono");
    }
    void prepare (double fs, int, int) override { dsp.prepare (fs); }
    void process (float* const* ch, int numCh, int n) noexcept override
    {
        dsp.setGainDb (gain->load());
        dsp.setSubsonic ((dspcore::InputStage::Subsonic) (int) subsonic->load());
        dsp.setInvert (invL->load() > 0.5f, invR->load() > 0.5f);
        dsp.setMono (mono->load() > 0.5f);
        dsp.process (ch, numCh, n);
    }
    std::unique_ptr<Component> createPanel() override
    {
        return std::make_unique<ParamPanel> (*apvts, std::vector<ParamPanel::Spec> {
            { pid ("subsonic"), studioui::utf8 ("SUBSÓNICO") }, { pid ("invL"), "Invertir L" }, { pid ("invR"), "Invertir R" },
            { pid ("mono"), "Escuchar en mono" }, { pid ("gain"), "GANANCIA" } });
    }

private:
    dspcore::InputStage dsp;
    std::atomic<float> *gain {}, *subsonic {}, *invL {}, *invR {}, *mono {};
};

// ============================================================================
// 2. EQ (el mismo núcleo y la misma UI que PrismaEQ)

class EqModule final : public Module
{
public:
    static void addParameters (Layout& l, const String& p) { studioui::EqCore::addParameters (l, p); }

    void bind (APVTS& s, const String& p) override { Module::bind (s, p); core.bind (s, p); }
    void prepare (double fs, int maxBlock, int numCh) override { core.prepare (fs, maxBlock, numCh); }
    void process (float* const* ch, int numCh, int n) noexcept override { core.process (ch, numCh, n); }
    int getLatency() const noexcept override { return core.getLatency(); }
    std::unique_ptr<Component> createPanel() override { return std::make_unique<studioui::EqView> (core); }

private:
    studioui::EqCore core;
};

// ============================================================================
// 3. Compresor de bus

class CompModule final : public Module
{
public:
    static void addParameters (Layout& l, const String& p)
    {
        addFloat (l, p, "threshold", "Comp Threshold", range (-40.0f, 0.0f), -18.0f, unit (" dB", 1));
        addFloat (l, p, "ratio", "Comp Ratio", range (1.0f, 10.0f, 2.5f), 2.0f, unit (":1", 1));
        addFloat (l, p, "attack", "Comp Attack", range (0.1f, 100.0f, 10.0f), 10.0f, unit (" ms", 1));
        addFloat (l, p, "release", "Comp Release", range (20.0f, 1500.0f, 200.0f), 200.0f, unit (" ms", 0));
        addBool (l, p, "auto", "Comp Auto Release", true);
        addFloat (l, p, "knee", "Comp Knee", range (0.0f, 12.0f), 6.0f, unit (" dB", 1));
        addFloat (l, p, "makeup", "Comp Makeup", range (-6.0f, 18.0f), 0.0f, unit (" dB", 1));
        addFloat (l, p, "mix", "Comp Mix", range (0.0f, 100.0f), 100.0f, unit (" %", 0));
        addFloat (l, p, "scHpf", "Comp SC High-Pass", range (0.0f, 300.0f, 90.0f), 90.0f, hzOrOff());
    }

    void bind (APVTS& s, const String& p) override
    {
        Module::bind (s, p);
        thr = param ("threshold"); ratio = param ("ratio"); atk = param ("attack"); rel = param ("release");
        autoRel = param ("auto"); knee = param ("knee"); makeup = param ("makeup"); mix = param ("mix"); sc = param ("scHpf");
    }
    void prepare (double fs, int, int) override { dsp.prepare (fs); }
    void process (float* const* ch, int numCh, int n) noexcept override
    {
        dsp.setThresholdDb (thr->load());
        dsp.setRatio (ratio->load());
        if (changed (atk->load(), lastAtk)) { lastAtk = atk->load(); dsp.setAttackMs (lastAtk); }
        if (changed (rel->load(), lastRel)) { lastRel = rel->load(); dsp.setReleaseMs (lastRel); }
        const bool a = autoRel->load() > 0.5f;
        if (a != lastAuto) { lastAuto = a; dsp.setAutoRelease (a); }
        dsp.setKneeDb (knee->load());
        dsp.setMakeupDb (makeup->load());
        dsp.setMix (mix->load() / 100.0f);
        dsp.setSidechainHpf (sc->load() < 20.0f ? 0.0f : sc->load());
        dsp.process (ch, numCh, n);
    }
    float getMeterDb() const noexcept override { return dsp.getGainReductionDb(); }
    String getMeterLabel() const override { return "GR"; }
    std::unique_ptr<Component> createPanel() override
    {
        return std::make_unique<ParamPanel> (*apvts, std::vector<ParamPanel::Spec> {
            { pid ("auto"), studioui::utf8 ("Release automático") },
            { pid ("threshold"), "THRESHOLD" }, { pid ("ratio"), "RATIO" }, { pid ("attack"), "ATTACK" }, { pid ("release"), "RELEASE" },
            { pid ("knee"), "KNEE" }, { pid ("scHpf"), "SC HPF" }, { pid ("makeup"), "MAKEUP" }, { pid ("mix"), "MIX" } },
            std::make_unique<ReductionMeter> ([this] { return getMeterDb(); }, "GR", 12.0f));
    }

private:
    dspcore::BusCompressor dsp;
    std::atomic<float> *thr {}, *ratio {}, *atk {}, *rel {}, *autoRel {}, *knee {}, *makeup {}, *mix {}, *sc {};
    float lastAtk = -1, lastRel = -1;
    bool lastAuto = false;
};

// ============================================================================
// 4. Saturación (4x)

class SatModule final : public Module
{
public:
    static void addParameters (Layout& l, const String& p)
    {
        addChoice (l, p, "mode", "Sat Mode", { "Tape", "Tube", "Transformer" }, 0);
        addFloat (l, p, "drive", "Sat Drive", range (0.0f, 24.0f), 4.0f, unit (" dB", 1));
        addFloat (l, p, "mix", "Sat Mix", range (0.0f, 100.0f), 100.0f, unit (" %", 0));
        addFloat (l, p, "output", "Sat Output", range (-12.0f, 12.0f), 0.0f, unit (" dB", 1));
        addBool (l, p, "autoGain", "Sat Auto Gain", true);
    }

    void bind (APVTS& s, const String& p) override
    {
        Module::bind (s, p);
        mode = param ("mode"); drive = param ("drive"); mix = param ("mix"); out = param ("output"); autoGain = param ("autoGain");
    }
    void prepare (double fs, int maxBlock, int numCh) override
    {
        os.prepare (numCh, maxBlock);
        dsp.prepare (fs * 4.0);
    }
    void process (float* const* ch, int numCh, int n) noexcept override
    {
        dsp.setMode ((dspcore::Saturator::Mode) (int) mode->load());
        dsp.setDriveDb (drive->load());
        dsp.setMix (mix->load() / 100.0f);
        dsp.setOutputDb (out->load());
        dsp.setAutoGain (autoGain->load() > 0.5f);
        os.process (ch, numCh, n, [this] (dsp::AudioBlock<float>& up)
        {
            float* p[2] = { up.getChannelPointer (0), up.getNumChannels() > 1 ? up.getChannelPointer (1) : nullptr };
            dsp.process (p, (int) up.getNumChannels(), (int) up.getNumSamples());
        });
    }
    int getLatency() const noexcept override { return os.latency(); }
    std::unique_ptr<Component> createPanel() override
    {
        return std::make_unique<ParamPanel> (*apvts, std::vector<ParamPanel::Spec> {
            { pid ("mode"), "MODO" }, { pid ("autoGain"), "Compensar nivel" },
            { pid ("drive"), "DRIVE" }, { pid ("mix"), "MIX" }, { pid ("output"), "OUTPUT" } });
    }

private:
    dspcore::Saturator dsp;
    Oversampler4x os;
    std::atomic<float> *mode {}, *drive {}, *mix {}, *out {}, *autoGain {};
};

// ============================================================================
// 5. Imagen estéreo

class ImagerModule final : public Module
{
public:
    static void addParameters (Layout& l, const String& p)
    {
        addFloat (l, p, "width", "Imager Width", range (0.0f, 200.0f), 100.0f, unit (" %", 0));
        addFloat (l, p, "bassMono", "Imager Bass Mono", range (0.0f, 300.0f, 120.0f), 0.0f, hzOrOff());
    }
    void bind (APVTS& s, const String& p) override { Module::bind (s, p); width = param ("width"); bass = param ("bassMono"); }
    void prepare (double fs, int, int) override { dsp.prepare (fs); }
    void process (float* const* ch, int numCh, int n) noexcept override
    {
        dsp.setWidth (width->load() / 100.0f);
        dsp.setBassMonoHz (bass->load() < 20.0f ? 0.0f : bass->load());
        dsp.process (ch, numCh, n);
    }
    std::unique_ptr<Component> createPanel() override
    {
        return std::make_unique<ParamPanel> (*apvts, std::vector<ParamPanel::Spec> {
            { pid ("width"), "ANCHO" }, { pid ("bassMono"), "GRAVES EN MONO" } });
    }

private:
    dspcore::StereoImager dsp;
    std::atomic<float> *width {}, *bass {};
};

// ============================================================================
// 6. Clipper (4x)

class ClipperModule final : public Module
{
public:
    static void addParameters (Layout& l, const String& p)
    {
        addFloat (l, p, "drive", "Clipper Drive", range (0.0f, 12.0f), 0.0f, unit (" dB", 1));
        addFloat (l, p, "ceiling", "Clipper Ceiling", range (-6.0f, 0.0f), 0.0f, unit (" dB", 1));
        addFloat (l, p, "knee", "Clipper Softness", range (0.0f, 100.0f), 30.0f, unit (" %", 0));
    }
    void bind (APVTS& s, const String& p) override
    {
        Module::bind (s, p);
        drive = param ("drive"); ceiling = param ("ceiling"); knee = param ("knee");
    }
    void prepare (double fs, int maxBlock, int numCh) override { juce::ignoreUnused (fs); os.prepare (numCh, maxBlock); }
    void process (float* const* ch, int numCh, int n) noexcept override
    {
        clipper.setCeilingDb (ceiling->load());
        clipper.setKnee (knee->load() / 100.0f);
        const float g = dbToGain (drive->load()), c = dbToGain (ceiling->load());
        float maxIn = 0.0f;
        os.process (ch, numCh, n, [&] (dsp::AudioBlock<float>& up)
        {
            for (size_t k = 0; k < up.getNumChannels(); ++k)
            {
                auto* d = up.getChannelPointer (k);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    const float x = d[i] * g;
                    maxIn = std::max (maxIn, std::fabs (x));
                    d[i] = clipper.process (x);
                }
            }
        });
        clipDb.store (maxIn > c ? 20.0f * std::log10 (maxIn / c) : 0.0f);
    }
    int getLatency() const noexcept override { return os.latency(); }
    float getMeterDb() const noexcept override { return clipDb.load(); }
    String getMeterLabel() const override { return "CLIP"; }
    std::unique_ptr<Component> createPanel() override
    {
        return std::make_unique<ParamPanel> (*apvts, std::vector<ParamPanel::Spec> {
            { pid ("drive"), "DRIVE" }, { pid ("ceiling"), "TECHO" }, { pid ("knee"), "SUAVIDAD" } },
            std::make_unique<ReductionMeter> ([this] { return getMeterDb(); }, "CLIP", 6.0f));
    }

private:
    dspcore::SoftClipper clipper;
    Oversampler4x os;
    std::atomic<float> *drive {}, *ceiling {}, *knee {};
    std::atomic<float> clipDb { 0.0f };
};

// ============================================================================
// 7. Limitador true-peak

class LimiterModule final : public Module
{
public:
    static void addParameters (Layout& l, const String& p)
    {
        addFloat (l, p, "gain", "Limiter Gain", range (0.0f, 24.0f), 0.0f, unit (" dB", 1));
        addFloat (l, p, "ceiling", "Limiter Ceiling", range (-12.0f, 0.0f), -1.0f, unit (" dBTP", 1));
        addFloat (l, p, "release", "Limiter Release", range (1.0f, 1000.0f, 80.0f), 80.0f, unit (" ms", 0));
        addFloat (l, p, "lookahead", "Limiter Lookahead", range (0.5f, 10.0f, 2.0f), 1.5f, unit (" ms", 1));
        addBool (l, p, "truePeak", "Limiter True Peak", true);
    }
    void bind (APVTS& s, const String& p) override
    {
        Module::bind (s, p);
        gain = param ("gain"); ceiling = param ("ceiling"); release = param ("release");
        lookahead = param ("lookahead"); tp = param ("truePeak");
    }
    void prepare (double fs, int, int) override
    {
        dsp.prepare (fs, 10.0f);
        lastLookahead = lookahead->load();
        dsp.setLookaheadMs (lastLookahead);
        dsp.reset();
    }
    void process (float* const* ch, int numCh, int n) noexcept override
    {
        dsp.setInputGainDb (gain->load());
        dsp.setCeilingDb (ceiling->load());
        if (changed (release->load(), lastRelease)) { lastRelease = release->load(); dsp.setReleaseMs (lastRelease); }
        if (changed (lookahead->load(), lastLookahead)) { lastLookahead = lookahead->load(); dsp.setLookaheadMs (lastLookahead); }
        dsp.setTruePeak (tp->load() > 0.5f);
        dsp.process (ch, numCh, n);
    }
    int getLatency() const noexcept override { return dsp.getLatencySamples(); }
    float getMeterDb() const noexcept override { return dsp.getGainReductionDb(); }
    String getMeterLabel() const override { return "GR"; }
    std::unique_ptr<Component> createPanel() override
    {
        return std::make_unique<ParamPanel> (*apvts, std::vector<ParamPanel::Spec> {
            { pid ("truePeak"), "True peak (ISP)" },
            { pid ("gain"), "GANANCIA" }, { pid ("ceiling"), "TECHO" }, { pid ("release"), "RELEASE" }, { pid ("lookahead"), "LOOKAHEAD" } },
            std::make_unique<ReductionMeter> ([this] { return getMeterDb(); }, "GR", 12.0f));
    }

private:
    dspcore::TruePeakLimiter dsp;
    std::atomic<float> *gain {}, *ceiling {}, *release {}, *lookahead {}, *tp {};
    float lastRelease = -1.0f, lastLookahead = -1.0f;
};

// ============================================================================
// 8. Dither

class DitherModule final : public Module
{
public:
    static void addParameters (Layout& l, const String& p)
    {
        addChoice (l, p, "bits", "Dither Bits", { "16 bits", "20 bits", "24 bits" }, 0);
        addBool (l, p, "shaping", "Dither Noise Shaping", false);
    }
    void bind (APVTS& s, const String& p) override { Module::bind (s, p); bits = param ("bits"); shaping = param ("shaping"); }
    void prepare (double, int, int) override { dsp.reset(); }
    void process (float* const* ch, int numCh, int n) noexcept override
    {
        static constexpr int table[] = { 16, 20, 24 };
        dsp.setBits (table[juce::jlimit (0, 2, (int) bits->load())]);
        dsp.setShaping (shaping->load() > 0.5f);
        dsp.process (ch, numCh, n);
    }
    std::unique_ptr<Component> createPanel() override
    {
        return std::make_unique<ParamPanel> (*apvts, std::vector<ParamPanel::Spec> {
            { pid ("bits"), studioui::utf8 ("RESOLUCIÓN DE EXPORTACIÓN") }, { pid ("shaping"), "Noise shaping" } });
    }

private:
    dspcore::Dither dsp;
    std::atomic<float> *bits {}, *shaping {};
};

// ============================================================================
// Medidor (solo como plugin suelto: la cadena tiene su propio medidor)

class MeterModule final : public Module
{
public:
    static void addParameters (Layout& l, const String& p)
    {
        addChoice (l, p, "platform", "Meter Platform", platformNames(), 1);
    }
    void bind (APVTS& s, const String& p) override { Module::bind (s, p); platform = param ("platform"); }
    void prepare (double fs, int, int numCh) override { meter.prepare (fs, numCh); }
    void process (float* const* ch, int numCh, int n) noexcept override
    {
        meter.process (ch, n);
        correlation.process (ch, numCh, n);
    }
    std::unique_ptr<Component> createPanel() override
    {
        MeterPanel::Sources s;
        s.output = &meter;
        s.correlation = &correlation.value;
        s.platform = platform;

        struct Panel final : Component
        {
            Panel (APVTS& state, const String& id, MeterPanel::Sources src) : meterPanel (src)
            {
                box.addItemList (platformNames(), 1);
                att = std::make_unique<APVTS::ComboBoxAttachment> (state, id, box);
                addAndMakeVisible (box);
                addAndMakeVisible (meterPanel);
            }
            void resized() override
            {
                auto r = getLocalBounds();
                box.setBounds (r.removeFromTop (26));
                r.removeFromTop (12);
                meterPanel.setBounds (r);
            }
            ComboBox box;
            std::unique_ptr<APVTS::ComboBoxAttachment> att;
            MeterPanel meterPanel;
        };
        return std::make_unique<Panel> (*apvts, pid ("platform"), s);
    }

private:
    dspcore::LoudnessMeter meter;
    CorrelationMeter correlation;
    std::atomic<float>* platform {};
};

// ============================================================================
// Registro

template <typename M>
static std::unique_ptr<Module> make() { return std::make_unique<M>(); }

const std::vector<ModuleInfo>& moduleRegistry()
{
    static const std::vector<ModuleInfo> reg {
        { "input",   "in_",   "Input",      studioui::utf8 ("Ganancia, filtro subsónico, polaridad y escucha en mono"), &InputModule::addParameters,   &make<InputModule> },
        { "eq",      "eq_",   "EQ",         studioui::utf8 ("EQ dinámico de 8 bandas con analizador (PrismaEQ)"),      &EqModule::addParameters,      &make<EqModule> },
        { "comp",    "comp_", "Compresor",  studioui::utf8 ("Compresor de bus VCA con release automático"),            &CompModule::addParameters,    &make<CompModule> },
        { "sat",     "sat_",  studioui::utf8 ("Saturación"), studioui::utf8 ("Cinta, válvula o transformador (4x)"),                    &SatModule::addParameters,     &make<SatModule> },
        { "imager",  "img_",  "Imagen",     "Ancho M/S y graves en mono",                             &ImagerModule::addParameters,  &make<ImagerModule> },
        { "clipper", "clip_", "Clipper",    "Recorta transitorios antes del limitador (4x)",          &ClipperModule::addParameters, &make<ClipperModule> },
        { "limiter", "lim_",  "Limitador",  "Limitador true-peak con lookahead",                      &LimiterModule::addParameters, &make<LimiterModule> },
        { "dither",  "dith_", "Dither",     studioui::utf8 ("TPDF con noise shaping opcional; siempre el último"),     &DitherModule::addParameters,  &make<DitherModule> },
        { "meter",   "mtr_",  "Medidor",    "Loudness EBU R128, true peak, LRA e histograma",         &MeterModule::addParameters,   &make<MeterModule>, false },
    };
    return reg;
}

const ModuleInfo* findModule (const String& id)
{
    for (auto& m : moduleRegistry())
        if (m.id == id) return &m;
    return nullptr;
}

} // namespace mastering
