#include "ChainEditor.h"

using namespace studioui;

// ============================================================================

ModuleRow::ModuleRow (MasterChainProcessor& p, std::function<void (int)> sel, std::function<void()> moved)
    : proc (p), onSelect (std::move (sel)), onMoved (std::move (moved))
{
    up.onClick = [this] { proc.moveModule (position, -1); onMoved(); };
    down.onClick = [this] { proc.moveModule (position, +1); onMoved(); };
    on.setTooltip ("Activar / bypass (con latencia compensada)");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    for (auto* c : std::initializer_list<juce::Component*> { &on, &up, &down })
        addAndMakeVisible (c);
}

void ModuleRow::setPosition (int pos)
{
    position = pos;
    slot = proc.slotAt (pos);
    auto& s = proc.getSlot (slot);
    onAtt.reset();
    onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, s.moduleInfo.prefix + "on", on);
    up.setVisible (proc.isReorderable (pos) && proc.isReorderable (pos - 1));
    down.setVisible (proc.isReorderable (pos) && proc.isReorderable (pos + 1));
}

void ModuleRow::refresh()
{
    auto& s = proc.getSlot (slot);
    const float v = s.module->getMeterDb();
    const auto label = s.module->getMeterLabel();
    const juce::String t = label.isNotEmpty() && s.isOn() ? label + " " + juce::String (v, 1) : juce::String();
    if (t != meterText) { meterText = t; }
    repaint();
}

void ModuleRow::paint (juce::Graphics& g)
{
    auto& s = proc.getSlot (slot);
    const bool active = s.isOn();
    auto r = getLocalBounds().toFloat().reduced (0.0f, 2.0f);
    g.setColour (selected ? Colours::navy.brighter (0.18f) : Colours::face);
    g.fillRoundedRectangle (r, 2.0f);
    if (selected)
    {
        g.setColour (Colours::coral);
        g.fillRect (r.withWidth (3.0f));
    }

    // Número de ranura (posición en la cadena) e indicador de estado
    g.setFont (studioui::mono (11.0f));
    g.setColour (selected ? Colours::coral : (active ? Colours::sky : Colours::dim.withAlpha (0.6f)));
    g.drawText (juce::String (position + 1).paddedLeft ('0', 2), juce::Rectangle<float> (36.0f, r.getY(), 22.0f, r.getHeight()),
                juce::Justification::centredLeft);
    const juce::Rectangle<float> led (r.getRight() - 46.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
    g.setColour (active ? Colours::ocean : Colours::dim.withAlpha (0.5f));
    if (active) g.fillRect (led); else g.drawRect (led, 1.2f);

    g.setColour (active ? Colours::text : Colours::dim);
    g.setFont (studioui::sans (14.0f, studioui::Weight::SemiBold));
    auto textArea = r.withTrimmedLeft (62.0f).withTrimmedRight (52.0f);
    g.drawText (s.moduleInfo.name, textArea.withTrimmedBottom (meterText.isNotEmpty() ? 14.0f : 0.0f), juce::Justification::centredLeft);
    if (meterText.isNotEmpty())
    {
        g.setColour (Colours::coral);
        g.setFont (studioui::mono (10.5f));
        g.drawText (meterText + " dB", textArea.withTrimmedTop (textArea.getHeight() - 22.0f), juce::Justification::centredLeft);
    }
    else if (! active)
    {
        g.setColour (Colours::dim.withAlpha (0.8f));
        g.setFont (studioui::mono (9.5f));
        g.drawText ("bypass", textArea.withTrimmedTop (textArea.getHeight() - 22.0f), juce::Justification::centredLeft);
    }
}

void ModuleRow::resized()
{
    auto r = getLocalBounds().reduced (6, 6);
    on.setBounds (r.removeFromLeft (24).withSizeKeepingCentre (24, 24));
    auto arrows = r.removeFromRight (24);
    up.setBounds (arrows.removeFromTop (arrows.getHeight() / 2).reduced (5, 3));
    down.setBounds (arrows.reduced (5, 3));
}

// ============================================================================

MasterChainEditor::MasterChainEditor (MasterChainProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    mastering::MeterPanel::Sources s;
    s.output = &proc.outputMeter;
    s.input = &proc.inputMeter;
    s.correlation = &proc.correlation.value;
    s.platform = proc.apvts.getRawParameterValue (MasterChainProcessor::platformId);
    meter = std::make_unique<mastering::MeterPanel> (s);
    addAndMakeVisible (*meter);

    platform.addItemList (mastering::platformNames(), 1);
    addAndMakeVisible (platform);
    addAndMakeVisible (compare);
    addAndMakeVisible (preview);
    compare.setTooltip ("Escucha el original con la misma sonoridad que el procesado: compara el sonido, no el volumen");
    preview.setTooltip (studioui::utf8 ("Aplica la bajada de volumen que hará la plataforma. ¡Desactívalo antes de exportar!"));
    compareAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, MasterChainProcessor::compareId, compare);
    previewAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, MasterChainProcessor::previewId, preview);
    platformAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, MasterChainProcessor::platformId, platform);

    for (int pos = 0; pos < proc.getNumSlots(); ++pos)
    {
        auto row = std::make_unique<ModuleRow> (proc, [this] (int slot) { selectSlot (slot); }, [this] { rebuildRows(); });
        addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }
    rebuildRows();

    setSize (1320, 780);
    selectSlot (proc.slotAt (proc.getNumSlots() - 2));   // abre en el limitador
    startTimerHz (15);
}

MasterChainEditor::~MasterChainEditor()
{
    modulePanel.reset();
    setLookAndFeel (nullptr);
}

void MasterChainEditor::rebuildRows()
{
    for (int pos = 0; pos < (int) rows.size(); ++pos)
    {
        rows[(size_t) pos]->setPosition (pos);
        rows[(size_t) pos]->setSelected (rows[(size_t) pos]->getSlotIndex() == selectedSlot);
    }
}

void MasterChainEditor::selectSlot (int slot)
{
    selectedSlot = slot;
    modulePanel.reset();
    modulePanel = proc.getSlot (slot).module->createPanel();
    addAndMakeVisible (*modulePanel);
    rebuildRows();
    resized();
    repaint();
}

void MasterChainEditor::timerCallback()
{
    for (auto& r : rows) r->refresh();
    repaint (titleArea);
}

void MasterChainEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);

    // Cabecera: marca y regla coral
    auto brand = titleArea.toFloat();
    const auto brandFont = juce::Font (studioui::sans (20.0f, studioui::Weight::Bold));
    g.setFont (brandFont);
    g.setColour (Colours::mist);
    g.drawText ("MASTER", brand.removeFromLeft (juce::GlyphArrangement::getStringWidth (brandFont, "MASTER ") + 1.0f), juce::Justification::centredLeft);
    g.setColour (Colours::coral);
    g.drawText ("CHAIN", brand.removeFromLeft (80.0f), juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (studioui::mono (10.5f));
    g.drawText ("LAT " + juce::String (proc.getLatencySamples()) + " smp", brand.withTrimmedLeft (14.0f), juce::Justification::centredLeft);
    g.setColour (Colours::hairline);
    g.fillRect (16.0f, (float) titleArea.getBottom() + 7.0f, (float) getWidth() - 32.0f, 1.0f);
    g.setColour (Colours::coral);
    g.fillRect (16.0f, (float) titleArea.getBottom() + 6.0f, 168.0f, 2.0f);

    // Ruta de señal: la columna de números queda unida por una línea (el orden real de procesado)
    if (! rows.empty())
    {
        const float x = (float) rows.front()->getX() + 30.0f;
        g.setColour (Colours::hairline);
        g.fillRect (x, (float) rows.front()->getY() - 6.0f, 1.0f, (float) (rows.back()->getBottom() - rows.front()->getY()) + 12.0f);
        g.setColour (Colours::dim);
        g.setFont (studioui::mono (9.5f));
        g.drawText ("IN", juce::Rectangle<float> (x - 12.0f, (float) rows.front()->getY() - 22.0f, 24.0f, 12.0f), juce::Justification::centred);
        g.drawText ("OUT", juce::Rectangle<float> (x - 14.0f, (float) rows.back()->getBottom() + 8.0f, 28.0f, 12.0f), juce::Justification::centred);
        auto label = rows.back()->getBounds().translated (0, rows.back()->getHeight() + 26).withHeight (16);
        g.setFont (studioui::sans (10.5f));
        g.drawText (studioui::utf8 ("Las flechas reordenan los módulos centrales"), label, juce::Justification::centredLeft);
    }

    // Encabezado del módulo seleccionado
    auto& s = proc.getSlot (selectedSlot);
    auto head = panelArea.withHeight (40);
    int selectedPos = 0;
    for (int pos = 0; pos < proc.getNumSlots(); ++pos) if (proc.slotAt (pos) == selectedSlot) selectedPos = pos;
    auto headTop = head.removeFromTop (22);
    g.setColour (Colours::coral);
    g.setFont (studioui::mono (13.0f));
    g.drawText (juce::String (selectedPos + 1).paddedLeft ('0', 2), headTop.removeFromLeft (26), juce::Justification::centredLeft);
    g.setColour (Colours::text);
    g.setFont (studioui::sans (17.0f, studioui::Weight::SemiBold));
    g.drawText (s.moduleInfo.name, headTop, juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (s.moduleInfo.description + (s.isOn() ? juce::String() : juce::String (studioui::utf8 ("  ·  EN BYPASS"))), head, juce::Justification::centredLeft);

    // Aviso permanente si la vista previa de plataforma está activa
    if (preview.getToggleState())
    {
        auto banner = getLocalBounds().removeFromBottom (26);
        g.setColour (Colours::coral.darker (0.3f));
        g.fillRect (banner);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (studioui::utf8 ("VISTA PREVIA DE PLATAFORMA ACTIVA: el nivel de salida está reducido. Desactívala antes de exportar."),
                    banner, juce::Justification::centred);
    }
}

void MasterChainEditor::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    if (preview.getToggleState()) r.removeFromBottom (26);

    auto top = r.removeFromTop (30);
    titleArea = top;
    preview.setBounds (top.removeFromRight (230));
    top.removeFromRight (8);
    platform.setBounds (top.removeFromRight (210));
    top.removeFromRight (16);
    compare.setBounds (top.removeFromRight (140));
    titleArea = titleArea.withRight (compare.getX() - 10);
    r.removeFromTop (12);

    auto list = r.removeFromLeft (220).withTrimmedTop (18);
    for (auto& row : rows)
        row->setBounds (list.removeFromTop (54));

    r.removeFromLeft (16);
    meter->setBounds (r.removeFromRight (310));
    r.removeFromRight (16);
    panelArea = r;
    if (modulePanel)
        modulePanel->setBounds (r.withTrimmedTop (52));
}
