#include <studioui/Fonts.h>
#include "StudioFontsData.h"

namespace studioui
{

namespace
{
    juce::Typeface::Ptr load (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }
}

juce::Typeface::Ptr sansTypeface (Weight w)
{
    using namespace StudioFontsData;
    static const juce::Typeface::Ptr regular  = load (IBMPlexSansRegular_ttf, IBMPlexSansRegular_ttfSize);
    static const juce::Typeface::Ptr medium   = load (IBMPlexSansMedium_ttf, IBMPlexSansMedium_ttfSize);
    static const juce::Typeface::Ptr semibold = load (IBMPlexSansSemiBold_ttf, IBMPlexSansSemiBold_ttfSize);
    static const juce::Typeface::Ptr bold     = load (IBMPlexSansBold_ttf, IBMPlexSansBold_ttfSize);
    switch (w)
    {
        case Weight::Regular:  return regular;
        case Weight::Medium:   return medium;
        case Weight::SemiBold: return semibold;
        case Weight::Bold:     return bold;
    }
    return regular;
}

juce::Typeface::Ptr monoTypeface()
{
    using namespace StudioFontsData;
    static const juce::Typeface::Ptr t = load (IBMPlexMonoMedium_ttf, IBMPlexMonoMedium_ttfSize);
    return t;
}

juce::Typeface::Ptr hardwareTypeface (Weight w)
{
    using namespace StudioFontsData;
    static const juce::Typeface::Ptr medium   = load (BarlowMedium_ttf, BarlowMedium_ttfSize);
    static const juce::Typeface::Ptr semibold = load (BarlowSemiBold_ttf, BarlowSemiBold_ttfSize);
    static const juce::Typeface::Ptr bold     = load (BarlowBold_ttf, BarlowBold_ttfSize);
    switch (w)
    {
        case Weight::Regular:
        case Weight::Medium:   return medium;
        case Weight::SemiBold: return semibold;
        case Weight::Bold:     return bold;
    }
    return semibold;
}

juce::Typeface::Ptr brandTypeface()
{
    using namespace StudioFontsData;
    static const juce::Typeface::Ptr t = load (BarlowCondensedBoldItalic_ttf, BarlowCondensedBoldItalic_ttfSize);
    return t;
}

} // namespace studioui
