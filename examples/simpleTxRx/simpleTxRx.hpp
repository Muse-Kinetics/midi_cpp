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
