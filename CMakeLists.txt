cmake_minimum_required(VERSION 3.22)

# Binario universal (Apple Silicon + Intel) por defecto en macOS
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Versión mínima de macOS")

project(PluginsVST VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PLUGINS_BUILD_PLUGINS "Compilar los plugins (requiere JUCE)" ON)
option(PLUGINS_BUILD_TESTS   "Compilar los tests del DSP"            ON)
option(PLUGINS_BUILD_CLAP    "Generar también formato CLAP"          ON)

# --- DSP compartido (C++ puro, sin JUCE) -------------------------------------
add_library(dspcore INTERFACE)
target_include_directories(dspcore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/libs/dspcore/include)
target_compile_features(dspcore INTERFACE cxx_std_17)

# --- UI compartida (requiere JUCE) -------------------------------------------
# Fuentes compiladas dentro de cada plugin que la use (JUCE compila sus módulos por target)
add_library(studioui INTERFACE)
target_include_directories(studioui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/libs/studioui/include)
target_sources(studioui INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/studioui/src/EqCore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/studioui/src/EqView.cpp)
target_link_libraries(studioui INTERFACE dspcore)

# --- Módulos de mastering (requiere JUCE) ------------------------------------
add_library(mastering INTERFACE)
target_include_directories(mastering INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/libs/mastering/include)
target_sources(mastering INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/mastering/src/Modules.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/mastering/src/Widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/mastering/src/MeterPanel.cpp)
target_link_libraries(mastering INTERFACE studioui dspcore)

# --- Tests ------------------------------------------------------------------
if(PLUGINS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Plugins ----------------------------------------------------------------
if(PLUGINS_BUILD_PLUGINS)
    include(cmake/Dependencies.cmake)

    # Tipografías incrustadas (SIL OFL): IBM Plex Sans/Mono y Barlow
    file(GLOB STUDIO_FONT_FILES ${CMAKE_CURRENT_SOURCE_DIR}/libs/studioui/fonts/*.ttf)
    juce_add_binary_data(StudioFonts HEADER_NAME StudioFontsData.h NAMESPACE StudioFontsData SOURCES ${STUDIO_FONT_FILES})
    set_target_properties(StudioFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(studiofonts INTERFACE)
    target_include_directories(studiofonts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/libs/studioui/include)
    target_sources(studiofonts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/libs/studioui/src/Fonts.cpp)
    target_link_libraries(studiofonts INTERFACE StudioFonts)
    target_link_libraries(studioui INTERFACE studiofonts)
    add_subdirectory(plugins/OptoComp)
    add_subdirectory(plugins/PrismaEQ)
    add_subdirectory(plugins/MasterChain)
    add_subdirectory(plugins/MasterModules)
    add_subdirectory(plugins/Afina)
    add_subdirectory(plugins/Furia)
endif()
