add_studio_plugin(TARGET Furia PRODUCT "Furia" CODE Fur1 VST3_CATEGORY Dynamics
    CLAP_FEATURES compressor distortion
    SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp
    LIBS dspcore)

add_processor_tests(TARGET Furia_ProcessorTests PLUGIN_NAME "Furia"
    SOURCES tests/ProcessorTests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp
    LIBS dspcore)
