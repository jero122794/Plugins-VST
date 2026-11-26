#include "PluginEditor.h"

using namespace juce;
using namespace FuriaIDs;

namespace
{
    const Colour faceTop (0xff1b1c1e), faceBottom (0xff0f1011), printWhite (0xffeeeeea), printDim (0xff9a9c9f);
    const Colour ledGreen (0xff3dff6e), ledYellow (0xffffd23d), ledRed (0xffff3b30), ledBlue (0xff4fb8ff);
    constexpr float sweep = MathConstants<float>::pi * 5.0f / 3.0f;   // 300° de recorrido

    Point<float> polar (Point<float> c, float r, float a) { return { c.x + r * std::sin (a), c.y - r * std::cos (a) }; }

    void drawLed (Graphics& g, Point<float> c, float r, Colour col, bool on)
    {
        if (on)
        {
            g.setColour (col.withAlpha (0.28f));
            g.fillEllipse (c.x - r * 2.4f, c.y - r * 2.4f, r * 4.8f, r * 4.8f);
        }
        g.setColour (Colours::black);
        g.fillEllipse (c.x - r - 1.5f, c.y - r - 1.5f, 2 * r + 3, 2 * r + 3);
        g.setGradientFill (ColourGradient (on ? col.brighter (0.7f) : col.darker (2.2f), c.x - r * 0.4f, c.y - r * 0.5f,
                                           on ? col : col.darker (3.0f), c.x + r, c.y + r, true));
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    }

    void printText (Graphics& g, const String& t, Rectangle<float> r, float size, Colour c = printWhite,
                    Justification j = Justification::centred, bool bold = true, bool italic = false)
    {
        g.setColour (c);
        int style = bold ? Font::bold : Font::plain;
        if (italic) style |= Font::italic;
        g.setFont (FontOptions (size, style));
        g.drawText (t, r, j);
    }

    void screw (Graphics& g, Point<float> c)
    {
        g.setGradientFill (ColourGradient (Colour (0xff5c5d60), c.x - 5, c.y - 5, Colour (0xff1c1c1e), c.x + 5, c.y + 5, false));
        g.fillEllipse (c.x - 5, c.y - 5, 10, 10);
        g.setColour (Colours::black.withAlpha (0.7f));
        g.drawLine (c.x - 3.5f, c.y + 1.5f, c.x + 3.5f, c.y - 1.5f, 1.3f);
    }
}

// ============================================================================

void FuriaKnobLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider& s)
{
    const auto b = Rectangle<int> (x, y, w, h).toFloat();
    const auto c = b.getCentre();
    const float R = std::min (b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
    const bool small = (bool) s.getProperties()["small"];

    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - R + 3, c.y - R + 6, 2 * R, 2 * R);

    if (small)   // perilla pequeña negra (Mix)
    {
        g.setGradientFill (ColourGradient (Colour (0xff4a4b4e), c.x, c.y - R, Colour (0xff0b0b0c), c.x, c.y + R, false));
        g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
        const float a = -sweep * 0.5f + pos * sweep;
        g.setColour (printWhite);
        g.drawLine (Line<float> (polar (c, R * 0.25f, a), polar (c, R * 0.92f, a)), 2.5f);
        return;
    }

    // Faldón plateado con la escala impresa: el número que queda bajo el índice superior es el valor
    g.setGradientFill (ColourGradient (Colour (0xfff4f4f4), c.x - R * 0.5f, c.y - R, Colour (0xff8d9094), c.x + R * 0.6f, c.y + R, true));
    g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.drawEllipse (c.x - R, c.y - R, 2 * R, 2 * R, 1.2f);

    const double lo = s.getMinimum(), hi = s.getMaximum();
    const double value = s.getValue();
    const int first = (int) std::ceil (lo), last = (int) std::floor (hi);
    g.setFont (FontOptions (R * 0.19f, Font::bold));
    for (int i = first; i <= last; ++i)
    {
        const float a = (float) ((double) i - value) / 10.0f * sweep;   // relativo al índice (arriba)
        if (std::fabs (a) > MathConstants<float>::pi * 0.92f) continue;
        g.setColour (Colour (0xff202124));
        g.drawLine (Line<float> (polar (c, R * 0.93f, a), polar (c, R * 0.82f, a)), 1.6f);
        const auto tp = polar (c, R * 0.68f, a);
        g.drawText (String (i), Rectangle<float> (R * 0.3f, R * 0.22f).withCentre (tp), Justification::centred);
        // medias marcas
        const float am = a + sweep / 20.0f;
        if (i < last) g.drawLine (Line<float> (polar (c, R * 0.93f, am), polar (c, R * 0.87f, am)), 1.0f);
    }

    // Centro moleteado oscuro
    const float r = R * 0.42f;
    g.setGradientFill (ColourGradient (Colour (0xffb9bcc0), c.x - r, c.y - r, Colour (0xff3d3f42), c.x + r, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    for (int i = 0; i < 40; ++i)
    {
        const float a = MathConstants<float>::twoPi * (float) i / 40.0f;
        g.setColour (Colours::black.withAlpha (0.25f));
        g.drawLine (Line<float> (polar (c, r * 0.8f, a), polar (c, r, a)), 1.0f);
    }
    juce::ignoreUnused (pos);
}

void SilverButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (Colours::black);
    g.fillRoundedRectangle (b.expanded (2.0f), 4.0f);
    g.setGradientFill (ColourGradient (Colour (down ? 0xff9ea1a5 : 0xffe9eaec), b.getX(), b.getY(),
                                       Colour (down ? 0xff6c6f73 : 0xff8f9296), b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (Colours::white.withAlpha (highlighted ? 0.5f : 0.3f));
    g.drawHorizontalLine ((int) b.getY() + 1, b.getX() + 3, b.getRight() - 3);
}

// ============================================================================

FuriaEditor::FuriaEditor (FuriaProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    auto setupKnob = [this] (Slider& s, const char* id, std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment>& att, bool small)
    {
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setLookAndFeel (&knobLnf);
        s.getProperties().set ("small", small);
        s.setMouseDragSensitivity (260);
        addAndMakeVisible (s);
        att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, s);
    };
    setupKnob (input, FuriaIDs::input, inputAtt, false);
    setupKnob (attack, FuriaIDs::attack, attackAtt, false);
    setupKnob (release, FuriaIDs::release, releaseAtt, false);
    setupKnob (output, FuriaIDs::output, outputAtt, false);
    setupKnob (mix, FuriaIDs::mix, mixAtt, true);
    input.setDoubleClickReturnValue (true, 3.0);
    output.setDoubleClickReturnValue (true, 5.0);

    // Botones como en el hardware: recorren los estados
    ratioButton.onClick = [this] { setParam (FuriaIDs::ratio, (float) (((int) param (FuriaIDs::ratio) + 1) % 9)); };
    detectorButton.onClick = [this]
    {
        // ninguno → HP → Band → HP+Band
        const int state = (param (detHp) > 0.5f ? 1 : 0) + (param (detBand) > 0.5f ? 2 : 0);
        const int next = (state + 1) % 4;
        setParam (detHp, (float) (next & 1));
        setParam (detBand, (float) ((next >> 1) & 1));
    };
    audioButton.onClick = [this]
    {
        // ninguno → Dist 2 → Dist 3 → HP → HP+Dist 2 → HP+Dist 3
        const int state = (param (audioHp) > 0.5f ? 3 : 0) + (int) param (dist);
        const int next = (state + 1) % 6;
        setParam (audioHp, next >= 3 ? 1.0f : 0.0f);
        setParam (dist, (float) (next % 3));
    };
    for (auto* b : { &ratioButton, &detectorButton, &audioButton }) addAndMakeVisible (b);
    powerSwitch.setAlpha (0.0f);   // invisible: se dibuja el balancín en paint()
    addAndMakeVisible (powerSwitch);
    powerAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, power, powerSwitch);

    setSize (1100, 420);
    startTimerHz (30);
}

FuriaEditor::~FuriaEditor()
{
    for (auto* s : { &input, &attack, &release, &output, &mix }) s->setLookAndFeel (nullptr);
}

void FuriaEditor::setParam (const char* id, float plain)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

void FuriaEditor::timerCallback()
{
    const float gr = proc.getGainReductionDb();
    shownGr = gr > shownGr ? gr : shownGr * 0.85f;   // LEDs: suben al instante, caen rápido
    if (gr >= peakGr) { peakGr = gr; peakHoldTicks = 30; } else if (--peakHoldTicks < 0) peakGr *= 0.9f;
    // Los pilotos se mantienen encendidos un instante para que se vean
    if (proc.isRedline()) { redlineHold = true; redlineTicks = 12; } else if (--redlineTicks < 0) redlineHold = false;
    if (proc.isThdLit())  { thdHold = true; thdTicks = 12; } else if (--thdTicks < 0) thdHold = false;
    repaint();
}

void FuriaEditor::mouseUp (const MouseEvent& e)
{
    const auto p = e.position;
    for (int i = 0; i < 9; ++i) if (ratioLeds[(size_t) i].contains (p)) { setParam (ratio, (float) i); return; }
    const char* det[] = { detHp, detBand, stereoLink };
    for (int i = 0; i < 3; ++i) if (detectorLeds[(size_t) i].contains (p)) { setParam (det[i], param (det[i]) > 0.5f ? 0.0f : 1.0f); return; }
    if (audioLeds[0].contains (p)) { setParam (audioHp, param (audioHp) > 0.5f ? 0.0f : 1.0f); return; }
    if (audioLeds[1].contains (p)) { setParam (dist, (int) param (dist) == 1 ? 0.0f : 1.0f); return; }
    if (audioLeds[2].contains (p)) { setParam (dist, (int) param (dist) == 2 ? 0.0f : 2.0f); return; }
}

void FuriaEditor::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (faceTop, 0, 0, faceBottom, 0, bounds.getBottom(), false));
    g.fillAll();
    for (auto pt : { Point<float> (16, 16), Point<float> (bounds.getRight() - 16, 16),
                     Point<float> (16, bounds.getBottom() - 16), Point<float> (bounds.getRight() - 16, bounds.getBottom() - 16) })
        screw (g, pt);
    const bool on = param (power) > 0.5f;

    // --- Marca
    printText (g, "FURIA", { 40, 34, 200, 48 }, 40.0f, printWhite, Justification::centredLeft, true, true);
    printText (g, "FX-8  COMPRESSOR", { 42, 80, 220, 18 }, 11.0f, printDim, Justification::centredLeft);

    // --- Escalera de GR (1 dB a la derecha, 26 dB a la izquierda)
    static constexpr int steps[] = { 26, 23, 20, 17, 14, 12, 10, 8, 6, 4, 3, 2, 1 };
    const float lx = grArea.getX(), lw = grArea.getWidth() / 13.0f, ly = grArea.getY() + 30.0f;
    for (int i = 0; i < 13; ++i)
    {
        const float cx = lx + lw * ((float) i + 0.5f);
        const int db = steps[i];
        const Colour col = db >= 14 ? ledRed : (db >= 6 ? ledYellow : ledGreen);
        printText (g, String (db), { cx - 14, grArea.getY(), 28, 16 }, 11.0f, printDim);
        const bool lit = on && (shownGr >= (float) db - 0.5f || std::fabs (peakGr - (float) db) < 0.5f);
        drawLed (g, { cx, ly }, 5.5f, col, lit);
    }
    printText (g, "GAIN REDUCTION", { lx, ly + 12, grArea.getWidth(), 16 }, 11.0f, printDim);

    // --- Pilotos
    drawLed (g, { lampArea.getX() + 8, lampArea.getY() + 8 }, 5.5f, ledRed, on && redlineHold);
    printText (g, "REDLINE", { lampArea.getX() + 20, lampArea.getY(), 80, 16 }, 10.5f, printWhite, Justification::centredLeft);
    drawLed (g, { lampArea.getX() + 8, lampArea.getY() + 34 }, 5.5f, ledYellow, on && thdHold);
    printText (g, "1% THD", { lampArea.getX() + 20, lampArea.getY() + 26, 80, 16 }, 10.5f, printWhite, Justification::centredLeft);

    // --- Ratio
    const int r = (int) param (ratio);
    for (int i = 0; i < 9; ++i)
    {
        const auto led = ratioLeds[(size_t) i];
        const Colour col = i == 7 ? ledBlue : (i == 8 ? ledYellow : ledRed);
        drawLed (g, led.getCentre().translated (0, -8), 5.0f, col, on && r == i);
        printText (g, ratioNames[i].toUpperCase(), { led.getX() - 6, led.getBottom() - 12, led.getWidth() + 12, 14 }, 10.5f,
                   i == 7 ? ledBlue.brighter (0.3f) : printWhite);
    }
    auto rb = ratioButton.getBounds().toFloat();
    printText (g, "RATIO", { rb.getX() - 10, rb.getBottom() + 4, rb.getWidth() + 20, 14 }, 11.0f);

    // --- Detector / Audio
    auto block = [&] (const std::array<Rectangle<float>, 3>& leds, const char* labels[3], std::array<bool, 3> states, Colour col,
                      SilverButton& button, const char* title)
    {
        for (int i = 0; i < 3; ++i)
        {
            drawLed (g, leds[(size_t) i].getCentre().withX (leds[(size_t) i].getRight() - 8), 5.0f, i == 2 && col == ledRed ? ledRed : col,
                     on && states[(size_t) i]);
            printText (g, labels[i], leds[(size_t) i].withTrimmedRight (26), 10.5f, printWhite, Justification::centredRight);
        }
        auto bb = button.getBounds().toFloat();
        printText (g, title, { bb.getX() - 20, bb.getBottom() + 4, bb.getWidth() + 40, 14 }, 11.0f);
    };
    const char* detLabels[] = { "HP", "BAND", "LINK" };
    block (detectorLeds, detLabels, { param (detHp) > 0.5f, param (detBand) > 0.5f, param (stereoLink) > 0.5f }, ledGreen, detectorButton, "DETECTOR");
    const char* audLabels[] = { "HP", "DIST 2", "DIST 3" };
    block (audioLeds, audLabels, { param (audioHp) > 0.5f, (int) param (dist) == 1, (int) param (dist) == 2 }, ledYellow, audioButton, "AUDIO");

    // --- Interruptor de encendido (balancín)
    printText (g, "POWER", { powerArea.getX() - 10, powerArea.getY() - 20, powerArea.getWidth() + 20, 14 }, 11.0f);
    g.setColour (Colours::black);
    g.fillRoundedRectangle (powerArea.expanded (3), 4.0f);
    auto top = powerArea.withHeight (powerArea.getHeight() * 0.5f), bottom = powerArea.withTrimmedTop (powerArea.getHeight() * 0.5f);
    g.setGradientFill (ColourGradient (Colour (on ? 0xff3a3b3e : 0xff1a1b1d), top.getX(), top.getY(), Colour (0xff151517), top.getX(), top.getBottom(), false));
    g.fillRect (on ? top : bottom);
    g.setColour (Colour (0xff0a0a0b));
    g.fillRect (on ? bottom : top);
    printText (g, "I", top, 12.0f, on ? printWhite : printDim);
    printText (g, "O", bottom, 12.0f, on ? printDim : printWhite);

    // --- Banda de perillas: marco y rótulos
    const float bandY = 228.0f;
    g.setColour (printWhite.withAlpha (0.85f));
    g.drawRoundedRectangle ({ 30, bandY, bounds.getWidth() - 60, bounds.getHeight() - bandY - 24 }, 10.0f, 1.6f);
    for (auto [s, name] : { std::pair<Slider*, const char*> { &input, "INPUT" }, { &attack, "ATTACK" }, { &release, "RELEASE" }, { &output, "OUTPUT" }, { &mix, "MIX" } })
    {
        auto sb = s->getBounds().toFloat();
        auto label = Rectangle<float> (110, 18).withCentre ({ sb.getCentreX(), bandY });
        g.setColour (faceBottom);
        g.fillRect (label);
        printText (g, name, label, 13.0f);
        if (s != &mix)
        {
            // Índice fijo sobre la perilla
            Path tri;
            const float cx = sb.getCentreX(), ty = sb.getY() - 4;
            tri.addTriangle (cx - 6, ty - 8, cx + 6, ty - 8, cx, ty);
            g.setColour (printWhite);
            g.fillPath (tri);
        }
    }
    auto mb = mix.getBounds().toFloat();
    printText (g, "Dry", { mb.getX() - 34, mb.getBottom() - 10, 40, 14 }, 10.5f, printDim, Justification::centredRight, false);
    printText (g, "Comp", { mb.getRight() - 6, mb.getBottom() - 10, 44, 14 }, 10.5f, printDim, Justification::centredLeft, false);
    printText (g, "Opto (10)", { release.getRight() - 30.0f, release.getBottom() - 8.0f, 70, 14 }, 9.5f, printDim, Justification::centredLeft, false);
    juce::ignoreUnused (r);
}

void FuriaEditor::resized()
{
    grArea = { 290, 30, 470, 60 };
    lampArea = { 790, 36, 100, 50 };
    for (int i = 0; i < 9; ++i)
        ratioLeds[(size_t) i] = Rectangle<float> (290 + 52.0f * (float) i, 120, 44, 40);
    ratioButton.setBounds (180, 128, 60, 26);
    for (int i = 0; i < 3; ++i)
    {
        detectorLeds[(size_t) i] = Rectangle<float> (890, 28 + 22.0f * (float) i, 84, 18);
        audioLeds[(size_t) i] = Rectangle<float> (984, 28 + 22.0f * (float) i, 86, 18);
    }
    detectorButton.setBounds (918, 128, 56, 26);
    audioButton.setBounds (1000, 128, 56, 26);
    powerArea = { 820, 118, 34, 50 };
    powerSwitch.setBounds (powerArea.toNearestInt());

    const int cy = 320, size = 136;
    input.setBounds (Rectangle<int> (size, size).withCentre ({ 150, cy }));
    attack.setBounds (Rectangle<int> (size, size).withCentre ({ 370, cy }));
    release.setBounds (Rectangle<int> (size, size).withCentre ({ 590, cy }));
    output.setBounds (Rectangle<int> (size, size).withCentre ({ 810, cy }));
    mix.setBounds (Rectangle<int> (62, 62).withCentre ({ 985, cy + 10 }));
}
