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

# --- Tests ------------------------------------------------------------------
if(PLUGINS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Plugins ----------------------------------------------------------------
if(PLUGINS_BUILD_PLUGINS)
    include(cmake/Dependencies.cmake)
    add_subdirectory(plugins/OptoComp)
endif()
