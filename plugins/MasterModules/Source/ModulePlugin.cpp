// Punto de entrada de cada plugin suelto: el módulo se elige con MODULE_ID al compilar.
#include "ModulePlugin.h"

#ifndef MODULE_ID
 #error "Define MODULE_ID (por ejemplo \"limiter\")"
#endif

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ModulePluginProcessor (MODULE_ID); }
