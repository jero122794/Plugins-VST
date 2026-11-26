#pragma once
//
// Tipografías incrustadas en el binario (licencia SIL OFL, ver libs/studioui/fonts/):
//   - IBM Plex Sans / Plex Mono: la suite moderna (Plex Mono para valores numéricos: no "bailan")
//   - Barlow / Barlow Condensed: los frontales de hardware (OptoComp, Furia)
//
#include <juce_gui_basics/juce_gui_basics.h>

namespace studioui
{

enum class Weight { Regular, Medium, SemiBold, Bold };

juce::Typeface::Ptr sansTypeface (Weight);
juce::Typeface::Ptr monoTypeface();
juce::Typeface::Ptr hardwareTypeface (Weight);
juce::Typeface::Ptr brandTypeface();

inline juce::FontOptions sans (float height, Weight w = Weight::Regular) { return juce::FontOptions (sansTypeface (w)).withHeight (height); }
inline juce::FontOptions mono (float height)                             { return juce::FontOptions (monoTypeface()).withHeight (height); }
inline juce::FontOptions hardware (float height, Weight w = Weight::SemiBold) { return juce::FontOptions (hardwareTypeface (w)).withHeight (height); }
inline juce::FontOptions brand (float height)                            { return juce::FontOptions (brandTypeface()).withHeight (height); }

} // namespace studioui
