# examples/simpleTxRx/CMakeLists.txt

add_executable(simpleTxRx
    main.cpp
    simpleTxRx.hpp
    simpleTxRx.cpp
    ../rtmidi/RtMidi.cpp
    ../rtmidi/RtMidi.h
)

# Tell RtMidi which backend to compile
if(APPLE)
    target_compile_definitions(simpleTxRx PRIVATE DEFAULT_RTMIDI_API_MACOSX_CORE __MACOSX_CORE__)
    target_link_libraries(simpleTxRx
        "-framework CoreMIDI"
        "-framework CoreAudio"
        "-framework CoreFoundation"
    )
elseif(WIN32)
    target_compile_definitions(simpleTxRx PRIVATE DEFAULT_RTMIDI_API_WINDOWS_MM __WINDOWS_MM__)
    target_link_libraries(simpleTxRx winmm)
elseif(UNIX)
    target_compile_definitions(simpleTxRx PRIVATE DEFAULT_RTMIDI_API_LINUX_ALSA __LINUX_ALSA__)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    target_include_directories(simpleTxRx PRIVATE ${ALSA_INCLUDE_DIRS})
    target_link_libraries(simpleTxRx ${ALSA_LIBRARIES})
endif()

target_include_directories(simpleTxRx 
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/examples/rtmidi
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(simpleTxRx
    MIDI_CPP
    pthread
)

target_include_directories(MIDI_CPP PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)