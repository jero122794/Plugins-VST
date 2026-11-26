add_studio_plugin(TARGET Afina PRODUCT "Afina" CODE Afn1 VST3_CATEGORY "Pitch Shift"
    CLAP_FEATURES pitch-correction
    SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp
    LIBS dspcore)
target_include_directories(Afina PRIVATE ${CMAKE_SOURCE_DIR}/libs/studioui/include)

add_processor_tests(TARGET Afina_ProcessorTests PLUGIN_NAME "Afina"
    SOURCES tests/ProcessorTests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp
    LIBS dspcore)
if(TARGET Afina_ProcessorTests)
    target_include_directories(Afina_ProcessorTests PRIVATE ${CMAKE_SOURCE_DIR}/libs/studioui/include)
endif()
