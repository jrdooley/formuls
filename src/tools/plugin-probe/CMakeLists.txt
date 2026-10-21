# A minimal plugin host for checking formuls' plugin builds: see main.cpp and
# src/tools/README.md. JUCE is taken from ~/JUCE, or -DJUCE_DIR=<path>.
cmake_minimum_required(VERSION 3.22)
project(plugin_probe VERSION 0.1)

if(NOT JUCE_DIR)
    file(TO_CMAKE_PATH "$ENV{HOME}/JUCE" JUCE_DIR)
    if(WIN32)
        file(TO_CMAKE_PATH "$ENV{USERPROFILE}/JUCE" JUCE_DIR)
    endif()
endif()
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce)

juce_add_console_app(plugin_probe PRODUCT_NAME "plugin_probe")
target_sources(plugin_probe PRIVATE main.cpp)
target_compile_definitions(plugin_probe PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>
    $<$<PLATFORM_ID:Linux>:JUCE_PLUGINHOST_LV2=1>)
target_link_libraries(plugin_probe PRIVATE juce::juce_audio_processors juce::juce_audio_utils)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Xlib, for looking at the editor's window tree (JUCE itself loads it at run time).
    find_package(X11 REQUIRED)
    target_link_libraries(plugin_probe PRIVATE X11::X11)
endif()
