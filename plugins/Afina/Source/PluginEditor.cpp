#include "PluginEditor.h"

using namespace juce;
namespace UI = studioui::Colours;   // paleta de la suite (juce::Colours es otra cosa)
using studioui::LabelledKnob;

namespace
{
    const Colour tuneAccent (0xff3fd6a8);   // verde agua: "afinado"
    const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    String noteName (int midi) { return String (names[((midi % 12) + 12) % 12]) + String (midi / 12 - 1); }
}

// ============================================================================

void NoteDisplay::timerCallback()
{
    const float f = proc.getDetectedHz(), g = proc.getCorrectedHz();
    voiced = f > 0.0f;
    if (voiced)
    {
        const float m = dspcore::hzToMidi (f);
        note = (int) std::lround (dspcore::hzToMidi (g > 0.0f ? g : f));
        inCents = (m - (float) note) * 100.0f;
        outCents = (dspcore::hzToMidi (g) - (float) note) * 100.0f;
    }
    shownIn += 0.4f * (inCents - shownIn);
    shownOut += 0.4f * (outCents - shownOut);
    repaint();
}

void NoteDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (UI::graphBg);
    g.fillRoundedRectangle (r, 8.0f);
    r.reduce (18.0f, 12.0f);

    // Nota
    auto noteArea = r.removeFromLeft (170.0f);
    g.setColour (voiced ? UI::text : UI::dim.withAlpha (0.5f));
    g.setFont (studioui::sans (64.0f, studioui::Weight::SemiBold));
    g.drawText (note >= 0 ? noteName (note) : String ("--"), noteArea.withTrimmedBottom (18.0f), Justification::centredLeft);
    g.setColour (UI::dim);
    g.setFont (studioui::mono (12.0f));
    g.drawText (voiced ? String (proc.getDetectedHz(), 1) + " Hz" : String ("sin tono"), noteArea.withTrimmedTop (noteArea.getHeight() - 20.0f),
                Justification::centredLeft);

    // Medidor de desafinación
    r.removeFromLeft (20.0f);
    auto meter = r.withTrimmedTop (r.getHeight() * 0.30f).withTrimmedBottom (r.getHeight() * 0.18f);
    auto xFor = [&] (float c) { return meter.getX() + meter.getWidth() * (jlimit (-50.0f, 50.0f, c) + 50.0f) / 100.0f; };
    g.setColour (UI::face);
    g.fillRoundedRectangle (meter, 4.0f);
    // Zona afinada (±5 cents)
    g.setColour (tuneAccent.withAlpha (0.18f));
    g.fillRect (Rectangle<float>::leftTopRightBottom (xFor (-5.0f), meter.getY(), xFor (5.0f), meter.getBottom()));
    g.setFont (FontOptions (10.0f));
    for (int c = -50; c <= 50; c += 10)
    {
        g.setColour (c == 0 ? UI::text : UI::hairline);
        g.drawVerticalLine ((int) xFor ((float) c), meter.getY() + (c % 50 == 0 ? 0.0f : meter.getHeight() * 0.5f), meter.getBottom());
        if (c % 25 == 0 || c == -50 || c == 50)
        {
            g.setColour (UI::dim);
            g.drawText ((c > 0 ? "+" : "") + String (c), Rectangle<float> (40.0f, 14.0f).withCentre ({ xFor ((float) c), meter.getBottom() + 10.0f }), Justification::centred);
        }
    }
    if (voiced)
    {
        // Entrada (aguja blanca) y salida corregida (marca de color)
        g.setColour (UI::text);
        g.fillRect (Rectangle<float> (3.0f, meter.getHeight() + 10.0f).withCentre ({ xFor (shownIn), meter.getCentreY() }));
        g.setColour (tuneAccent);
        Path tri;
        const float x = xFor (shownOut);
        tri.addTriangle (x - 8.0f, meter.getY() - 12.0f, x + 8.0f, meter.getY() - 12.0f, x, meter.getY() - 1.0f);
        g.fillPath (tri);
    }
    g.setColour (UI::dim);
    g.setFont (FontOptions (11.0f, Font::bold));
    const String label = voiced ? "ENTRADA " + String ((shownIn > 0 ? "+" : "") + String (roundToInt (shownIn))) + " c   ->   SALIDA "
                                      + String ((shownOut > 0 ? "+" : "") + String (roundToInt (shownOut))) + " c"
                                : String ("ESPERANDO VOZ");
    g.drawText (label, r.removeFromTop (r.getHeight() * 0.22f), Justification::centredLeft);
}

// ============================================================================

void PitchHistory::timerCallback()
{
    const float f = proc.getDetectedHz(), c = proc.getCorrectedHz();
    in[(size_t) pos] = f > 0.0f ? dspcore::hzToMidi (f) : -1.0f;
    out[(size_t) pos] = c > 0.0f ? dspcore::hzToMidi (c) : -1.0f;
    pos = (pos + 1) % len;
    // La vista sigue la tesitura (centro = media de lo cantado recientemente)
    float sum = 0.0f; int n = 0;
    for (float v : in) if (v > 0.0f) { sum += v; ++n; }
    if (n > 10) centreMidi += 0.08f * (sum / (float) n - centreMidi);
    repaint();
}

void PitchHistory::paint (Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (UI::graphBg);
    g.fillRoundedRectangle (area, 8.0f);
    auto r = area.reduced (8.0f).withTrimmedLeft (34.0f);
    const float span = 15.0f;   // semitonos visibles
    const float lo = std::round (centreMidi) - span * 0.5f;
    auto yFor = [&] (float m) { return r.getBottom() - r.getHeight() * (m - lo) / span; };
    const float laneH = r.getHeight() / span;
    const auto allowed = proc.currentNotes();

    g.setFont (FontOptions (10.0f));
    for (int m = (int) std::ceil (lo); m < (int) (lo + span); ++m)
    {
        const bool inScale = (allowed >> (((m % 12) + 12) % 12)) & 1;
        auto lane = Rectangle<float> (r.getX(), yFor ((float) m) - laneH * 0.5f, r.getWidth(), laneH);
        g.setColour (inScale ? UI::face.withAlpha (0.65f) : UI::graphBg);
        g.fillRect (lane.reduced (0.0f, 0.5f));
        g.setColour (inScale ? UI::text.withAlpha (0.8f) : UI::dim.withAlpha (0.45f));
        g.drawText (noteName (m), Rectangle<float> (area.getX() + 4.0f, lane.getY(), 36.0f, laneH), Justification::centredLeft);
    }

    auto xFor = [&] (int i) { return r.getX() + r.getWidth() * (float) i / (float) (len - 1); };
    auto line = [&] (const std::array<float, len>& v, Colour col, float thick)
    {
        Path p;
        bool started = false;
        for (int i = 0; i < len; ++i)
        {
            const float m = v[(size_t) ((pos + i) % len)];
            if (m <= 0.0f || m < lo - 1.0f || m > lo + span + 1.0f) { started = false; continue; }
            if (! started) { p.startNewSubPath (xFor (i), yFor (m)); started = true; }
            else p.lineTo (xFor (i), yFor (m));
        }
        g.setColour (col);
        g.strokePath (p, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
    };
    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    line (in, UI::dim.withAlpha (0.9f), 1.5f);
    line (out, tuneAccent, 2.5f);
    g.restoreState();

    g.setFont (FontOptions (10.5f));
    g.setColour (UI::dim);
    g.drawText ("voz", r.removeFromTop (16.0f).withTrimmedRight (70.0f), Justification::centredRight);
    g.setColour (tuneAccent);
    g.drawText ("corregida", Rectangle<float> (64.0f, 16.0f).withPosition (r.getRight() - 66.0f, r.getY() - 16.0f), Justification::centredRight);
}

// ============================================================================

Rectangle<float> NoteKeyboard::keyBounds (int pc) const
{
    static const int whiteIndex[] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
    auto b = getLocalBounds().toFloat().withTrimmedTop (16.0f);
    const float ww = b.getWidth() / 7.0f;
    if (! isBlack (pc))
        return { b.getX() + ww * (float) whiteIndex[pc], b.getY(), ww, b.getHeight() };
    const int leftWhite = whiteIndex[pc - 1];
    return { b.getX() + ww * (float) (leftWhite + 1) - ww * 0.32f, b.getY(), ww * 0.64f, b.getHeight() * 0.6f };
}

void NoteKeyboard::paint (Graphics& g)
{
    const auto scaleNotes = dspcore::Scale::absolute (AfinaProcessor::scaleMask ((int) proc.apvts.getRawParameterValue (AfinaIDs::scale)->load()),
                                                      (int) proc.apvts.getRawParameterValue (AfinaIDs::key)->load());
    const auto allowed = proc.currentNotes();
    g.setColour (UI::dim);
    g.setFont (FontOptions (11.0f, Font::bold));
    g.drawText ("NOTAS (clic para excluir)", getLocalBounds().removeFromTop (14), Justification::centredLeft);

    for (int pass = 0; pass < 2; ++pass)          // blancas primero, negras encima
        for (int pc = 0; pc < 12; ++pc)
        {
            if (isBlack (pc) != (pass == 1)) continue;
            auto k = keyBounds (pc).reduced (1.0f, 0.0f);
            const bool inScale = (scaleNotes >> pc) & 1, on = (allowed >> pc) & 1;
            Colour fill = isBlack (pc) ? Colour (0xff2a2d33) : Colour (0xffd9dce1);
            if (on) fill = isBlack (pc) ? tuneAccent.darker (0.5f) : tuneAccent.brighter (0.3f);
            else if (inScale) fill = fill.withMultipliedAlpha (0.6f);
            g.setColour (fill);
            g.fillRoundedRectangle (k, 3.0f);
            g.setColour (UI::graphBg);
            g.drawRoundedRectangle (k, 3.0f, 1.0f);
            g.setColour (isBlack (pc) ? UI::text.withAlpha (0.8f) : UI::graphBg);
            g.setFont (FontOptions (10.0f, Font::bold));
            g.drawText (names[pc], k.withTrimmedTop (k.getHeight() - 16.0f), Justification::centred);
            if (inScale && ! on)   // excluida a mano
            {
                g.setColour (Colour (0xffff6b6b));
                g.drawLine (k.getX() + 5, k.getY() + 5, k.getRight() - 5, k.getY() + 21, 2.0f);
                g.drawLine (k.getRight() - 5, k.getY() + 5, k.getX() + 5, k.getY() + 21, 2.0f);
            }
        }
}

void NoteKeyboard::mouseUp (const MouseEvent& e)
{
    int hit = -1;
    for (int pc = 0; pc < 12 && hit < 0; ++pc) if (isBlack (pc) && keyBounds (pc).contains (e.position)) hit = pc;
    for (int pc = 0; pc < 12 && hit < 0; ++pc) if (! isBlack (pc) && keyBounds (pc).contains (e.position)) hit = pc;
    if (hit < 0) return;
    if (auto* p = proc.apvts.getParameter (AfinaIDs::note (hit)))
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
    repaint();
}

// ============================================================================

AfinaEditor::AfinaEditor (AfinaProcessor& p) : AudioProcessorEditor (&p), proc (p), display (p), history (p), keyboard (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (display);
    addAndMakeVisible (history);
    addAndMakeVisible (keyboard);

    auto setupCombo = [this] (ComboBox& box, Label& label, const String& text, const StringArray& items, const char* id, std::unique_ptr<CA>& att)
    {
        box.addItemList (items, 1);
        label.setText (text, dontSendNotification);
        label.setFont (FontOptions (11.0f, Font::bold));
        addAndMakeVisible (box);
        addAndMakeVisible (label);
        att = std::make_unique<CA> (proc.apvts, id, box);
    };
    setupCombo (key, keyLabel, "TONALIDAD", AfinaIDs::keyNames, AfinaIDs::key, keyAtt);
    setupCombo (scale, scaleLabel, "ESCALA", AfinaIDs::scaleNames, AfinaIDs::scale, scaleAtt);
    setupCombo (range, rangeLabel, "RANGO DE VOZ", AfinaIDs::rangeNames, AfinaIDs::range, rangeAtt);
    range.setTooltip ("Define la nota más grave a detectar. Soprano tiene la latencia más baja (~24 ms)");

    for (auto* k : { &retune, &amount, &transpose })
    {
        k->slider.setColour (Slider::rotarySliderFillColourId, tuneAccent);
        addAndMakeVisible (k);
    }
    retune.slider.setTooltip (String::fromUTF8 ("0 ms: corrección instantánea (efecto robótico). 20–60 ms: natural. >100 ms: muy sutil"));
    retuneAtt = std::make_unique<SA> (proc.apvts, AfinaIDs::retune, retune.slider);
    amountAtt = std::make_unique<SA> (proc.apvts, AfinaIDs::amount, amount.slider);
    transposeAtt = std::make_unique<SA> (proc.apvts, AfinaIDs::transpose, transpose.slider);

    setSize (980, 640);
}

AfinaEditor::~AfinaEditor() { setLookAndFeel (nullptr); }

void AfinaEditor::paint (Graphics& g)
{
    g.fillAll (UI::panel);
    g.setColour (UI::text);
    g.setFont (FontOptions (21.0f, Font::bold));
    g.drawText ("AFINA", 20, 12, 120, 30, Justification::centredLeft);
    g.setColour (UI::dim);
    g.setFont (FontOptions (12.0f));
    g.drawText (String::fromUTF8 ("Corrección de afinación en tiempo real"), 104, 14, 400, 28, Justification::centredLeft);
    const double ms = proc.getSampleRate() > 0 ? 1000.0 * proc.getLatencySamples() / proc.getSampleRate() : 0.0;
    g.drawText ("Latencia " + String (roundToInt (ms)) + " ms", getWidth() - 220, 14, 200, 28, Justification::centredRight);
}

void AfinaEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 12);
    r.removeFromTop (40);
    display.setBounds (r.removeFromTop (110));
    r.removeFromTop (12);

    auto controls = r.removeFromBottom (150);
    r.removeFromBottom (12);
    history.setBounds (r);

    auto combos = controls.removeFromLeft (200);
    for (auto [label, box] : { std::pair { &keyLabel, &key }, { &scaleLabel, &scale }, { &rangeLabel, &range } })
    {
        label->setBounds (combos.removeFromTop (16));
        box->setBounds (combos.removeFromTop (28));
        combos.removeFromTop (6);
    }
    controls.removeFromLeft (20);
    for (auto* k : { &retune, &amount, &transpose })
        k->setBounds (controls.removeFromLeft (120));
    controls.removeFromLeft (20);
    keyboard.setBounds (controls.reduced (0, 10));
}
