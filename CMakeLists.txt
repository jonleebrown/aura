cmake_minimum_required(VERSION 3.22)

project(KarmaTestBed VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(JUCE_SOURCE_DIR)
    if(NOT EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "JUCE_SOURCE_DIR must point to a JUCE source checkout.")
    endif()
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    find_package(JUCE CONFIG REQUIRED)
endif()

juce_add_plugin(KarmaTestBed
    COMPANY_NAME "Aurora Test Bed"
    BUNDLE_ID "audio.aurora.testbed.karma"
    PLUGIN_MANUFACTURER_CODE Qbtx
    PLUGIN_CODE Apth
    PRODUCT_NAME "Karma Test Bed"
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Modulation)

juce_generate_juce_header(KarmaTestBed)

target_sources(KarmaTestBed
    PRIVATE
        src/PluginEditor.cpp
        src/PluginProcessor.cpp)

target_include_directories(KarmaTestBed
    PRIVATE
        shared/include
        src)

target_compile_definitions(KarmaTestBed
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(KarmaTestBed
    PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
