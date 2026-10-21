# Checks that WebSocketServer never blocks its caller on a client that stops
# reading: see main.cpp and src/tools/README.md. JUCE from ~/JUCE or -DJUCE_DIR.
cmake_minimum_required(VERSION 3.22)
project(ws_stall_test VERSION 0.1)

if(NOT JUCE_DIR)
    file(TO_CMAKE_PATH "$ENV{HOME}/JUCE" JUCE_DIR)
endif()
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce)

juce_add_console_app(ws_stall_test PRODUCT_NAME "ws_stall_test")
target_sources(ws_stall_test PRIVATE main.cpp ../../app/Source/WebSocketServer.cpp)
target_include_directories(ws_stall_test PRIVATE ../../app/Source)
target_compile_definitions(ws_stall_test PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ws_stall_test PRIVATE juce::juce_core juce::juce_events)
juce_generate_juce_header(ws_stall_test)
