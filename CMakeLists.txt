cmake_minimum_required(VERSION 3.24)

project(PsySynth VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PSYSYNTH_BUILD_PLUGIN "Build the VST3/AU/Standalone plugin (needs JUCE)" ON)
option(PSYSYNTH_BUILD_TESTS "Build the DSP unit tests" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# Core: DSP, engine and generator. Plain C++20, no JUCE, so tests and offline
# renders build in seconds and the audio code cannot reach into GUI types.
# ---------------------------------------------------------------------------
add_library(psysynth_core STATIC
    src/core/engine/Params.cpp
    src/core/engine/Engine.cpp
    src/core/engine/Voice.cpp
    src/core/engine/Arpeggiator.cpp
    src/core/engine/KickVoice.cpp
    src/core/dsp/Wavetable.cpp
    src/core/generator/Generator.cpp
    src/core/generator/Recipes.cpp
    src/core/generator/ArpRecipes.cpp
    src/core/generator/KickRecipes.cpp
)
target_include_directories(psysynth_core PUBLIC src/core)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(psysynth_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
set_target_properties(psysynth_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(PSYSYNTH_BUILD_PLUGIN)
    # Point FETCHCONTENT_SOURCE_DIR_JUCE at a local checkout to skip the download.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_binary_data(psysynth_assets SOURCES
        resources/fonts/Inter-Regular.ttf
        resources/fonts/Inter-Medium.ttf
        resources/fonts/Inter-SemiBold.ttf
        resources/fonts/JetBrainsMono-Regular.ttf
        resources/fonts/JetBrainsMono-Medium.ttf)

    juce_add_plugin(PsySynth
        COMPANY_NAME "Jonas Grunau"
        BUNDLE_ID "de.grunau.psysynth"
        PLUGIN_MANUFACTURER_CODE Jgru
        PLUGIN_CODE Psy1
        FORMATS VST3 AU Standalone
        PRODUCT_NAME "PsySynth"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Instrument Synth
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        COPY_PLUGIN_AFTER_BUILD TRUE)

    target_sources(PsySynth PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/ui/Theme.cpp
        src/ui/PsyLookAndFeel.cpp
        src/ui/Widgets.cpp
        src/ui/Panels.cpp
        src/ui/Displays.cpp
        src/ui/KickPanels.cpp)

    target_include_directories(PsySynth PRIVATE src)

    target_compile_definitions(PsySynth PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(PsySynth
        PRIVATE
            psysynth_core
            psysynth_assets
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Dev tool: renders the editor to PNG without a host.
    juce_add_console_app(psysynth_snapshot PRODUCT_NAME "psysynth_snapshot")
    target_sources(psysynth_snapshot PRIVATE
        tools/Snapshot.cpp
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/ui/Theme.cpp
        src/ui/PsyLookAndFeel.cpp
        src/ui/Widgets.cpp
        src/ui/Panels.cpp
        src/ui/Displays.cpp
        src/ui/KickPanels.cpp)
    target_include_directories(psysynth_snapshot PRIVATE src)
    target_compile_definitions(psysynth_snapshot PRIVATE
        JucePlugin_Name="PsySynth"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(psysynth_snapshot PRIVATE
        psysynth_core psysynth_assets juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(PSYSYNTH_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(psysynth_tests
        tests/TestOscillators.cpp
        tests/TestFilters.cpp
        tests/TestShapersAndFx.cpp
        tests/TestDynamics.cpp
        tests/TestEngine.cpp
        tests/TestGenerator.cpp
        tests/TestArp.cpp
        tests/TestKick.cpp)
    target_link_libraries(psysynth_tests PRIVATE psysynth_core Catch2::Catch2WithMain)

    add_executable(psysynth_render tools/RenderCategories.cpp)
    target_link_libraries(psysynth_render PRIVATE psysynth_core)

    include(CTest)
    include(Catch)
    catch_discover_tests(psysynth_tests)
endif()
