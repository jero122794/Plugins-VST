# Cada módulo de mastering también como plugin suelto (mismo código que en MasterChain).
function(add_module_plugin target product code id category)
    add_studio_plugin(TARGET ${target} PRODUCT "${product}" CODE ${code} VST3_CATEGORY ${category}
        CLAP_FEATURES ${ARGN}
        SOURCES Source/ModulePlugin.cpp
        LIBS mastering
        DEFINITIONS MODULE_ID="${id}")
endfunction()

add_module_plugin(MCLimiter   "MC Limiter"   Mlm1 limiter Dynamics  limiter mastering)
add_module_plugin(MCBusComp   "MC BusComp"   Mbc1 comp    Dynamics  compressor)
add_module_plugin(MCSaturator "MC Saturator" Mst1 sat     Distortion distortion)
add_module_plugin(MCImager    "MC Imager"    Mim1 imager  Spatial   utility)
add_module_plugin(MCClipper   "MC Clipper"   Mcl1 clipper Dynamics  distortion)
add_module_plugin(MCMeter     "MC Meter"     Mmt1 meter   Analyzer  analyzer)

# Galería: captura de cada plugin suelto con audio de prueba (para revisar la UI sin DAW)
add_processor_tests(TARGET MasterModules_Gallery PLUGIN_NAME "Gallery"
    SOURCES tests/Gallery.cpp
    LIBS mastering)
