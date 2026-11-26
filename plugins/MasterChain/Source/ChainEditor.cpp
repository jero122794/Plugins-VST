#include "ChainEditor.h"

using namespace studioui;

// ============================================================================

ModuleRow::ModuleRow (MasterChainProcessor& p, std::function<void (int)> sel, std::function<void()> moved)
    : proc (p), onSelect (std::move (sel)), onMoved (std::move (moved))
{
    name.onClick = [this] { onSelect (slot); };
    up.onClick = [this] { proc.moveModule (position, -1); onMoved(); };
    down.onClick = [this] { proc.moveModule (position, +1); onMoved(); };
    on.setTooltip ("Activar / bypass (con latencia compensada)");
    for (auto* c : std::initializer_list<juce::Component*> { &on, &name, &up, &down })
        addAndMakeVisible (c);
}

void ModuleRow::setPosition (int pos)
{
    position = pos;
    slot = proc.slotAt (pos);
    auto& s = proc.getSlot (slot);
    name.setButtonText (s.moduleInfo.name);
    name.setTooltip (s.moduleInfo.description);
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
    if (t != meterText) { meterText = t; repaint(); }
    name.setColour (juce::TextButton::textColourOffId, s.isOn() ? Colours::text : Colours::dim);
}

void ModuleRow::paint (juce::Graphics& g)
{
    g.setColour (selected ? Colours::hairline : Colours::face);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.0f, 2.0f), 5.0f);
    if (selected)
    {
        g.setColour (Colours::accent);
        g.fillRoundedRectangle (0.0f, 2.0f, 3.0f, (float) getHeight() - 4.0f, 1.5f);
    }
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (meterText, getLocalBounds().withTrimmedLeft (34).withTrimmedBottom (4), juce::Justification::bottomLeft);
}

void ModuleRow::resized()
{
    auto r = getLocalBounds().reduced (4, 4);
    on.setBounds (r.removeFromLeft (26));
    auto arrows = r.removeFromRight (26);
    up.setBounds (arrows.removeFromTop (arrows.getHeight() / 2).reduced (6, 4));
    down.setBounds (arrows.reduced (6, 4));
    name.setBounds (r.withTrimmedBottom (12));
    name.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    name.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
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
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    g.drawText ("MASTER CHAIN", titleArea.removeFromLeft (180), juce::Justification::centredLeft);

    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Latencia: " + juce::String (proc.getLatencySamples()) + " muestras", titleArea, juce::Justification::centredLeft);

    // Encabezado del módulo seleccionado
    auto& s = proc.getSlot (selectedSlot);
    auto head = panelArea.withHeight (40);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText (s.moduleInfo.name, head.removeFromTop (22), juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (s.moduleInfo.description + (s.isOn() ? juce::String() : juce::String (studioui::utf8 ("  ·  EN BYPASS"))), head, juce::Justification::centredLeft);

    // Aviso permanente si la vista previa de plataforma está activa
    if (preview.getToggleState())
    {
        auto banner = getLocalBounds().removeFromBottom (26);
        g.setColour (juce::Colour (0xffc92a2a));
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

    auto list = r.removeFromLeft (220);
    for (auto& row : rows)
        row->setBounds (list.removeFromTop (54));

    r.removeFromLeft (16);
    meter->setBounds (r.removeFromRight (310));
    r.removeFromRight (16);
    panelArea = r;
    if (modulePanel)
        modulePanel->setBounds (r.withTrimmedTop (52));
}
