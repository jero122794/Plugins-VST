#include <studioui/EqView.h>

namespace studioui
{

static constexpr int numBands = EqCore::numBands;

// ============================================================================
// Analizador

void EqGraph::Analyzer::consume (const float* d, int n)
{
    for (int i = 0; i < n; ++i)
    {
        ring[(size_t) writePos] = d[i];
        writePos = (writePos + 1) & (size - 1);
    }
    active = true;
}

void EqGraph::Analyzer::compute()
{
    for (int i = 0; i < size; ++i)
        work[(size_t) i] = ring[(size_t) ((writePos + i) & (size - 1))];
    std::fill (work.begin() + size, work.end(), 0.0f);
    window.multiplyWithWindowingTable (work.data(), (size_t) size);
    fft.performFrequencyOnlyForwardTransform (work.data(), true);

    // Ventana Hann: un seno de amplitud A da un pico de A·N/4
    const float norm = 4.0f / (float) size;
    for (int i = 0; i < size / 2; ++i)
    {
        const float db = juce::Decibels::gainToDecibels (work[(size_t) i] * norm, -120.0f);
        float& s = smoothedDb[(size_t) i];
        s = db > s ? db : s + 0.2f * (db - s);  // sube al instante, cae suave
    }
}

// ============================================================================
// Gráfico

EqGraph::EqGraph (EqCore& c, std::function<void (int)> cb)
    : core (c), onSelect (std::move (cb)), popup (std::make_unique<BandPanel> (c))
{
    addChildComponent (*popup);
    startTimerHz (30);
}

EqGraph::~EqGraph() = default;

void EqGraph::setSelected (int band)
{
    selected = band;
    popup->showBand (band);
    placePopup();
    repaint();
}

void EqGraph::showPopup (bool show)
{
    popupVisible = show;
    placePopup();
}

/** Panel flotante junto al nodo: debajo si el nodo está arriba, encima si está abajo; nunca fuera del gráfico. */
void EqGraph::placePopup()
{
    const bool visible = popupVisible && params[(size_t) selected].enabled && dragging < 0;
    popup->setVisible (visible);
    if (! visible) return;
    const int w = 520, h = 150;
    const auto node = nodePosition (selected);
    const float y = node.y < plot.getCentreY() ? node.y + 22.0f : node.y - 22.0f - (float) h;
    const float x = juce::jlimit (plot.getX() + 6.0f, plot.getRight() - (float) w - 6.0f, node.x - (float) w * 0.5f);
    popup->setBounds (juce::Rectangle<float> (x, juce::jlimit (plot.getY() + 6.0f, plot.getBottom() - (float) h - 6.0f, y), (float) w, (float) h).toNearestInt());
}

void EqGraph::resized()
{
    plot = getLocalBounds().toFloat().reduced (1.0f);
}

float EqGraph::xForFreq (float f) const
{
    return plot.getX() + plot.getWidth() * std::log (f / minFreq) / std::log (maxFreq / minFreq);
}

float EqGraph::freqForX (float x) const
{
    return minFreq * std::pow (maxFreq / minFreq, (x - plot.getX()) / plot.getWidth());
}

float EqGraph::yForDb (float db) const
{
    return plot.getCentreY() - db / rangeDb * plot.getHeight() * 0.5f;
}

float EqGraph::dbForY (float y) const
{
    return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * rangeDb;
}

float EqGraph::yForAnalyzerDb (float db) const
{
    return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 96.0f);
}

juce::Point<float> EqGraph::nodePosition (int b) const
{
    const auto& p = params[(size_t) b];
    const float x = xForFreq (juce::jlimit (minFreq, maxFreq, p.freq));
    const float db = p.hasGain() ? juce::jlimit (-rangeDb, rangeDb, p.gainDb) : 0.0f;
    return { x, yForDb (db) };
}

int EqGraph::hitTest (juce::Point<float> pos) const
{
    int best = -1;
    float bestDist = 11.0f;
    for (int b = 0; b < numBands; ++b)
    {
        if (! params[(size_t) b].enabled) continue;
        const float d = nodePosition (b).getDistanceFrom (pos);
        if (d < bestDist) { bestDist = d; best = b; }
    }
    return best;
}

void EqGraph::timerCallback()
{
    core.preFifo.pull ([this] (const float* d, int n) { pre.consume (d, n); });
    core.postFifo.pull ([this] (const float* d, int n) { post.consume (d, n); });
    if (pre.active)  pre.compute();
    if (post.active) post.compute();

    for (int b = 0; b < numBands; ++b)
    {
        params[(size_t) b] = core.readBand (b);
        dyn[(size_t) b] = core.getDynamicGainDb (b);
    }
    popup->refreshEnablement();
    placePopup();
    repaint();
}

juce::Path EqGraph::analyzerPath (const Analyzer& a, bool closed) const
{
    const double fs = core.getBaseRate() > 0 ? core.getBaseRate() : 48000.0;
    juce::Path path;
    bool started = false;
    for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
    {
        const float f = freqForX (x);
        const float bin = f * (float) Analyzer::size / (float) fs;
        const int i0 = juce::jlimit (0, Analyzer::size / 2 - 2, (int) bin);
        const float frac = juce::jlimit (0.0f, 1.0f, bin - (float) i0);
        float db = a.smoothedDb[(size_t) i0] + frac * (a.smoothedDb[(size_t) i0 + 1] - a.smoothedDb[(size_t) i0]);
        db += 4.5f * std::log2 (f / 1000.0f);  // inclinación de 4.5 dB/oct: referencia de espectro medio de una mezcla
        const float y = yForAnalyzerDb (db);
        if (! started) { path.startNewSubPath (x, closed ? plot.getBottom() : y); if (closed) path.lineTo (x, y); started = true; }
        else path.lineTo (x, y);
    }
    if (closed && started)
    {
        path.lineTo (plot.getRight(), plot.getBottom());
        path.closeSubPath();
    }
    return path;
}

void EqGraph::paint (juce::Graphics& g)
{
    g.setColour (Colours::graphBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    // --- Rejilla
    g.setFont (juce::FontOptions (10.0f));
    for (int f : { 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                   1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 })
    {
        const float x = xForFreq ((float) f);
        const bool major = f == 100 || f == 1000 || f == 10000;
        g.setColour (major ? Colours::hairline : Colours::grid);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
    }
    for (auto [f, txt] : { std::pair { 50.f, "50" }, { 100.f, "100" }, { 200.f, "200" }, { 500.f, "500" },
                           { 1000.f, "1k" }, { 2000.f, "2k" }, { 5000.f, "5k" }, { 10000.f, "10k" } })
    {
        g.setColour (Colours::dim);
        g.drawText (txt, juce::Rectangle<float> (xForFreq (f) - 20.0f, plot.getBottom() - 16.0f, 40.0f, 14.0f), juce::Justification::centred);
    }
    for (int db = -18; db <= 18; db += 6)
    {
        const float y = yForDb ((float) db);
        g.setColour (db == 0 ? Colours::hairline.brighter (0.2f) : Colours::grid);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        if (db != 0)
        {
            g.setColour (Colours::dim);
            g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (plot.getRight() - 30.0f, y - 7.0f, 26.0f, 14.0f),
                        juce::Justification::centredRight);
        }
    }

    // --- Analizador: post relleno con degradado, pre como línea tenue (para comparar antes/después)
    if (post.active)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d7fc4).withAlpha (0.55f), 0.0f, plot.getY() + plot.getHeight() * 0.25f,
                                                 juce::Colour (0xff1d3a5c).withAlpha (0.08f), 0.0f, plot.getBottom(), false));
        g.fillPath (analyzerPath (post, true));
        g.setColour (juce::Colour (0xff7fb8f0).withAlpha (0.6f));
        g.strokePath (analyzerPath (post, false), juce::PathStrokeType (1.0f));
    }
    if (pre.active)
    {
        g.setColour (Colours::text.withAlpha (0.22f));
        g.strokePath (analyzerPath (pre, false), juce::PathStrokeType (1.0f));
    }

    // --- Curvas
    const double rate = core.getProcessingRate();
    const int numPts = juce::jmax (2, (int) plot.getWidth() / 2);
    std::vector<float> total ((size_t) numPts, 0.0f), staticTotal ((size_t) numPts, 0.0f), freqs ((size_t) numPts);
    for (int i = 0; i < numPts; ++i)
        freqs[(size_t) i] = freqForX (plot.getX() + plot.getWidth() * (float) i / (float) (numPts - 1));

    bool anyDynamic = false;
    for (int b = 0; b < numBands; ++b)
    {
        const auto& p = params[(size_t) b];
        if (! p.enabled) continue;
        anyDynamic |= p.isDynamic();

        juce::Path fill;
        fill.startNewSubPath (plot.getX(), yForDb (0.0f));
        for (int i = 0; i < numPts; ++i)
        {
            const float d  = dspcore::EqBand::responseDb (p, rate, freqs[(size_t) i], dyn[(size_t) b]);
            total[(size_t) i] += d;
            staticTotal[(size_t) i] += p.isDynamic() ? dspcore::EqBand::responseDb (p, rate, freqs[(size_t) i]) : d;
            fill.lineTo (plot.getX() + plot.getWidth() * (float) i / (float) (numPts - 1), yForDb (juce::jlimit (-rangeDb * 1.2f, rangeDb * 1.2f, d)));
        }
        fill.lineTo (plot.getRight(), yForDb (0.0f));
        fill.closeSubPath();
        g.setColour (Colours::band (b).withAlpha (b == selected ? 0.22f : 0.08f));
        g.fillPath (fill);

        if (p.isDynamic())
        {
            // Hasta dónde puede llegar la banda: área entre la curva estática y la del rango completo
            juce::Path span;
            auto xAt = [&] (int i) { return plot.getX() + plot.getWidth() * (float) i / (float) (numPts - 1); };
            for (int i = 0; i < numPts; ++i)
            {
                const float y = yForDb (juce::jlimit (-rangeDb * 1.2f, rangeDb * 1.2f, dspcore::EqBand::responseDb (p, rate, freqs[(size_t) i])));
                if (i == 0) span.startNewSubPath (xAt (i), y); else span.lineTo (xAt (i), y);
            }
            for (int i = numPts - 1; i >= 0; --i)
                span.lineTo (xAt (i), yForDb (juce::jlimit (-rangeDb * 1.2f, rangeDb * 1.2f,
                                                            dspcore::EqBand::responseDb (p, rate, freqs[(size_t) i], p.dynRangeDb))));
            span.closeSubPath();
            g.setColour (Colours::band (b).withAlpha (0.20f));
            g.fillPath (span);
        }
    }

    auto curve = [&] (const std::vector<float>& v)
    {
        juce::Path path;
        for (int i = 0; i < numPts; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) (numPts - 1);
            const float y = yForDb (juce::jlimit (-rangeDb * 1.2f, rangeDb * 1.2f, v[(size_t) i]));
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        return path;
    };

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());
    if (anyDynamic)
    {
        g.setColour (Colours::text.withAlpha (0.35f));
        juce::Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, curve (staticTotal), dashes, 2);
        g.fillPath (dashed);
    }
    g.setColour (Colours::text);
    g.strokePath (curve (total), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.restoreState();

    // --- Nodos
    for (int b = 0; b < numBands; ++b)
    {
        const auto& p = params[(size_t) b];
        if (! p.enabled) continue;
        const auto pos = nodePosition (b);
        const auto col = Colours::band (b);

        if (p.isDynamic())  // indicador de cuánto se está moviendo la banda ahora
        {
            const float y2 = yForDb (juce::jlimit (-rangeDb, rangeDb, p.gainDb + dyn[(size_t) b]));
            g.setColour (col.withAlpha (0.8f));
            g.drawLine (pos.x, pos.y, pos.x, y2, 3.0f);
        }

        const float r = b == selected ? 9.0f : (b == hovered ? 8.0f : 7.0f);
        if (b == selected)
        {
            g.setColour (col.withAlpha (0.25f));
            g.fillEllipse (pos.x - r - 7, pos.y - r - 7, 2 * r + 14, 2 * r + 14);
        }
        g.setColour (Colours::graphBg);
        g.fillEllipse (pos.x - r - 2, pos.y - r - 2, 2 * r + 4, 2 * r + 4);
        g.setColour (col);
        g.fillEllipse (pos.x - r, pos.y - r, 2 * r, 2 * r);
        g.setColour (Colours::graphBg);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (pos.x - r, pos.y - r, 2 * r, 2 * r), juce::Justification::centred);
        if (b == selected)
        {
            g.setColour (Colours::text);
            g.drawEllipse (pos.x - r - 3, pos.y - r - 3, 2 * r + 6, 2 * r + 6, 1.5f);
        }
    }

    // --- Lectura bajo el cursor: frecuencia, nota y ganancia (ayuda a cazar resonancias)
    if (dragging < 0 && plot.contains (mousePos) && ! (popup->isVisible() && popup->getBounds().toFloat().contains (mousePos)))
    {
        const float f = freqForX (mousePos.x);
        g.setColour (Colours::text.withAlpha (0.25f));
        g.drawVerticalLine ((int) mousePos.x, plot.getY(), plot.getBottom());
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (f / 440.0));
        const float cents = (float) (100.0 * (69.0 + 12.0 * std::log2 (f / 440.0) - midi));
        juce::String txt = (f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz" : juce::String ((int) std::round (f)) + " Hz")
                         + "   " + names[((midi % 12) + 12) % 12] + juce::String (midi / 12 - 1)
                         + (std::abs (cents) >= 1.0f ? juce::String ((cents > 0 ? " +" : " ") + juce::String ((int) cents) + "c") : juce::String())
                         + "   " + (dbForY (mousePos.y) >= 0 ? "+" : "") + juce::String (dbForY (mousePos.y), 1) + " dB";
        const float w = 210.0f;
        auto box = juce::Rectangle<float> (w, 20.0f).withPosition (juce::jlimit (plot.getX() + 4.0f, plot.getRight() - w - 4.0f, mousePos.x + 12.0f),
                                                                   juce::jlimit (plot.getY() + 4.0f, plot.getBottom() - 24.0f, mousePos.y - 28.0f));
        g.setColour (Colours::face.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (Colours::text);
        g.setFont (mono (11.0f));
        g.drawText (txt, box, juce::Justification::centred);
    }

    if (std::none_of (params.begin(), params.end(), [] (auto& p) { return p.enabled; }))
    {
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Doble clic para crear una banda", plot.withHeight (plot.getHeight() * 0.4f), juce::Justification::centred);
    }
}

// --- Interacción -------------------------------------------------------------

void EqGraph::setParam (int b, const char* name, float plainValue)
{
    if (auto* param = core.state->getParameter (EqCore::bandId (core.prefix, b, name)))
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

void EqGraph::beginGesture (int b)
{
    for (auto* n : { "freq", "gain" })
        if (auto* param = core.state->getParameter (EqCore::bandId (core.prefix, b, n))) param->beginChangeGesture();
}

void EqGraph::endGesture (int b)
{
    for (auto* n : { "freq", "gain" })
        if (auto* param = core.state->getParameter (EqCore::bandId (core.prefix, b, n))) param->endChangeGesture();
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    const int hit = hitTest (e.position);
    if (hit < 0)
    {
        showPopup (false);   // clic en vacío: cierra el panel flotante
        return;
    }
    selected = hit;
    onSelect (hit);
    dragging = hit;
    popupVisible = true;
    placePopup();
    beginGesture (hit);
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const auto pos = e.position.withX (juce::jlimit (plot.getX(), plot.getRight(), e.position.x))
                               .withY (juce::jlimit (plot.getY(), plot.getBottom(), e.position.y));
    setParam (dragging, "freq", freqForX (pos.x));
    if (params[(size_t) dragging].hasGain())
        setParam (dragging, "gain", juce::jlimit (-rangeDb, rangeDb, dbForY (pos.y)));
}

void EqGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0) endGesture (dragging);
    dragging = -1;
    placePopup();
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int hit = hitTest (e.position); hit >= 0)
    {
        setParam (hit, "on", 0.0f);  // doble clic en un nodo: apagar la banda
        return;
    }
    for (int b = 0; b < numBands; ++b)
    {
        if (params[(size_t) b].enabled) continue;
        const float f = freqForX (e.position.x);
        // En los extremos se crean cortes; en el resto, campanas
        const int type = f < 35.0f ? 3 : (f > 15000.0f ? 4 : 0);
        setParam (b, "type", (float) type);
        setParam (b, "freq", f);
        setParam (b, "gain", type == 0 ? juce::jlimit (-rangeDb, rangeDb, dbForY (e.position.y)) : 0.0f);
        setParam (b, "q", type == 0 ? 1.0f : 0.707f);
        setParam (b, "dynRange", 0.0f);
        setParam (b, "on", 1.0f);
        selected = b;
        onSelect (b);
        showPopup (true);
        return;
    }
}

void EqGraph::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int b = hovered >= 0 ? hovered : selected;
    if (! params[(size_t) b].enabled) return;
    const float q = juce::jlimit (0.1f, 18.0f, params[(size_t) b].q * std::exp (w.deltaY * 1.5f));
    setParam (b, "q", q);
}

void EqGraph::mouseExit (const juce::MouseEvent&)
{
    mousePos = { -1.0f, -1.0f };
    hovered = -1;
    repaint();
}

void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    const int h = hitTest (e.position);
    if (h != hovered) { hovered = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

// ============================================================================
// Panel de banda

BandPanel::BandPanel (EqCore& eqCore) : core (eqCore)
{
    type.addItemList (EqCore::typeNames, 1);
    slope.addItemList (EqCore::slopeNames, 1);
    placement.addItemList (EqCore::placementNames, 1);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &on, &type, &slope, &placement, &freq, &gain, &q, &dynRange, &threshold, &attack, &release })
        addAndMakeVisible (c);

    showBand (0);
}

void BandPanel::showBand (int b)
{
    band = b;
    // Primero se destruyen los attachments viejos, después se crean los nuevos
    onAtt.reset(); typeAtt.reset(); slopeAtt.reset(); placementAtt.reset();
    freqAtt.reset(); gainAtt.reset(); qAtt.reset(); dynAtt.reset(); thrAtt.reset(); atkAtt.reset(); relAtt.reset();

    auto& s = *core.state;
    auto id = [&] (const char* n) { return EqCore::bandId (core.prefix, b, n); };
    onAtt        = std::make_unique<BA> (s, id ("on"), on);
    typeAtt      = std::make_unique<CA> (s, id ("type"), type);
    slopeAtt     = std::make_unique<CA> (s, id ("slope"), slope);
    placementAtt = std::make_unique<CA> (s, id ("placement"), placement);
    freqAtt      = std::make_unique<SA> (s, id ("freq"), freq.slider);
    gainAtt      = std::make_unique<SA> (s, id ("gain"), gain.slider);
    qAtt         = std::make_unique<SA> (s, id ("q"), q.slider);
    dynAtt       = std::make_unique<SA> (s, id ("dynRange"), dynRange.slider);
    thrAtt       = std::make_unique<SA> (s, id ("threshold"), threshold.slider);
    atkAtt       = std::make_unique<SA> (s, id ("attack"), attack.slider);
    relAtt       = std::make_unique<SA> (s, id ("release"), release.slider);

    for (auto* k : { &freq, &gain, &q, &dynRange, &threshold, &attack, &release })
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::band (b));
    refreshEnablement();
    repaint();
}

void BandPanel::refreshEnablement()
{
    const auto p = core.readBand (band);
    gain.setEnabled (p.hasGain());
    dynRange.setEnabled (p.hasGain());
    for (auto* k : { &threshold, &attack, &release })
        k->setEnabled (p.isDynamic());
    slope.setEnabled (p.isCut());
    for (juce::Component* c : std::initializer_list<juce::Component*> { &type, &slope, &placement, &freq, &gain, &q, &dynRange, &threshold, &attack, &release })
        c->setAlpha (p.enabled ? 1.0f : 0.45f);
}

void BandPanel::paint (juce::Graphics& g)
{
    // Panel flotante: fondo opaco, borde del color de la banda y sombra
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 14, { 0, 4 }).drawForRectangle (g, r.toNearestInt());
    g.setColour (Colours::face.withAlpha (0.97f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colours::band (band).withAlpha (0.8f));
    g.drawRoundedRectangle (r, 8.0f, 1.5f);

    auto head = r.reduced (12.0f, 8.0f).removeFromTop (26.0f);
    drawFilterIcon (g, head.removeFromLeft (34.0f).reduced (0.0f, 3.0f), core.readBand (band), Colours::band (band));
    g.setColour (Colours::band (band));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (juce::String (band + 1), head.removeFromLeft (22.0f), juce::Justification::centredLeft);
}

void BandPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    auto head = r.removeFromTop (26);
    head.removeFromLeft (60);
    on.setBounds (head.removeFromLeft (56));
    head.removeFromLeft (6);
    type.setBounds (head.removeFromLeft (118));
    head.removeFromLeft (6);
    slope.setBounds (head.removeFromLeft (104));
    head.removeFromLeft (6);
    placement.setBounds (head.removeFromLeft (96));

    r.removeFromTop (4);
    const int w = r.getWidth() / 7;
    for (auto* k : { &freq, &gain, &q, &dynRange, &threshold, &attack, &release })
    {
        k->label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        k->setBounds (r.removeFromLeft (w));
    }
}

// ============================================================================
// Icono de forma de filtro

void drawFilterIcon (juce::Graphics& g, juce::Rectangle<float> area, const dspcore::EqBandParams& source, juce::Colour colour)
{
    // Prototipo de la forma, con ganancia y frecuencia fijas para que el icono sea siempre legible
    auto p = source;
    p.enabled = true;
    p.freq = 1000.0f;
    p.q = p.isCut() ? 0.707f : 1.0f;
    p.gainDb = p.hasGain() ? (source.gainDb < 0.0f ? -9.0f : 9.0f) : 0.0f;
    p.dynRangeDb = 0.0f;
    juce::Path path;
    for (int i = 0; i <= 40; ++i)
    {
        const double f = 40.0 * std::pow (25000.0 / 40.0, i / 40.0);
        const float db = juce::jlimit (-14.0f, 14.0f, dspcore::EqBand::responseDb (p, 96000.0, f));
        const float x = area.getX() + area.getWidth() * (float) i / 40.0f;
        const float y = area.getCentreY() - db / 14.0f * area.getHeight() * 0.5f;
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ============================================================================
// Tira de bandas

BandStrip::BandStrip (EqCore& c, std::function<void (int)> cb) : core (c), onSelect (std::move (cb))
{
    for (int b = 0; b < numBands; ++b)
    {
        auto& btn = onButtons[(size_t) b];
        btn.setButtonText ("ON");
        btn.setClickingTogglesState (true);
        btn.setColour (juce::TextButton::buttonOnColourId, Colours::band (b).withAlpha (0.85f));
        btn.setColour (juce::TextButton::textColourOnId, Colours::graphBg);
        addAndMakeVisible (btn);
        onAtts[(size_t) b] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            *core.state, EqCore::bandId (core.prefix, b, "on"), btn);
    }
    startTimerHz (15);
}

juce::Rectangle<int> BandStrip::cellBounds (int b) const
{
    const float w = (float) getWidth() / (float) numBands;
    return juce::Rectangle<float> (w * (float) b, 0.0f, w, (float) getHeight()).reduced (3.0f, 0.0f).toNearestInt();
}

void BandStrip::resized()
{
    for (int b = 0; b < numBands; ++b)
    {
        auto c = cellBounds (b).reduced (8, 0);
        onButtons[(size_t) b].setBounds (c.removeFromBottom (30).withTrimmedBottom (8).withSizeKeepingCentre (c.getWidth(), 22));
    }
}

void BandStrip::mouseUp (const juce::MouseEvent& e)
{
    for (int b = 0; b < numBands; ++b)
        if (cellBounds (b).contains (e.getPosition()))
        {
            selected = b;
            onSelect (b);
            repaint();
            return;
        }
}

void BandStrip::paint (juce::Graphics& g)
{
    for (int b = 0; b < numBands; ++b)
    {
        const auto p = core.readBand (b);
        const auto col = Colours::band (b);
        auto r = cellBounds (b).toFloat();
        g.setColour (b == selected ? Colours::hairline : Colours::face);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (p.enabled ? col : Colours::hairline);
        g.fillRoundedRectangle (r.withHeight (3.0f), 1.5f);

        auto inner = r.reduced (8.0f, 8.0f);
        auto head = inner.removeFromTop (20.0f);
        g.setColour (p.enabled ? col : Colours::dim);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1), head.removeFromLeft (16.0f), juce::Justification::centredLeft);
        drawFilterIcon (g, head.reduced (2.0f, 2.0f), p, p.enabled ? col : Colours::dim);

        const auto textCol = p.enabled ? Colours::text : Colours::dim;
        g.setColour (textCol);
        g.setFont (mono (12.0f));
        const juce::String f = p.freq >= 1000.0f ? juce::String (p.freq / 1000.0f, 2) + " kHz" : juce::String ((int) std::round (p.freq)) + " Hz";
        g.drawText (f, inner.removeFromTop (16.0f), juce::Justification::centredLeft);
        g.setColour (p.enabled ? Colours::dim : Colours::dim.withAlpha (0.6f));
        g.setFont (mono (11.0f));
        const juce::String second = p.hasGain() ? (p.gainDb >= 0 ? "+" : "") + juce::String (p.gainDb, 1) + " dB" : juce::String (p.slopeDbPerOct) + " dB/oct";
        const bool roomy = inner.getWidth() > 150.0f;   // en la cadena (más estrecha) se omite la Q
        g.drawText (roomy ? second + "   Q " + juce::String (p.q, 2) : second, inner.removeFromTop (15.0f), juce::Justification::centredLeft);

        // Medidor dinámico (estilo "GR delta"): cuánto se está moviendo la banda dentro de su rango
        auto meter = inner.removeFromTop (14.0f).withTrimmedTop (5.0f);
        g.setColour (Colours::graphBg);
        g.fillRoundedRectangle (meter, 2.0f);
        if (p.enabled && p.isDynamic())
        {
            const float frac = juce::jlimit (0.0f, 1.0f, std::fabs (core.getDynamicGainDb (b)) / std::max (0.1f, std::fabs (p.dynRangeDb)));
            g.setColour (col);
            g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * frac), 2.0f);
        }
    }
}

// ============================================================================
// Vista completa

EqView::EqView (EqCore& c)
    : core (c),
      graph (c, [this] (int b) { selectBand (b); }),
      strip (c, [this] (int b) { selectBand (b); graph.showPopup (true); })
{
    addAndMakeVisible (graph);
    addAndMakeVisible (strip);

    output.slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    addAndMakeVisible (output);
    addAndMakeVisible (hq);
    outputAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (*core.state, core.prefix + "output", output.slider);
    hqAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (*core.state, core.prefix + "hq", hq);
    hq.setTooltip ("Procesa a 2x: evita que las campanas agudas se estrechen cerca de Nyquist");

    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (core.state->getParameter (core.prefix + "phase")))
        phase.addItemList (p->choices, 1);
    phase.setTooltip (utf8 ("Fase lineal: sin desplazamiento de fase (ideal en mastering), con más latencia. Las bandas dinámicas actúan como estáticas."));
    addAndMakeVisible (phase);
    phaseAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (*core.state, core.prefix + "phase", phase);
    phaseNote.setFont (juce::FontOptions (11.0f));
    phaseNote.setColour (juce::Label::textColourId, Colours::dim);
    phaseNote.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (phaseNote);
    selectBand (0);
    startTimerHz (15);
}

void EqView::selectBand (int b)
{
    selected = b;
    graph.setSelected (b);
    strip.setSelected (b);
}

void EqView::timerCallback()
{
    const bool linear = core.isLinearPhase();
    hq.setEnabled (! linear);
    bool anyDynamic = false;
    for (int b = 0; b < numBands; ++b) anyDynamic |= core.readBand (b).isDynamic();
    const double ms = core.getBaseRate() > 0 ? 1000.0 * core.getLatency() / core.getBaseRate() : 0.0;
    phaseNote.setText (linear ? utf8 ("Latencia ") + juce::String (ms, 0) + " ms" + (anyDynamic ? utf8 (" · bandas dinámicas como estáticas") : juce::String())
                              : utf8 ("Doble clic: crear banda · rueda: Q · clic en vacío: cerrar panel"), juce::dontSendNotification);
}

void EqView::paint (juce::Graphics&) {}

void EqView::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (30);
    hq.setBounds (header.removeFromRight (60));
    header.removeFromRight (8);
    phase.setBounds (header.removeFromRight (140));
    header.removeFromRight (16);
    phaseNote.setBounds (header.withTrimmedLeft (200));
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (118);
    r.removeFromBottom (10);
    graph.setBounds (r);
    output.setBounds (bottom.removeFromRight (96));
    bottom.removeFromRight (10);
    strip.setBounds (bottom);
}

} // namespace studioui
