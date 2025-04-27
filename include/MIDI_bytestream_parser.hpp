/*
  ----------------------------------------------------------------------------
  File:        bytestream_parser.hpp
  Description: Receive a MIDI bytestream, put SysEx into a SysExMessage object,
  all other data gets constructed into Realtime and Channel messages.

  Copyright © 2025 KMI Music, Inc. All rights reserved.
  Unauthorized copying of this file, via any medium, is strictly prohibited.
  Proprietary and confidential.

  ----------------------------------------------------------------------------
*/

#ifndef MIDI_BYTESTREAM_PARSER_H
#define MIDI_BYTESTREAM_PARSER_H

#include <cstdint>
#include <cstddef>
#include "MIDI_sysex.hpp"

#define STATUS_BYTE_MASK 0x80

class MidiBytestreamParser {
public:
    using DebugMessageCallback = void (*)(void* ctx, uint8_t* msg, size_t len);

    MidiBytestreamParser(SysExMessageRX* sysexObj);

    void setDebugCallback(DebugMessageCallback cb, void* ctx) { callback = cb; context = ctx; }

    // Feed a single byte; returns true if a full message was completed
    bool parse(uint8_t byte);
    void reset();

    uint8_t msg[3]; // Public for reading when parse() returns true
    size_t msgLen = 0;

private:
    enum class State {
        WAIT_STATUS,
        READ_DATA,
        SYSEX
    } state = State::WAIT_STATUS;

    SysExMessageRX* syx = nullptr;
    void* context = nullptr;
    DebugMessageCallback callback = nullptr;

    uint8_t msgIndex = 0;
    uint8_t expectedLength = 0;
    uint8_t runningStatus = 0;
};

#endif // MIDI_BYTESTREAM_PARSER_H