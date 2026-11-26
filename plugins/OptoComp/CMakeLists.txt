juce_add_plugin(OptoComp
    PRODUCT_NAME            "OptoComp"
    COMPANY_NAME            "Plugins-VST"
    BUNDLE_ID               com.pluginsvst.optocomp
    PLUGIN_MANUFACTURER_CODE Plvs
    PLUGIN_CODE             Opt2
    FORMATS                 ${PLUGIN_FORMATS}
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE            kAudioUnitType_Effect
    VST3_CATEGORIES         Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD ${APPLE})   # en macOS se instala en ~/Library/Audio/Plug-Ins

target_sources(OptoComp PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(OptoComp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(OptoComp
    PRIVATE
        dspcore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(PLUGINS_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET OptoComp
        CLAP_ID "com.pluginsvst.optocomp"
        CLAP_FEATURES audio-effect compressor mono stereo)
endif()

# --- Tests de integración del procesador --------------------------------------
# Compila el mismo código del procesador como app de consola (sin wrapper de plugin).
if(PLUGINS_BUILD_TESTS)
    juce_add_console_app(OptoComp_ProcessorTests PRODUCT_NAME "OptoComp_ProcessorTests")
    target_sources(OptoComp_ProcessorTests PRIVATE
        tests/ProcessorTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(OptoComp_ProcessorTests PRIVATE
        JucePlugin_Name="OptoComp"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(OptoComp_ProcessorTests PRIVATE
        dspcore
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME OptoComp_ProcessorTests COMMAND OptoComp_ProcessorTests)
endif()
