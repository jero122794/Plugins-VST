juce_add_plugin(PrismaEQ
    PRODUCT_NAME            "PrismaEQ"
    COMPANY_NAME            "Plugins-VST"
    BUNDLE_ID               com.pluginsvst.prismaeq
    PLUGIN_MANUFACTURER_CODE Plvs
    PLUGIN_CODE             Prq1
    FORMATS                 ${PLUGIN_FORMATS}
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE            kAudioUnitType_Effect
    VST3_CATEGORIES         Fx EQ
    AAX_CATEGORY            EQ
    COPY_PLUGIN_AFTER_BUILD ${APPLE})   # en macOS se instala en ~/Library/Audio/Plug-Ins

target_sources(PrismaEQ PRIVATE
    Source/PluginProcessor.cpp)

target_compile_definitions(PrismaEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PrismaEQ
    PRIVATE
        dspcore
        studioui
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(PLUGINS_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET PrismaEQ
        CLAP_ID "com.pluginsvst.prismaeq"
        CLAP_FEATURES audio-effect equalizer mono stereo)
endif()

# --- Tests de integración del procesador --------------------------------------
# Compila el mismo código del procesador como app de consola (sin wrapper de plugin).
if(PLUGINS_BUILD_TESTS)
    juce_add_console_app(PrismaEQ_ProcessorTests PRODUCT_NAME "PrismaEQ_ProcessorTests")
    target_sources(PrismaEQ_ProcessorTests PRIVATE
        tests/ProcessorTests.cpp
        Source/PluginProcessor.cpp)
    target_compile_definitions(PrismaEQ_ProcessorTests PRIVATE
        JucePlugin_Name="PrismaEQ"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(PrismaEQ_ProcessorTests PRIVATE
        dspcore
        studioui
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME PrismaEQ_ProcessorTests COMMAND PrismaEQ_ProcessorTests)
endif()
