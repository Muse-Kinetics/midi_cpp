/*
  ----------------------------------------------------------------------------
  File:        simpleTxRx.hpp
  Description: Example application demonstrating how to use the MIDI_CPP library 
  for sending and receiving MIDI messages, including KMI/MK formatted SysEx messages.

  Copyright © 2025 KMI Music, Inc. All rights reserved.
  Unauthorized copying of this file, via any medium, is strictly prohibited.
  Proprietary and confidential.

  ----------------------------------------------------------------------------
*/

#ifndef SIMPLETXRX_H
#define SIMPLETXRX_H

#include "MIDI_sysex.hpp"
#include "MIDI_bytestream_parser.hpp"

#include <RtMidi.h>
#include <memory>
#include <vector>
#include <iostream>
#include <thread>

// Demonstration enums for a theoretical sysex packetdata implementation
// message category/types stored in the sysex preamble
enum SYX_MSG_CATEGORY
{
    MSG_CAT_NULL,
    MSG_CAT_SYSTEM,         
    MSG_CAT_INSTRUMENT,    
    NUM_MSG_CATEGORIES
};

enum SYX_MSG_SYSTEM
{
    MSG_SYS_NULL,
    MSG_SYS_EVENT,              // has data payload that matches an event_t struct
    MSG_SYS_LOAD_BOOTLOADER,     
    MSG_SYS_MIDI_PRINT,
    NUM_SYSTEM_MSG_TYPES
};

enum SYX_MSG_INSTRUMENT
{
    MSG_INST_NULL,
    MSG_INST_INSTRUMENT_NAME_REQ,            
    MSG_INST_INSTRUMENT_NAME_REPLY,    
    NUM_INSTRUMENT_MSG_TYPES
};


// SysEx TX/RX class for sending and receiving MIDI messages
class SimpleTxRx
{
public:
    SimpleTxRx();
    ~SimpleTxRx();

    void setup();
    void sendExampleMessages();

private:
    // MIDI
    std::unique_ptr<RtMidiOut> midiOut;
    std::unique_ptr<RtMidiIn> midiIn;

    // Sysex
    SysExMessageTX syxTx;
    SysExMessageRX syxRx;
    MidiBytestreamParser byteParser;

    // Setup callbacks
    void setupCallbacks();

    // SysEx TX callback
    static int16_t sendSysExCallback(void* ctx, uint8_t* data, uint16_t length);

    // SysEx RX callbacks
    static void debugPrint(void* ctx, const char* str);
    static void rx_activeSense(void* ctx);
    static void rx_id_request(void* ctx);
    static void rx_id_reply(void* ctx, SYSEX_DEVICE_INQUIRY_REPLY* reply);
    static void rx_host_message(void* ctx, uint8_t msg_type, uint8_t data_val, uint16_t int_val);
    static void rx_packet_data(void* ctx, PACKET_PREAMBLE* preamble, uint8_t* packet_data);

    // MIDI Input
    void rx_midi_message(unsigned char statusByte, const uint8_t* msg);
    void rx_midi_realtime(unsigned char statusByte, const uint8_t* msg);
    static void midiInputCallback(double timeStamp, std::vector<unsigned char>* message, void* userData);
};

#endif/* SIMPLETXRX_H */
