cmake_minimum_required(VERSION 3.22)
project(PyGamerSPX VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# JUCE: fetched by the build scripts (git clone --depth 1 --branch 7.0.12)
# into third_party/JUCE. Never committed to the project zip.
add_subdirectory(third_party/JUCE)

# ----------------------------------------------------------------------
# The plugin: VST3 ONLY (current VST standard; no AU/CLAP/VST2/Standalone)
juce_add_plugin(PyGamerSPX
    COMPANY_NAME "PyGamerSPX"
    PLUGIN_MANUFACTURER_CODE PygS
    PLUGIN_CODE Spx9
    FORMATS VST3
    PRODUCT_NAME "PyGamer SPX"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES "Fx" "Reverb" "Delay" "Modulation")

juce_generate_juce_header(PyGamerSPX)

target_sources(PyGamerSPX PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(PyGamerSPX PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # Splash disabled under the GPLv3 open-source release (see README licensing).
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PyGamerSPX PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    PUBLIC juce::juce_recommended_config_flags)

# ----------------------------------------------------------------------
# Tool: headless VST3 validation harness (not a deliverable plugin)
juce_add_console_app(SpxHarness PRODUCT_NAME "SpxHarness")
juce_generate_juce_header(SpxHarness)
target_sources(SpxHarness PRIVATE tools/PluginHarness.cpp)
target_compile_definitions(SpxHarness PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1)
target_link_libraries(SpxHarness PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics)

# ----------------------------------------------------------------------
# Tool: GUI screenshot renderer for socials (not a deliverable plugin)
juce_add_gui_app(SpxScreenshot PRODUCT_NAME "SpxScreenshot")
juce_generate_juce_header(SpxScreenshot)
target_sources(SpxScreenshot PRIVATE
    tools/ScreenshotApp.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(SpxScreenshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(SpxScreenshot PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics)
