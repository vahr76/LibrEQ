cmake_minimum_required(VERSION 3.22)
# Build for Apple Silicon and Intel, and for macOS versions still in common use.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
# On Windows, link the C++ runtime statically so that users need no extra install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(LibrEQ VERSION 0.2.0 LANGUAGES C CXX)
include(CTest)
include(FetchContent)

add_executable(LibrEQDspTests tests/DspTests.cpp src/DSP/Equalizer.cpp src/DSP/LinearPhase.cpp)
target_include_directories(LibrEQDspTests PRIVATE src)
target_compile_features(LibrEQDspTests PRIVATE cxx_std_20)
add_test(NAME dsp COMMAND LibrEQDspTests)

option(LIBREQ_DSP_ONLY "Build only the JUCE-free DSP tests" OFF)
if(LIBREQ_DSP_ONLY)
    return()
endif()

option(LIBREQ_COPY_AFTER_BUILD "Install the plugin into the user's plugin folder after building" ON)
set(LIBREQ_FORMATS VST3)
if(APPLE)
    list(APPEND LIBREQ_FORMATS AU)
endif()

FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.10 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

juce_add_binary_data(LibrEQAssets SOURCES assets/fonts/Inter-Regular.otf assets/fonts/Inter-SemiBold.otf)

juce_add_plugin(LibrEQ
    COMPANY_NAME "LibrEQ Community"
    BUNDLE_ID org.libreq.audio.libreq
    PLUGIN_MANUFACTURER_CODE LiEQ
    PLUGIN_CODE LiEQ
    FORMATS ${LIBREQ_FORMATS}
    PRODUCT_NAME "LibrEQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD ${LIBREQ_COPY_AFTER_BUILD})
juce_generate_juce_header(LibrEQ)
target_sources(LibrEQ PRIVATE
    src/Plugin/PluginProcessor.cpp
    src/Plugin/PluginEditor.cpp
    src/DSP/Equalizer.cpp
    src/DSP/LinearPhase.cpp)
target_include_directories(LibrEQ PRIVATE src)
target_compile_features(LibrEQ PRIVATE cxx_std_20)
target_compile_definitions(LibrEQ PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(LibrEQ PRIVATE LibrEQAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Run on distributions with an older C++ runtime than the build machine.
    target_link_options(LibrEQ_VST3 PRIVATE -static-libstdc++ -static-libgcc)
endif()

# Exercises the processor and the editor without a DAW, and renders a screenshot.
juce_add_console_app(LibrEQUiCheck)
target_sources(LibrEQUiCheck PRIVATE
    tests/UiCheck.cpp
    src/Plugin/PluginProcessor.cpp
    src/Plugin/PluginEditor.cpp
    src/DSP/Equalizer.cpp
    src/DSP/LinearPhase.cpp)
juce_generate_juce_header(LibrEQUiCheck)
target_include_directories(LibrEQUiCheck PRIVATE src)
target_compile_features(LibrEQUiCheck PRIVATE cxx_std_20)
target_compile_definitions(LibrEQUiCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=0)
target_link_libraries(LibrEQUiCheck PRIVATE LibrEQAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
add_test(NAME ui COMMAND LibrEQUiCheck)
