cmake_minimum_required(VERSION 3.24)

# Before project(): on Apple platforms project() creates an empty cache entry
# for the deployment target, and a set(... CACHE ...) after it never applies,
# so the plugin only loaded on the macOS it was built on (releases said minos
# 26.0). FORCE also fills that empty entry in existing build dirs; an explicit
# -DCMAKE_OSX_DEPLOYMENT_TARGET still wins. Check: otool -l <binary> | grep minos
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

# Release builds pass the tag's version, e.g. -DPSYBOX_VERSION=1.2.0.
set(PSYBOX_VERSION "0.1.0" CACHE STRING "Plugin version, MAJOR.MINOR.PATCH")
project(PsyBox VERSION ${PSYBOX_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static MSVC runtime, so the Windows plugin doesn't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(PSYBOX_BUILD_PLUGIN "Build the VST3/AU/Standalone plugin (needs JUCE)" ON)
option(PSYBOX_BUILD_TESTS "Build the DSP unit tests" ON)
option(PSYBOX_COPY_PLUGIN "Copy the built plugins into the system plug-in folders" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# Core: DSP, engine and generator. Plain C++20, no JUCE, so tests and offline
# renders build in seconds and the audio code cannot reach into GUI types.
# ---------------------------------------------------------------------------
add_library(psybox_core STATIC
    src/core/engine/Params.cpp
    src/core/engine/Engine.cpp
    src/core/engine/Voice.cpp
    src/core/engine/Arpeggiator.cpp
    src/core/engine/KickVoice.cpp
    src/core/engine/PhosVoice.cpp
    src/core/engine/PhosFx.cpp
    src/core/dsp/Wavetable.cpp
    src/core/dsp/NoiseBank.cpp
    src/core/generator/Generator.cpp
    src/core/generator/Recipes.cpp
    src/core/generator/Macros.cpp
    src/core/generator/ArpRecipes.cpp
    src/core/generator/KickRecipes.cpp
    src/core/generator/JilaxRecipes.cpp
    src/core/generator/GhostRecipes.cpp
    src/core/generator/PhosRecipes.cpp
    src/core/generator/Loudness.cpp
)
target_include_directories(psybox_core PUBLIC src/core)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(psybox_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
set_target_properties(psybox_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Generated patches are a pure function of the seed on every platform: arm64
# clang fuses a + b * c into an FMA, MSVC on x64 does not, so the last bit of a
# drawn value would differ. Keep the generator's arithmetic unfused.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        src/core/generator/Generator.cpp
        src/core/generator/Recipes.cpp
        src/core/generator/Macros.cpp
        src/core/generator/ArpRecipes.cpp
        src/core/generator/KickRecipes.cpp
        src/core/generator/JilaxRecipes.cpp
        src/core/generator/GhostRecipes.cpp
        src/core/generator/PhosRecipes.cpp
        PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(PSYBOX_BUILD_PLUGIN)
    # Point FETCHCONTENT_SOURCE_DIR_JUCE at a local checkout to skip the download.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_binary_data(psybox_assets SOURCES
        resources/fonts/Inter-Regular.ttf
        resources/fonts/Inter-Medium.ttf
        resources/fonts/Inter-SemiBold.ttf
        resources/fonts/JetBrainsMono-Regular.ttf
        resources/fonts/JetBrainsMono-Medium.ttf)

    juce_add_plugin(PsyBox
        COMPANY_NAME "Jonas Grunau"
        BUNDLE_ID "de.grunau.psybox"
        PLUGIN_MANUFACTURER_CODE Jgru
        PLUGIN_CODE Psy1
        FORMATS VST3 AU Standalone
        PRODUCT_NAME "PsyBox"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Instrument Synth
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        COPY_PLUGIN_AFTER_BUILD ${PSYBOX_COPY_PLUGIN})

    target_sources(PsyBox PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/ui/Theme.cpp
        src/ui/PsyLookAndFeel.cpp
        src/ui/Widgets.cpp
        src/ui/Panels.cpp
        src/ui/SynthPanels.cpp
        src/ui/Pages.cpp
        src/ui/FxPage.cpp
        src/ui/Displays.cpp
        src/ui/KickPanels.cpp
        src/ui/PhosPanels.cpp)

    target_include_directories(PsyBox PRIVATE src)

    target_compile_definitions(PsyBox PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(PsyBox
        PRIVATE
            psybox_core
            psybox_assets
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Dev tool: renders the editor to PNG without a host.
    juce_add_console_app(psybox_snapshot PRODUCT_NAME "psybox_snapshot")
    target_sources(psybox_snapshot PRIVATE
        tools/Snapshot.cpp
        tools/ProcessorTest.cpp
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/ui/Theme.cpp
        src/ui/PsyLookAndFeel.cpp
        src/ui/Widgets.cpp
        src/ui/Panels.cpp
        src/ui/SynthPanels.cpp
        src/ui/Pages.cpp
        src/ui/FxPage.cpp
        src/ui/Displays.cpp
        src/ui/KickPanels.cpp
        src/ui/PhosPanels.cpp)
    target_include_directories(psybox_snapshot PRIVATE src)
    target_compile_definitions(psybox_snapshot PRIVATE
        JucePlugin_Name="PsyBox"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(psybox_snapshot PRIVATE
        psybox_core psybox_assets juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(PSYBOX_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(psybox_tests
        tests/TestOscillators.cpp
        tests/TestNoiseBank.cpp
        tests/TestFilters.cpp
        tests/TestShapersAndFx.cpp
        tests/TestDynamics.cpp
        tests/TestEngine.cpp
        tests/TestGenerator.cpp
        tests/TestArp.cpp
        tests/TestKick.cpp
        tests/TestLoudness.cpp
        tests/TestPhoscyon.cpp
        tests/TestParams.cpp)
    target_link_libraries(psybox_tests PRIVATE psybox_core Catch2::Catch2WithMain)

    add_executable(psybox_render tools/RenderCategories.cpp)
    target_link_libraries(psybox_render PRIVATE psybox_core)

    include(CTest)
    include(Catch)
    catch_discover_tests(psybox_tests)
endif()
