#include <mastering/Widgets.h>

namespace mastering
{

using namespace studioui;

void ReductionMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (text, r.removeFromTop (16.0f), juce::Justification::centred);
    auto value = r.removeFromBottom (18.0f);
    g.setColour (Colours::text);
    g.setFont (mono (12.0f));
    g.drawText (juce::String (shown, 1) + " dB", value, juce::Justification::centred);

    auto bar = r.reduced (r.getWidth() * 0.5f - 8.0f, 4.0f);
    g.setColour (Colours::face);
    g.fillRoundedRectangle (bar, 3.0f);

    // Escala hacia abajo: 0 dB arriba (como los medidores de GR de hardware)
    const float frac = juce::jlimit (0.0f, 1.0f, shown / range);
    g.setColour (Colours::accent);
    g.fillRoundedRectangle (bar.withHeight (bar.getHeight() * frac), 3.0f);
    const float holdY = bar.getY() + bar.getHeight() * juce::jlimit (0.0f, 1.0f, peakHold / range);
    g.setColour (Colours::text.withAlpha (0.7f));
    g.drawHorizontalLine ((int) holdY, bar.getX(), bar.getRight());

    g.setFont (juce::FontOptions (9.0f));
    for (float db = 0.0f; db <= range; db += range / 4.0f)
    {
        const float y = bar.getY() + bar.getHeight() * db / range;
        g.setColour (Colours::dim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (bar.getRight() + 3.0f, y - 6.0f, 20.0f, 12.0f), juce::Justification::centredLeft);
    }
}

// ============================================================================

namespace
{
    void graphBackground (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (Colours::graphBg);
        g.fillRoundedRectangle (r, 6.0f);
    }
    float toDb (float lin) { return juce::Decibels::gainToDecibels (lin, -120.0f); }
}

void HistoryView::timerCallback()
{
    const auto blocks = module.getBlocksProcessed();
    if (blocks == lastBlocks) return;   // sin audio nuevo (transporte parado): el historial se congela
    lastBlocks = blocks;
    in[(size_t) pos]  = toDb (module.takeInputPeak());
    out[(size_t) pos] = toDb (module.takeOutputPeak());
    gr[(size_t) pos]  = module.takeMeterHold();
    pos = (pos + 1) % len;
    repaint();
}

void HistoryView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    graphBackground (g, area);
    auto r = area.reduced (8.0f, 8.0f).withTrimmedLeft (26.0f);
    constexpr float lo = -30.0f;   // en mastering la señal vive cerca de 0 dB: más resolución arriba
    auto yFor = [&] (float db) { return r.getBottom() - r.getHeight() * (juce::jlimit (lo, 0.0f, db) - lo) / -lo; };
    auto xFor = [&] (int i) { return r.getX() + r.getWidth() * (float) i / (float) (len - 1); };

    g.setFont (juce::FontOptions (9.0f));
    for (int db = 0; db >= -30; db -= 6)
    {
        g.setColour (Colours::grid);
        g.drawHorizontalLine ((int) yFor ((float) db), r.getX(), r.getRight());
        g.setColour (Colours::dim);
        g.drawText (juce::String (db), juce::Rectangle<float> (area.getX() + 4.0f, yFor ((float) db) - 6.0f, 24.0f, 12.0f), juce::Justification::centredLeft);
    }

    juce::Path inFill, outLine;
    inFill.startNewSubPath (r.getX(), r.getBottom());
    for (int i = 0; i < len; ++i)
    {
        const int k = (pos + i) % len;
        inFill.lineTo (xFor (i), yFor (in[(size_t) k]));
        if (i == 0) outLine.startNewSubPath (xFor (i), yFor (out[(size_t) k]));
        else outLine.lineTo (xFor (i), yFor (out[(size_t) k]));
    }
    inFill.lineTo (r.getRight(), r.getBottom());
    inFill.closeSubPath();
    g.setColour (Colours::dim.withAlpha (0.25f));
    g.fillPath (inFill);
    g.setColour (Colours::accent);
    g.strokePath (outLine, juce::PathStrokeType (1.5f));

    if (showGr)
    {
        // GR colgando desde arriba, en su propia escala
        juce::Path grFill;
        grFill.startNewSubPath (r.getX(), r.getY());
        float maxGr = 0.0f;
        for (int i = 0; i < len; ++i)
        {
            const float v = gr[(size_t) ((pos + i) % len)];
            maxGr = std::max (maxGr, v);
            grFill.lineTo (xFor (i), r.getY() + r.getHeight() * 0.5f * juce::jlimit (0.0f, 1.0f, v / grRange));
        }
        grFill.lineTo (r.getRight(), r.getY());
        grFill.closeSubPath();
        g.setColour (juce::Colour (0xffff8a5c).withAlpha (0.55f));
        g.fillPath (grFill);
        const auto label = "GR " + juce::String (gr[(size_t) ((pos + len - 1) % len)], 1) + utf8 (" dB  ·  máx ") + juce::String (maxGr, 1);
        auto pill = juce::Rectangle<float> (150.0f, 18.0f).withPosition (r.getRight() - 154.0f, r.getBottom() - 22.0f);
        g.setColour (Colours::face.withAlpha (0.9f));
        g.fillRoundedRectangle (pill, 4.0f);
        g.setColour (juce::Colour (0xffff8a5c));
        g.setFont (mono (11.0f));
        g.drawText (label, pill, juce::Justification::centred);
    }

    g.setFont (juce::FontOptions (10.0f));
    g.setColour (Colours::dim);
    g.drawText (utf8 ("entrada"), r.withTrimmedTop (r.getHeight() - 14.0f), juce::Justification::bottomLeft);
    g.setColour (Colours::accent);
    g.drawText ("salida", r.withTrimmedTop (r.getHeight() - 14.0f).withTrimmedLeft (52.0f), juce::Justification::bottomLeft);
}

void TransferCurveView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    graphBackground (g, area);
    // Cuadrado centrado: entrada (dB) en X, salida (dB) en Y, de -36 a +12
    const float side = std::min (area.getWidth(), area.getHeight()) - 30.0f;
    auto r = juce::Rectangle<float> (side, side).withCentre (area.getCentre()).translated (8.0f, -4.0f);
    constexpr float lo = -36.0f, hi = 12.0f;
    auto xFor = [&] (float db) { return r.getX() + r.getWidth() * (db - lo) / (hi - lo); };
    auto yFor = [&] (float db) { return r.getBottom() - r.getHeight() * (juce::jlimit (lo, hi, db) - lo) / (hi - lo); };

    g.setFont (juce::FontOptions (9.0f));
    for (int db = -36; db <= 12; db += 12)
    {
        g.setColour (db == 0 ? Colours::hairline.brighter (0.2f) : Colours::grid);
        g.drawVerticalLine ((int) xFor ((float) db), r.getY(), r.getBottom());
        g.drawHorizontalLine ((int) yFor ((float) db), r.getX(), r.getRight());
        g.setColour (Colours::dim);
        g.drawText (juce::String (db), juce::Rectangle<float> (xFor ((float) db) - 12.0f, r.getBottom() + 2.0f, 24.0f, 11.0f), juce::Justification::centred);
        g.drawText (juce::String (db), juce::Rectangle<float> (r.getX() - 26.0f, yFor ((float) db) - 6.0f, 22.0f, 12.0f), juce::Justification::centredRight);
    }

    // Diagonal de referencia (sin procesar)
    g.setColour (Colours::hairline);
    const float dashes[] = { 3.0f, 3.0f };
    g.drawDashedLine (juce::Line<float> (xFor (lo), yFor (lo), xFor (hi), yFor (hi)), dashes, 2);

    // Techo (solo si el módulo tiene uno, p. ej. el clipper)
    if (ceiling)
    {
        g.setColour (juce::Colour (0xffff6b6b).withAlpha (0.7f));
        g.drawHorizontalLine ((int) yFor (ceiling()), r.getX(), r.getRight());
    }

    // Curva: pico de salida para un pico de entrada (máximo de ambas polaridades: cubre curvas asimétricas)
    juce::Path p;
    for (int i = 0; i <= 200; ++i)
    {
        const float inDb = lo + (hi - lo) * (float) i / 200.0f;
        const float a = juce::Decibels::decibelsToGain (inDb);
        const float o = std::max (std::fabs (transfer (a)), std::fabs (transfer (-a)));
        const float outDb = toDb (o);
        if (i == 0) p.startNewSubPath (xFor (inDb), yFor (outDb)); else p.lineTo (xFor (inDb), yFor (outDb));
    }
    g.setColour (Colours::text);
    g.strokePath (p, juce::PathStrokeType (2.0f));

    // Nivel en vivo
    if (live > 1.0e-4f)
    {
        const float inDb = juce::jlimit (lo, hi, toDb (live));
        const float a = juce::Decibels::decibelsToGain (inDb);
        const float outDb = toDb (std::max (std::fabs (transfer (a)), std::fabs (transfer (-a))));
        g.setColour (Colours::accent);
        g.fillEllipse (xFor (inDb) - 5.0f, yFor (outDb) - 5.0f, 10.0f, 10.0f);
    }
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (utf8 ("entrada → salida (dB)"), area.reduced (8.0f).withHeight (14.0f), juce::Justification::topLeft);
}

void Goniometer::timerCallback()
{
    double lr = 0, ll = 0, rr = 0;
    float peak = 1.0e-6f;
    fifo.pull ([&] (float l, float r)
    {
        // Rotación 45°: Mid arriba, Side a los lados
        const juce::Point<float> p ((l - r) * 0.70710678f, (l + r) * 0.70710678f);
        pts[(size_t) write] = p;
        write = (write + 1) % maxPoints;
        count = std::min (count + 1, maxPoints);
        lr += l * (double) r; ll += l * (double) l; rr += r * (double) r;
        peak = std::max (peak, std::hypot (p.x, p.y));   // escala sobre los ejes ya rotados
    });
    if (ll > 0 && rr > 0)
        correlation = 0.8f * correlation + 0.2f * (float) (lr / std::sqrt (ll * rr));
    gain = 0.9f * gain + 0.1f * juce::jlimit (1.0f, 20.0f, 0.9f / peak);   // autoescala suave
    repaint();
}

void Goniometer::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    graphBackground (g, area);
    auto r = area.reduced (10.0f).withTrimmedBottom (34.0f);
    const float radius = std::min (r.getWidth(), r.getHeight()) * 0.5f;
    const auto c = r.getCentre();

    g.setColour (Colours::grid);
    g.drawEllipse (c.x - radius, c.y - radius, 2 * radius, 2 * radius, 1.0f);
    g.drawLine (c.x, c.y - radius, c.x, c.y + radius);
    g.drawLine (c.x - radius, c.y, c.x + radius, c.y);
    g.setColour (Colours::hairline);
    g.drawLine (c.x - radius * 0.707f, c.y - radius * 0.707f, c.x + radius * 0.707f, c.y + radius * 0.707f);
    g.drawLine (c.x + radius * 0.707f, c.y - radius * 0.707f, c.x - radius * 0.707f, c.y + radius * 0.707f);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("M", juce::Rectangle<float> (c.x - 10, c.y - radius - 2, 20, 12), juce::Justification::centred);
    g.drawText ("L", juce::Rectangle<float> (c.x - radius * 0.75f - 14, c.y - radius * 0.75f - 12, 14, 12), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (c.x + radius * 0.75f, c.y - radius * 0.75f - 12, 14, 12), juce::Justification::centred);

    for (int i = 0; i < count; ++i)
    {
        const auto& p = pts[(size_t) ((write - 1 - i + maxPoints) % maxPoints)];
        const float px = p.x * gain, py = p.y * gain;
        if (px * px + py * py > 1.0f) continue;   // fuera del círculo: no se dibuja (recortar crea líneas falsas)
        const float x = c.x + px * radius;
        const float y = c.y - py * radius;
        g.setColour (Colours::accent.withAlpha (0.55f * (1.0f - (float) i / (float) maxPoints) + 0.05f));
        g.fillRect (x, y, 1.5f, 1.5f);
    }

    // Correlación
    auto bar = area.reduced (14.0f, 10.0f).removeFromBottom (14.0f);
    g.setColour (Colours::face);
    g.fillRoundedRectangle (bar, 3.0f);
    const float cx = bar.getCentreX(), x = bar.getX() + bar.getWidth() * (juce::jlimit (-1.0f, 1.0f, correlation) + 1.0f) * 0.5f;
    g.setColour (correlation < 0.0f ? juce::Colour (0xffff6b6b) : Colours::accent);
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (cx, x), bar.getY(), std::max (cx, x), bar.getBottom()));
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (utf8 ("correlación ") + juce::String (correlation, 2), bar.translated (0.0f, -15.0f), juce::Justification::centredLeft);
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (text, r.removeFromTop (14.0f), juce::Justification::centred);
    auto value = r.removeFromBottom (16.0f);
    auto bar = r.reduced (r.getWidth() * 0.5f - 6.0f, 2.0f);
    g.setColour (Colours::face);
    g.fillRoundedRectangle (bar, 3.0f);
    constexpr float lo = -60.0f;
    auto yFor = [&] (float db) { return bar.getBottom() - bar.getHeight() * (juce::jlimit (lo, 0.0f, db) - lo) / -lo; };
    const float y = yFor (shown);
    const auto col = shown > -1.0f ? juce::Colour (0xffff6b6b) : shown > -6.0f ? juce::Colour (0xffffc048) : Colours::accent;
    g.setColour (col);
    g.fillRoundedRectangle (bar.withTop (y), 3.0f);
    g.setColour (Colours::text);
    g.drawHorizontalLine ((int) yFor (hold), bar.getX(), bar.getRight());
    g.setColour (Colours::text);
    g.setFont (mono (10.0f));
    g.drawText (hold > -89.0f ? juce::String (hold, 1) : juce::String ("-inf"), value, juce::Justification::centred);
}

// ============================================================================

ParamPanel::ParamPanel (APVTS& state, const std::vector<Spec>& specs, std::unique_ptr<juce::Component> meter,
                        std::unique_ptr<juce::Component> visual)
    : meterComponent (std::move (meter)), visualComponent (std::move (visual))
{
    for (auto& s : specs)
    {
        auto* p = state.getParameter (s.id);
        jassert (p != nullptr);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
        {
            auto c = std::make_unique<Choice>();
            c->label.setText (s.label, juce::dontSendNotification);
            c->label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            c->box.addItemList (choice->choices, 1);
            addAndMakeVisible (c->label);
            addAndMakeVisible (c->box);
            c->att = std::make_unique<APVTS::ComboBoxAttachment> (state, s.id, c->box);
            choices.push_back (std::move (c));
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
        {
            auto t = std::make_unique<juce::ToggleButton> (s.label);
            addAndMakeVisible (*t);
            toggleAtts.push_back (std::make_unique<APVTS::ButtonAttachment> (state, s.id, *t));
            toggles.push_back (std::move (t));
        }
        else
        {
            auto k = std::make_unique<LabelledKnob> (s.label);
            k->slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
            addAndMakeVisible (*k);
            knobAtts.push_back (std::make_unique<APVTS::SliderAttachment> (state, s.id, k->slider));
            knobs.push_back (std::move (k));
        }
    }
    if (meterComponent)
        addAndMakeVisible (*meterComponent);
    if (visualComponent)
        addAndMakeVisible (*visualComponent);
}

void ParamPanel::resized()
{
    auto r = getLocalBounds();
    if (meterComponent)
        meterComponent->setBounds (r.removeFromRight (80).reduced (0, 8));

    // Zona superior: opciones a la izquierda y perillas a la derecha.
    // Si hay visualizador, ocupa todo el ancho por debajo.
    const int n = (int) knobs.size();
    const int cols = std::max (1, std::min (4, n)), rows = (n + cols - 1) / cols;
    const int knobH = visualComponent ? 128 : 150;
    int optionsH = 0;
    for (size_t i = 0; i < choices.size(); ++i) optionsH += 54;
    for (size_t i = 0; i < toggles.size(); ++i) optionsH += 32;
    const int topH = visualComponent ? std::max (rows * knobH, optionsH + 8) : r.getHeight();

    auto top = r.removeFromTop (topH);
    if (visualComponent)
        visualComponent->setBounds (r.withTrimmedTop (10));

    if (! choices.empty() || ! toggles.empty())
    {
        auto left = top.removeFromLeft (180).reduced (0, 8);
        for (auto& c : choices)
        {
            c->label.setBounds (left.removeFromTop (18));
            c->box.setBounds (left.removeFromTop (26));
            left.removeFromTop (10);
        }
        for (auto& t : toggles)
        {
            t->setBounds (left.removeFromTop (26));
            left.removeFromTop (6);
        }
        top.removeFromLeft (16);
    }

    if (n == 0) return;
    const int cellW = std::min (130, top.getWidth() / cols), cellH = std::min (knobH, top.getHeight() / rows);
    for (int i = 0; i < n; ++i)
    {
        const int row = i / cols, col = i % cols;
        knobs[(size_t) i]->setBounds (top.getX() + col * cellW, top.getY() + row * cellH, cellW, cellH);
    }
}

} // namespace mastering
