# Silt: a granular sampler, built twice from one codebase.
#   Silt     (instrument) MIDI track; records from a sidechain input
#   Silt FX  (effect)     audio track; records its own input; MIDI optional
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# Plan and decisions: ../granular-sampler-vst-research/09-implementation-plan.md
cmake_minimum_required(VERSION 3.22)
project(Silt VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0 CACHE STRING "")

# Copies each build into the user plug-in folders so DAWs see it. CI turns
# this off (on Windows it would need admin rights).
option(SILT_COPY_AFTER_BUILD "Install plug-ins into the user plug-in folders after building" ON)

include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE)
# Pinned past the 0.26.0 tag: that release doesn't build against JUCE 8.0.11+.
FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d)
FetchContent_MakeAvailable(juce clap_juce_extensions)

# Identity. PROVISIONAL until the first public build (plan D2/D3). After that
# these never change: hosts store them in every saved project.
set(SILT_COMPANY "Silt Audio")
set(SILT_MANUFACTURER_CODE SltA)
set(SILT_BUNDLE_PREFIX com.siltaudio)

# The engine: plain C++ with no JUCE dependency, so the same code builds into
# both plug-ins, the tests and the offline render tool (plan §3).
add_library(silt_core STATIC
    core/src/Engine.cpp
    core/src/GrainVoice.cpp
    core/src/LevelMeter.cpp
    core/src/SampleBuffer.cpp)
target_include_directories(silt_core PUBLIC core/include)
target_compile_options(silt_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wshadow>)
set_target_properties(silt_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(SILT_BUILD_TESTS "Build the core unit tests (fetches Catch2)" ON)
if(SILT_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.15.3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(silt_tests tests/TestDsp.cpp tests/TestEngine.cpp)
    target_link_libraries(silt_tests PRIVATE silt_core Catch2::Catch2WithMain)

    enable_testing()
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(silt_tests)
endif()

# Offline renderer: audio file + notes + settings in, WAV out.
juce_add_console_app(silt_render PRODUCT_NAME "silt-render")
target_sources(silt_render PRIVATE tools/render/Main.cpp)
target_compile_definitions(silt_render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(silt_render
    PRIVATE silt_core juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

function(silt_add_plugin target)
    cmake_parse_arguments(ARG "" "PRODUCT_NAME;PLUGIN_CODE;BUNDLE_SUFFIX;IS_SYNTH;AU_MAIN_TYPE"
        "VST3_CATEGORIES;CLAP_FEATURES" ${ARGN})

    juce_add_plugin(${target}
        COMPANY_NAME "${SILT_COMPANY}"
        BUNDLE_ID ${SILT_BUNDLE_PREFIX}.${ARG_BUNDLE_SUFFIX}
        PLUGIN_MANUFACTURER_CODE ${SILT_MANUFACTURER_CODE}
        PLUGIN_CODE ${ARG_PLUGIN_CODE}
        FORMATS AU VST3 Standalone
        PRODUCT_NAME "${ARG_PRODUCT_NAME}"
        IS_SYNTH ${ARG_IS_SYNTH}
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES ${ARG_VST3_CATEGORIES}
        AU_MAIN_TYPE ${ARG_AU_MAIN_TYPE}
        COPY_PLUGIN_AFTER_BUILD ${SILT_COPY_AFTER_BUILD})

    clap_juce_extensions_plugin(TARGET ${target}
        CLAP_ID "${SILT_BUNDLE_PREFIX}.${ARG_BUNDLE_SUFFIX}"
        CLAP_FEATURES ${ARG_CLAP_FEATURES})

    target_sources(${target} PRIVATE
        plugin/PluginProcessor.cpp
        plugin/PluginEditor.cpp)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0          # the WebView UI arrives with Spike B
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(${target}
        PRIVATE
            silt_core
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

silt_add_plugin(Silt
    PRODUCT_NAME "Silt"
    PLUGIN_CODE Slti
    BUNDLE_SUFFIX silt
    IS_SYNTH TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Sampler
    CLAP_FEATURES instrument sampler granular stereo)

silt_add_plugin(SiltFX
    PRODUCT_NAME "Silt FX"
    PLUGIN_CODE Sltf
    BUNDLE_SUFFIX siltfx
    IS_SYNTH FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    VST3_CATEGORIES Fx
    CLAP_FEATURES audio-effect granular sampler stereo)
