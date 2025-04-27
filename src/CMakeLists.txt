# src/CMakeLists.txt

add_library(MIDI_CPP
    MIDI_bytestream_parser.cpp
    MIDI_sysex.cpp
    MIDI_device_metadata.cpp
)

target_include_directories(MIDI_CPP
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)