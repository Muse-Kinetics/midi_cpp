#ifndef MIDI_BYTESTREAM_PARSER_H
#define MIDI_BYTESTREAM_PARSER_H

#include <cstdint>
#include <cstddef>
#include "MIDI_sysex.hpp"

class MidiBytestreamParser {
public:
    using DebugMessageCallback = void (*)(void* ctx, uint8_t* msg, size_t len);

    MidiBytestreamParser(SysExMessage* sysexObj);

    void setDebugCallback(DebugMessageCallback cb, void* ctx) { callback = cb; context = ctx; }

    // Feed a single byte; returns true if a full message was completed
    bool parse(uint8_t byte);

    uint8_t msg[3]; // Public for reading when parse() returns true
    size_t msgLen = 0;

private:
    enum class State {
        WAIT_STATUS,
        READ_DATA,
        SYSEX
    } state = State::WAIT_STATUS;

    SysExMessage* syx = nullptr;
    void* context = nullptr;
    DebugMessageCallback callback = nullptr;

    uint8_t msgIndex = 0;
    uint8_t expectedLength = 0;
    uint8_t runningStatus = 0;
};

#endif // MIDI_BYTESTREAM_PARSER_H