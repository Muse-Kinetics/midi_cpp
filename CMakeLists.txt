cmake_minimum_required(VERSION 3.14)
project(MIDI_CPP)

# Build the core library
add_subdirectory(src)

# Optionally build examples
option(BUILD_EXAMPLES "Build examples" OFF)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()