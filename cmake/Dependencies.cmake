# JUCE y clap-juce-extensions se descargan automáticamente con FetchContent.
# Para no volver a descargarlos, se puede apuntar a una copia local:
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/ruta/a/JUCE
#   -DFETCHCONTENT_SOURCE_DIR_CLAP_JUCE_EXTENSIONS=/ruta/a/clap-juce-extensions
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(juce)

if(PLUGINS_BUILD_CLAP)
    FetchContent_Declare(clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        3837f4a6ed2ab0ba46aba69ae5b7041c7c64ce74  # main a fecha 2026-11-26 (sin shallow: no funciona con hashes)
        GIT_SUBMODULES_RECURSE TRUE)
    FetchContent_MakeAvailable(clap_juce_extensions)
endif()

# Formatos según plataforma: AU solo existe en macOS. AAX queda fuera (no se usa Pro Tools).
set(PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND PLUGIN_FORMATS AU)
endif()
