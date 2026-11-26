# Crea un plugin (AU/VST3/CLAP/Standalone) con la configuración común del proyecto.
#   add_studio_plugin(TARGET name PRODUCT "Nombre" CODE Abcd CLAP_FEATURES ... SOURCES ... LIBS ... DEFINITIONS ...)
function(add_studio_plugin)
    cmake_parse_arguments(P "" "TARGET;PRODUCT;CODE;VST3_CATEGORY" "CLAP_FEATURES;SOURCES;LIBS;DEFINITIONS" ${ARGN})
    string(TOLOWER "${P_TARGET}" lower)
    juce_add_plugin(${P_TARGET}
        PRODUCT_NAME            "${P_PRODUCT}"
        COMPANY_NAME            "Plugins-VST"
        BUNDLE_ID               com.pluginsvst.${lower}
        PLUGIN_MANUFACTURER_CODE Plvs
        PLUGIN_CODE             ${P_CODE}
        FORMATS                 ${PLUGIN_FORMATS}
        IS_SYNTH                FALSE
        NEEDS_MIDI_INPUT        FALSE
        NEEDS_MIDI_OUTPUT       FALSE
        IS_MIDI_EFFECT          FALSE
        AU_MAIN_TYPE            kAudioUnitType_Effect
        VST3_CATEGORIES         Fx ${P_VST3_CATEGORY}
        COPY_PLUGIN_AFTER_BUILD ${APPLE})
    target_sources(${P_TARGET} PRIVATE ${P_SOURCES})
    target_compile_definitions(${P_TARGET} PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 ${P_DEFINITIONS})
    target_link_libraries(${P_TARGET}
        PRIVATE ${P_LIBS} juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
    if(PLUGINS_BUILD_CLAP)
        clap_juce_extensions_plugin(TARGET ${P_TARGET}
            CLAP_ID "com.pluginsvst.${lower}"
            CLAP_FEATURES audio-effect ${P_CLAP_FEATURES} mono stereo)
    endif()
endfunction()

# App de consola con el mismo código del procesador, para tests de integración.
function(add_processor_tests)
    cmake_parse_arguments(T "" "TARGET;PLUGIN_NAME" "SOURCES;LIBS;DEFINITIONS" ${ARGN})
    if(NOT PLUGINS_BUILD_TESTS)
        return()
    endif()
    juce_add_console_app(${T_TARGET} PRODUCT_NAME "${T_TARGET}")
    target_sources(${T_TARGET} PRIVATE ${T_SOURCES})
    target_compile_definitions(${T_TARGET} PRIVATE
        JucePlugin_Name="${T_PLUGIN_NAME}" JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 ${T_DEFINITIONS})
    target_link_libraries(${T_TARGET} PRIVATE ${T_LIBS} juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME ${T_TARGET} COMMAND ${T_TARGET})
endfunction()
