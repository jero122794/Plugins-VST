#pragma once

#include "ChainProcessor.h"
#include <mastering/MeterPanel.h>
#include <studioui/StudioLookAndFeel.h>

/** Fila de la lista de módulos: on/off, nombre (selecciona), medidor y flechas para reordenar. */
class ModuleRow final : public juce::Component
{
public:
    ModuleRow (MasterChainProcessor&, std::function<void (int slot)> onSelect, std::function<void()> onMoved);
    void setPosition (int position);
    void setSelected (bool s) { selected = s; repaint(); }
    void refresh();
    int getSlotIndex() const noexcept { return slot; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override { onSelect (slot); }

private:
    MasterChainProcessor& proc;
    std::function<void (int)> onSelect;
    std::function<void()> onMoved;
    int position = 0, slot = 0;
    bool selected = false;

    juce::ToggleButton on;
    juce::ArrowButton up { "up", 0.75f, studioui::Colours::dim }, down { "down", 0.25f, studioui::Colours::dim };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    juce::String meterText;
};

class MasterChainEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MasterChainEditor (MasterChainProcessor&);
    ~MasterChainEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void selectSlot (int slot);

private:
    void timerCallback() override;
    void rebuildRows();

    studioui::DefaultLookAndFeelScope defaultLnf;
    MasterChainProcessor& proc;
    studioui::StudioLookAndFeel lnf;
    std::vector<std::unique_ptr<ModuleRow>> rows;
    std::unique_ptr<juce::Component> modulePanel;
    std::unique_ptr<mastering::MeterPanel> meter;
    int selectedSlot = 0;

    juce::ToggleButton compare { "A/B igualado" }, preview { "Escuchar como la plataforma" };
    juce::ComboBox platform;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> compareAtt, previewAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> platformAtt;

    juce::Rectangle<int> panelArea, titleArea;
};
