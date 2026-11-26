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
    : core (c), onSelect (std::move (cb))
{
    startTimerHz (30);
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

    // --- Analizador: pre (relleno tenue) y post (línea)
    if (pre.active)
    {
        g.setColour (Colours::dim.withAlpha (0.12f));
        g.fillPath (analyzerPath (pre, true));
    }
    if (post.active)
    {
        g.setColour (Colours::accent.withAlpha (0.45f));
        g.strokePath (analyzerPath (post, false), juce::PathStrokeType (1.0f));
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

        const float r = b == selected ? 8.0f : (b == hovered ? 7.0f : 6.0f);
        g.setColour (col);
        g.fillEllipse (pos.x - r, pos.y - r, 2 * r, 2 * r);
        g.setColour (Colours::graphBg);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (pos.x - r, pos.y - r, 2 * r, 2 * r), juce::Justification::centred);
        if (b == selected)
        {
            g.setColour (Colours::text);
            g.drawEllipse (pos.x - r - 3, pos.y - r - 3, 2 * r + 6, 2 * r + 6, 1.5f);

            juce::String info = p.freq >= 1000.0f ? juce::String (p.freq / 1000.0f, 2) + " kHz" : juce::String ((int) std::round (p.freq)) + " Hz";
            if (p.hasGain()) info << "   " << (p.gainDb >= 0 ? "+" : "") << juce::String (p.gainDb, 1) << " dB";
            info << "   Q " << juce::String (p.q, 2);
            g.setFont (juce::FontOptions (12.0f));
            const float w = 190.0f;
            const float tx = juce::jlimit (plot.getX() + 4.0f, plot.getRight() - w - 4.0f, pos.x - w * 0.5f);
            const float ty = pos.y > plot.getY() + 40.0f ? pos.y - 34.0f : pos.y + 16.0f;
            g.setColour (Colours::face.withAlpha (0.9f));
            g.fillRoundedRectangle (tx, ty, w, 20.0f, 4.0f);
            g.setColour (Colours::text);
            g.drawText (info, juce::Rectangle<float> (tx, ty, w, 20.0f), juce::Justification::centred);
        }
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
    if (hit < 0) return;
    selected = hit;
    onSelect (hit);
    dragging = hit;
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

void EqGraph::mouseMove (const juce::MouseEvent& e)
{
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
    g.setColour (Colours::band (band));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("BANDA " + juce::String (band + 1), getLocalBounds().removeFromTop (24).removeFromLeft (110), juce::Justification::centredLeft);
}

void BandPanel::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (170);
    left.removeFromTop (28);
    on.setBounds (left.removeFromTop (24));
    left.removeFromTop (4);
    type.setBounds (left.removeFromTop (24));
    left.removeFromTop (6);
    slope.setBounds (left.removeFromTop (24));
    left.removeFromTop (6);
    placement.setBounds (left.removeFromTop (24));

    r.removeFromLeft (16);
    const int w = r.getWidth() / 7;
    for (auto* k : { &freq, &gain, &q, &dynRange, &threshold, &attack, &release })
        k->setBounds (r.removeFromLeft (w).withTrimmedTop (16));
}

// ============================================================================
// Vista completa

EqView::EqView (EqCore& c)
    : core (c),
      graph (c, [this] (int b) { selectBand (b); }),
      panel (c)
{
    addAndMakeVisible (graph);
    addAndMakeVisible (panel);
    for (int b = 0; b < numBands; ++b)
    {
        auto& btn = bandButtons[(size_t) b];
        btn.setButtonText (juce::String (b + 1));
        btn.onClick = [this, b] { selectBand (b); };
        addAndMakeVisible (btn);
    }
    output.slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    addAndMakeVisible (output);
    addAndMakeVisible (hq);
    outputAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (*core.state, core.prefix + "output", output.slider);
    hqAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (*core.state, core.prefix + "hq", hq);
    hq.setTooltip ("Procesa a 2x: evita que las campanas agudas se estrechen cerca de Nyquist");
    selectBand (0);
    startTimerHz (15);
}

void EqView::selectBand (int b)
{
    selected = b;
    graph.setSelected (b);
    panel.showBand (b);
    timerCallback();
}

void EqView::timerCallback()
{
    for (int b = 0; b < numBands; ++b)
    {
        auto& btn = bandButtons[(size_t) b];
        const bool enabled = core.readBand (b).enabled;
        const auto col = Colours::band (b);
        btn.setColour (juce::TextButton::buttonColourId, enabled ? col.withAlpha (b == selected ? 0.9f : 0.45f) : Colours::face);
        btn.setColour (juce::TextButton::textColourOffId, enabled ? Colours::graphBg : (b == selected ? col : Colours::dim));
    }
    panel.refreshEnablement();
}

void EqView::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (28);
    hq.setBounds (header.removeFromRight (60));
    header.removeFromRight (16);
    for (int b = numBands - 1; b >= 0; --b)
    {
        bandButtons[(size_t) b].setBounds (header.removeFromRight (30));
        header.removeFromRight (4);
    }
    r.removeFromTop (10);
    auto bottom = r.removeFromBottom (160);
    graph.setBounds (r);
    bottom.removeFromTop (12);
    output.setBounds (bottom.removeFromRight (90).withTrimmedTop (16));
    bottom.removeFromRight (12);
    panel.setBounds (bottom);
}

} // namespace studioui
