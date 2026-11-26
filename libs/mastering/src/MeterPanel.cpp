#include <mastering/MeterPanel.h>

namespace mastering
{

using namespace studioui;

namespace
{
    juce::String lufsText (double v, int decimals = 1)
    {
        return std::isfinite (v) ? juce::String (v, decimals) : juce::String (studioui::utf8 ("—"));
    }

    /** Color según cercanía al objetivo: verde ±1 LU, ámbar ±3 LU, rojo más allá. */
    juce::Colour targetColour (double value, double target)
    {
        if (! std::isfinite (value)) return Colours::dim;
        const double d = std::fabs (value - target);
        return d <= 1.0 ? juce::Colour (0xff69db7c) : d <= 3.0 ? juce::Colour (0xffffc048) : juce::Colour (0xffff6b6b);
    }
}

MeterPanel::MeterPanel (Sources s) : src (s)
{
    history.fill (-INFINITY);
    resetButton.onClick = [this]
    {
        if (src.output) src.output->requestReset();
        if (src.input) src.input->requestReset();
        history.fill (-INFINITY);
    };
    histButton.onClick = [this] { showHistory = false; repaint(); };
    historyButton.onClick = [this] { showHistory = true; repaint(); };
    for (auto* b : { &resetButton, &histButton, &historyButton })
        addAndMakeVisible (b);
    startTimerHz (30);
}

void MeterPanel::timerCallback()
{
    if (++timerTicks % 3 == 0 && src.output)   // 10 Hz
    {
        history[(size_t) historyPos] = (float) src.output->getShortTerm();
        historyPos = (historyPos + 1) % historyLen;
    }
    histButton.setToggleState (! showHistory, juce::dontSendNotification);
    historyButton.setToggleState (showHistory, juce::dontSendNotification);
    repaint();
}

void MeterPanel::resized()
{
    auto r = getLocalBounds();
    auto bottom = r.removeFromBottom (28);
    resetButton.setBounds (bottom.removeFromRight (70));
    histButton.setBounds (bottom.removeFromLeft (100));
    bottom.removeFromLeft (6);
    historyButton.setBounds (bottom.removeFromLeft (90));
    graphArea = r.removeFromBottom (juce::jmin (220, r.getHeight() / 2)).withTrimmedBottom (8);
}

void MeterPanel::paint (juce::Graphics& g)
{
    if (src.output == nullptr) return;
    auto& m = *src.output;
    const int platformIdx = src.platform ? (int) src.platform->load() : 0;
    const double target = platformIdx > 0 ? platformTarget (platformIdx) : -14.0;
    const double I = m.getIntegrated();

    auto r = getLocalBounds().toFloat();
    r.removeFromBottom (28.0f + (float) graphArea.getHeight() + 8.0f);

    // --- Integrado (protagonista)
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("INTEGRADO (LUFS)", r.removeFromTop (16.0f), juce::Justification::centredLeft);
    g.setColour (platformIdx > 0 ? targetColour (I, target) : Colours::text);
    g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
    g.drawText (lufsText (I), r.removeFromTop (46.0f), juce::Justification::centredLeft);

    // --- Rejilla de valores
    struct Item { const char* label; juce::String value; };
    const double tp = m.getTruePeakDb();
    const std::vector<Item> items {
        { "MOMENTARY", lufsText (m.getMomentary()) },
        { "SHORT-TERM", lufsText (m.getShortTerm()) },
        { "RANGO (LRA)", juce::String (m.getLoudnessRange(), 1) + " LU" },
        { "TRUE PEAK", tp > -150.0 ? juce::String (tp, 1) + " dBTP" : juce::String (studioui::utf8 ("—")) },
        { "PLR", std::isfinite (I) ? juce::String (tp - I, 1) + " dB" : juce::String (studioui::utf8 ("—")) },
        { "MAX SHORT-TERM", lufsText (m.getMaxShortTerm()) },
    };
    const float cellW = r.getWidth() / 2.0f, cellH = 40.0f;
    for (size_t i = 0; i < items.size(); ++i)
    {
        juce::Rectangle<float> cell (r.getX() + cellW * (float) (i % 2), r.getY() + cellH * (float) (i / 2), cellW, cellH);
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (items[i].label, cell.removeFromTop (14.0f), juce::Justification::centredLeft);
        // Rojo solo si al redondear a 0.1 dB supera -1 dBTP (el objetivo habitual de streaming)
        const bool overCeiling = juce::String (items[i].label) == "TRUE PEAK" && tp > -0.95;
        g.setColour (overCeiling ? juce::Colour (0xffff6b6b) : Colours::text);
        g.setFont (juce::FontOptions (17.0f));
        g.drawText (items[i].value, cell, juce::Justification::centredLeft);
    }
    r.removeFromTop (cellH * 3.0f + 6.0f);

    // --- Ganancia de sonoridad de la cadena
    if (src.input != nullptr && std::isfinite (I) && std::isfinite (src.input->getIntegrated()))
    {
        const double delta = I - src.input->getIntegrated();
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("La cadena sube la sonoridad " + juce::String (delta, 1) + " LU", r.removeFromTop (18.0f), juce::Justification::centredLeft);
    }

    // --- Plataforma
    if (platformIdx > 0 && std::isfinite (I))
    {
        const double change = target - I;
        juce::String txt = platformNames()[platformIdx].upToFirstOccurrenceOf (" (", false, false) + ": ";
        if (change < -0.05)      txt << studioui::utf8 ("bajará tu máster ") << juce::String (-change, 1) << " dB";
        else if (change > 0.05)  txt << juce::String (change, 1) << " dB por debajo del objetivo";
        else                     txt << "en el objetivo";
        g.setColour (change < -3.0 ? juce::Colour (0xffffc048) : Colours::text);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (txt, r.removeFromTop (32.0f).toNearestInt(), juce::Justification::centredLeft, 2);
    }

    // --- Correlación
    if (src.correlation != nullptr)
    {
        auto row = r.removeFromTop (34.0f);
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (studioui::utf8 ("CORRELACIÓN"), row.removeFromTop (14.0f), juce::Justification::centredLeft);
        auto bar = row.reduced (0.0f, 4.0f);
        g.setColour (Colours::face);
        g.fillRoundedRectangle (bar, 3.0f);
        const float c = juce::jlimit (-1.0f, 1.0f, src.correlation->load());
        const float cx = bar.getCentreX(), x = bar.getX() + bar.getWidth() * (c + 1.0f) * 0.5f;
        g.setColour (c < 0.0f ? juce::Colour (0xffff6b6b) : Colours::accent);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (cx, x), bar.getY(), std::max (cx, x), bar.getBottom()));
        g.setColour (Colours::hairline);
        g.drawVerticalLine ((int) cx, bar.getY(), bar.getBottom());
    }

    // --- Gráfico
    auto ga = graphArea.toFloat();
    g.setColour (Colours::graphBg);
    g.fillRoundedRectangle (ga, 4.0f);
    if (showHistory) drawHistory (g, ga.reduced (6.0f));
    else             drawHistogram (g, ga.reduced (6.0f));
}

void MeterPanel::drawHistogram (juce::Graphics& g, juce::Rectangle<float> area)
{
    // Distribución del short-term entre -40 y 0 LUFS: un máster aplastado es un pico estrecho a la derecha
    constexpr double lo = -40.0, hi = 0.0;
    const int first = (int) ((lo - dspcore::LoudnessMeter::uiHistMin) / dspcore::LoudnessMeter::uiHistStep);
    const int last  = (int) ((hi - dspcore::LoudnessMeter::uiHistMin) / dspcore::LoudnessMeter::uiHistStep);
    std::uint32_t maxCount = 1;
    for (int b = first; b < last; ++b) maxCount = std::max (maxCount, src.output->getUiHistogramBin (b));

    auto plot = area.withTrimmedBottom (14.0f);
    const float bw = plot.getWidth() / (float) (last - first);
    for (int b = first; b < last; ++b)
    {
        const float h = plot.getHeight() * (float) src.output->getUiHistogramBin (b) / (float) maxCount;
        g.setColour (Colours::accent.withAlpha (0.8f));
        g.fillRect (plot.getX() + bw * (float) (b - first), plot.getBottom() - h, std::max (1.0f, bw - 1.0f), h);
    }
    auto xFor = [&] (double lufs) { return plot.getX() + plot.getWidth() * (float) ((lufs - lo) / (hi - lo)); };

    const double I = src.output->getIntegrated();
    if (std::isfinite (I))
    {
        g.setColour (Colours::text);
        g.drawVerticalLine ((int) xFor (I), plot.getY(), plot.getBottom());
    }
    const int platformIdx = src.platform ? (int) src.platform->load() : 0;
    if (platformIdx > 0)
    {
        g.setColour (juce::Colour (0xff69db7c));
        const float x = xFor (platformTarget (platformIdx));
        for (float y = plot.getY(); y < plot.getBottom(); y += 6.0f)
            g.drawVerticalLine ((int) x, y, std::min (y + 3.0f, plot.getBottom()));
    }
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (9.0f));
    for (int l = -40; l <= 0; l += 10)
        g.drawText (juce::String (l), juce::Rectangle<float> (xFor (l) - 14.0f, plot.getBottom() + 1.0f, 28.0f, 12.0f), juce::Justification::centred);
}

void MeterPanel::drawHistory (juce::Graphics& g, juce::Rectangle<float> area)
{
    constexpr float lo = -40.0f, hi = 0.0f;
    auto yFor = [&] (float lufs) { return area.getBottom() - area.getHeight() * (juce::jlimit (lo, hi, lufs) - lo) / (hi - lo); };

    g.setFont (juce::FontOptions (9.0f));
    for (int l = -40; l <= 0; l += 10)
    {
        g.setColour (Colours::grid);
        g.drawHorizontalLine ((int) yFor ((float) l), area.getX(), area.getRight());
        g.setColour (Colours::dim);
        g.drawText (juce::String (l), juce::Rectangle<float> (area.getX(), yFor ((float) l) - 12.0f, 26.0f, 12.0f), juce::Justification::centredLeft);
    }

    juce::Path p;
    bool started = false;
    for (int i = 0; i < historyLen; ++i)
    {
        const float v = history[(size_t) ((historyPos + i) % historyLen)];
        const float x = area.getX() + area.getWidth() * (float) i / (float) (historyLen - 1);
        if (! std::isfinite (v)) { started = false; continue; }
        if (! started) { p.startNewSubPath (x, yFor (v)); started = true; }
        else p.lineTo (x, yFor (v));
    }
    g.setColour (Colours::accent);
    g.strokePath (p, juce::PathStrokeType (1.5f));

    const double I = src.output->getIntegrated();
    if (std::isfinite (I))
    {
        g.setColour (Colours::text.withAlpha (0.6f));
        g.drawHorizontalLine ((int) yFor ((float) I), area.getX(), area.getRight());
    }
}

} // namespace mastering
