# Beatsmith - build configuration
#
# Targets:
#   beatsmith_engine        the JUCE-free sequencer engine (static library)
#   beatsmith_engine_tests  engine tests, no JUCE needed           (BEATSMITH_BUILD_ENGINE_TESTS)
#   Beatsmith_VST3          the plug-in                             (BEATSMITH_BUILD_PLUGIN)
#   beatsmith_plugin_tests  tests of the JUCE layer and the editor  (BEATSMITH_BUILD_PLUGIN_TESTS)
#
# See CONTRIBUTING.md for the commands CI runs.

cmake_minimum_required(VERSION 3.28)

project(Beatsmith VERSION 0.0.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clang-tidy and IDEs

option(BEATSMITH_BUILD_PLUGIN "Build the VST3 plug-in (downloads JUCE)" ON)
option(BEATSMITH_BUILD_ENGINE_TESTS "Build the engine tests" ON)
option(BEATSMITH_BUILD_PLUGIN_TESTS "Build the plug-in layer tests (needs BEATSMITH_BUILD_PLUGIN)" OFF)
option(BEATSMITH_WARNINGS_AS_ERRORS "Treat compiler warnings in our own code as errors" OFF)
option(BEATSMITH_SANITIZERS "Build the engine and its tests with address and undefined-behaviour sanitizers" OFF)

# Static MSVC runtime, so the plug-in does not depend on an installed VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

#===============================================================================
# Shared compiler settings for our own code (not for JUCE or other dependencies)

add_library(beatsmith_warnings INTERFACE)
if(MSVC)
    target_compile_options(beatsmith_warnings INTERFACE /W4 $<$<BOOL:${BEATSMITH_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(beatsmith_warnings INTERFACE
        -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion
        $<$<BOOL:${BEATSMITH_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

add_library(beatsmith_sanitizers INTERFACE)
if(BEATSMITH_SANITIZERS AND NOT MSVC)
    target_compile_options(beatsmith_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(beatsmith_sanitizers INTERFACE -fsanitize=address,undefined)
endif()

#===============================================================================
# Engine: plain C++17, no JUCE. Shared by the plug-in and the tests.

add_library(beatsmith_engine STATIC
    Source/Engine/BlockContext.h
    Source/Engine/NoteEvent.h
    Source/Engine/Fraction.h
    Source/Engine/Pattern.h
    Source/Engine/Pattern.cpp
    Source/Engine/MusicalTimeline.h
    Source/Engine/MusicalTimeline.cpp
    Source/Engine/TestPattern.h
    Source/Engine/TestPattern.cpp
    Source/Engine/BeatsmithEngine.h
    Source/Engine/BeatsmithEngine.cpp)
target_include_directories(beatsmith_engine PUBLIC Source)
target_link_libraries(beatsmith_engine PRIVATE beatsmith_warnings PUBLIC beatsmith_sanitizers)
set_target_properties(beatsmith_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BEATSMITH_BUILD_ENGINE_TESTS)
    enable_testing()
    add_executable(beatsmith_engine_tests
        Tests/TestFramework.h
        Tests/Engine/EngineTestRig.h
        Tests/Engine/EngineTestMain.cpp
        Tests/Engine/PatternTests.cpp
        Tests/Engine/TimingTests.cpp
        Tests/Engine/NoteSafetyTests.cpp)
    target_link_libraries(beatsmith_engine_tests PRIVATE beatsmith_engine beatsmith_warnings)
    add_test(NAME engine COMMAND beatsmith_engine_tests)
endif()

#===============================================================================
# Plug-in. Dependencies are pinned so every build is reproducible.

if(BEATSMITH_BUILD_PLUGIN)
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)

    # JUCE 9.0.4 (the tag points at commit 3fb5cbc0340758596dac39f7c8de47d562b4896e), same as MIDI Forge.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.4
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(JUCE)

    set(BEATSMITH_PLUGIN_SOURCES
        Source/Plugin/PluginProcessor.h
        Source/Plugin/PluginProcessor.cpp
        Source/UI/Theme.h
        Source/UI/PluginEditor.h
        Source/UI/PluginEditor.cpp)

    set(BEATSMITH_JUCE_DEFINITIONS
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    juce_add_plugin(Beatsmith
        PRODUCT_NAME "Beatsmith"
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "ugerhard"
        BUNDLE_ID com.ugerhard.beatsmith
        PLUGIN_MANUFACTURER_CODE Ugrh
        PLUGIN_CODE Btsm
        FORMATS VST3
        IS_SYNTH FALSE
        IS_MIDI_EFFECT TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    # Only the JUCE modules Beatsmith needs: the plug-in wrapper, processors and the GUI. juce_audio_utils is
    # deliberately left out – it would add juce_audio_formats (audio file codecs) and juce_audio_devices (audio
    # and MIDI drivers), which a MIDI plug-in never uses: less third-party code in the plug-in, a smaller binary.
    target_sources(Beatsmith PRIVATE ${BEATSMITH_PLUGIN_SOURCES})
    target_compile_definitions(Beatsmith PUBLIC ${BEATSMITH_JUCE_DEFINITIONS})
    target_link_libraries(Beatsmith
        PRIVATE
            beatsmith_engine
            juce::juce_audio_processors
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    if(BEATSMITH_BUILD_PLUGIN_TESTS)
        enable_testing()
        juce_add_console_app(beatsmith_plugin_tests PRODUCT_NAME "Beatsmith Plugin Tests")
        target_sources(beatsmith_plugin_tests PRIVATE ${BEATSMITH_PLUGIN_SOURCES} Tests/Plugin/PluginTests.cpp)
        target_include_directories(beatsmith_plugin_tests PRIVATE Source)
        target_compile_definitions(beatsmith_plugin_tests PRIVATE
            ${BEATSMITH_JUCE_DEFINITIONS}
            JucePlugin_Name="Beatsmith"
            JucePlugin_VersionString="${PROJECT_VERSION}")
        target_link_libraries(beatsmith_plugin_tests PRIVATE
            beatsmith_engine
            juce::juce_audio_processors
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
        add_test(NAME plugin COMMAND beatsmith_plugin_tests)
    endif()
endif()
