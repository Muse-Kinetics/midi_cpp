/*
  ----------------------------------------------------------------------------
  File:        simpleTxRx.cpp
  Description: Example application demonstrating how to use the MIDI_CPP library 
  for sending and receiving MIDI messages, including KMI/MK formatted SysEx messages.

  Copyright (c) 2026 KMI Music, Inc.
  SPDX-License-Identifier: MIT

  Author: Eric Bateman <eric@musekinetics.com>

  ----------------------------------------------------------------------------
*/

#include "simpleTxRx.hpp"
#include <RtMidi.h>

RtMidi::Api getDefaultApi()
{
#if defined(DEFAULT_RTMIDI_API_MACOSX_CORE)
    std::cout << "[INFO] Using RtMidi API: MACOSX_CORE" << std::endl;
    return RtMidi::MACOSX_CORE;
#elif defined(DEFAULT_RTMIDI_API_WINDOWS_MM)
    std::cout << "[INFO] Using RtMidi API: WINDOWS_MM" << std::endl;
    return RtMidi::WINDOWS_MM;
#elif defined(DEFAULT_RTMIDI_API_LINUX_ALSA)
    std::cout << "[INFO] Using RtMidi API: LINUX_ALSA" << std::endl;
    return RtMidi::LINUX_ALSA;
#else
    std::cout << "[WARNING] No RtMidi API defined, using UNSPECIFIED (Dummy backend)" << std::endl;
    return RtMidi::UNSPECIFIED;
#endif
}

SimpleTxRx::SimpleTxRx()
    :   syxRx(nullptr), // if you wanted your SysExMessageRX object to automatically respond to SysExID requests, you would pass a pointer to a SysExMessageTX object here
        byteParser(&syxRx) // pass the SysExMessageRX object to the MidiBytestreamParser, which handles incoming MIDI data
{
}

SimpleTxRx::~SimpleTxRx()
{
}

void SimpleTxRx::setup()
{
    // First we set up RtMidi
    midiOut = std::make_unique<RtMidiOut>(getDefaultApi(), "SimpleTxRx Out");
    midiIn = std::make_unique<RtMidiIn>(getDefaultApi(), "SimpleTxRx In");

    while (midiOut == nullptr || midiIn == nullptr) {
        std::cerr << "[ERROR] Failed to create MIDI ports. Retrying..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::string portNameOut = "SimpleTxRx"; 
    std::string portNameIn = portNameOut; 
    midiOut->openVirtualPort(portNameOut);

    // Enumerate all available MIDI input ports
    unsigned int nPorts = midiIn->getPortCount();
    bool portFound = false;
    for (unsigned int i = 0; i < nPorts; ++i) {
        std::string portNameFound = midiIn->getPortName(i);
        
        // Check if the current port name matches the target port name
        if (portNameFound == portNameIn) {
            midiIn->openPort(i);  // Open the port with the found index
            std::cout << "Opened MIDI input port: " << portNameFound << " (Index: " << i << ")" << std::endl;
            portFound = true;
            break;
        }
    }

    // If no matching port was found
    if (!portFound) {
        std::cerr << "Error: Could not find a port with the name " << portNameIn << std::endl;
    }

    // Set up RtMidi input callback, which puts incoming MIDI messages into the byte parser
    midiIn->setCallback(&SimpleTxRx::midiInputCallback, this);
    midiIn->ignoreTypes(false, false, false);

    // Set up MIDI_CPP callbacks
    setupCallbacks();
}

void SimpleTxRx::setupCallbacks()
{
    // SysEx TX callbacks
    syxTx.setCB_tx_Context(this);
    syxTx.setCB_send(&SimpleTxRx::sendSysExCallback);

    // SysEx RX callbacks
    syxRx.setCB_rx_Context(this);
    syxRx.setCB_DebugPrint_Context(this);
    syxRx.setCB_DebugPrint(&SimpleTxRx::debugPrint);
    syxRx.setCB_rx_ActiveSense(&SimpleTxRx::rx_activeSense);
    syxRx.setCB_rx_IDRequest(&SimpleTxRx::rx_id_request);
    syxRx.setCB_rx_IDReply(&SimpleTxRx::rx_id_reply);
    syxRx.setCB_rx_HostMessage(&SimpleTxRx::rx_host_message);
    syxRx.setCB_rx_PacketData(&SimpleTxRx::rx_packet_data);
}

// send messages out of the RtMidiOut object, which loop back to the RtMidiIn object and get processed by the byte parser
void SimpleTxRx::sendExampleMessages()
{
    std::vector<unsigned char> message;

    std::cout << std::endl << "Send IDReq" << std::endl;
    // 1. Send SysEx ID Request
    syxTx.sendSysExIDRequest();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    std::cout << std::endl << "Send IDReply" << std::endl;
    // 2. Send SysEx ID Reply
    syxTx.sendSysExIDReply();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // 3. Send dummy PacketData message
    std::cout << std::endl << "Send PacketData, preamble and then payload" << std::endl;
    uint8_t dummyData[20];
    for (int i = 0; i < 20; ++i)
        dummyData[i] = 200+i; // show off 8bit data

    syxTx.sendSyxFormattedMessage(SYX_PRODUCT_ID_LSB, MSG_CAT_SYSTEM, MSG_SYS_MIDI_PRINT, dummyData, sizeof(dummyData));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // 4. Send ActiveSense message
    std::cout << std::endl << "Send ActiveSense" << std::endl;
    message.clear();
    message.push_back(MIDI_RT_ACTIVE_SENSE); 
    midiOut->sendMessage(&message); 
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // 5. Send Note On message
    std::cout << std::endl << "Send NoteOn" << std::endl;
    message.clear();
    message = {MIDI_NOTE_ON, 60, 127}; // Note On message for middle C
    midiOut->sendMessage(&message); 
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

//-----------------------------
// Callback Implementations
//-----------------------------

// The sendSysExCallback should connect to your MIDI output
int16_t SimpleTxRx::sendSysExCallback(void* ctx, uint8_t* data, uint16_t length)
{
    if (!length || !data) {
        std::cerr << "[ERROR] Invalid SysEx data received in callback." << std::endl;
        return -1; // error
    }

    auto self = static_cast<SimpleTxRx*>(ctx);

    std::vector<unsigned char> msg(data, data + length);

    if (self->midiOut) {
        self->midiOut->sendMessage(&msg); // <<-- your MIDI output
    } else {
        std::cerr << "Error: midiOut is not initialized!" << std::endl;
    }

    std::cout << "[SyxTX] Sent SysEx message of length " << length << std::endl;
    return 0; // success
}

void SimpleTxRx::debugPrint(void* ctx, const char* str)
{
    std::cout << "[DEBUG] " << str << std::endl;
}

// EMC requires we check this in the sysex handler, the bytestream object will take priority if implemented
void SimpleTxRx::rx_activeSense(void* ctx)
{
    std::cout << "[SyxRX] ActiveSense received" << std::endl;
}

void SimpleTxRx::rx_id_request(void* ctx)
{
    std::cout << "[SyxRX] ID Request received" << std::endl;
}

void SimpleTxRx::rx_id_reply(void* ctx, SYSEX_DEVICE_INQUIRY_REPLY* reply)
{
    std::cout << "[SyxRX] ID Reply received" << std::endl;
    std::cout << "  Manufacturer ID: " << (int)reply->metadata.mfg_id[0] << "."
              << (int)reply->metadata.mfg_id[1] << "."
              << (int)reply->metadata.mfg_id[2] << std::endl;
    std::cout << "  Family ID: " << (int)reply->metadata.family_id[0] << "."
              << (int)reply->metadata.family_id[1] << std::endl;
    std::cout << "  Product ID: " << reply->metadata.prod_id[0] << "."
              << (int)reply->metadata.prod_id[1] << std::endl;
    std::cout << "  Bootloader Version: "
              << (int)reply->bl_ver[0] << "."
              << (int)reply->bl_ver[1] << "."
              << (int)reply->bl_ver[2] << std::endl;    
    std::cout << "  Application Version: " 
              << (int)reply->app_ver[0] << "."
              << (int)reply->app_ver[1] << "."
              << (int)reply->app_ver[2] << std::endl;
}

// Host messages are Dan's format used in the Riser Bootloader, not a common implementation in our products
void SimpleTxRx::rx_host_message(void* ctx, uint8_t msg_type, uint8_t data_val, uint16_t int_val)
{
    std::cout << "[SyxRX] Host Message: type=" << (int)msg_type
              << " val=" << (int)data_val
              << " int=" << int_val << std::endl;
}

// Packet Data messages are KMI formatted data payloads, used by most KMI/MK products. 
// Handles 8bit/7bit encoding and CRC checking.
void SimpleTxRx::rx_packet_data(void* ctx, PACKET_PREAMBLE* preamble, uint8_t* packet_data)
{
    std::cout << "[SyxRX] Packet Data received. Category=" << (int)preamble->category
              << ", Type=" << (int)preamble->type
              << ", Length=" << preamble->length;

    std::cout << std::endl;
    for (int i = 0; i < preamble->length; ++i) {
        std::cout << " [" << (int)packet_data[i] << "]";
    } 
    std::cout << std::endl;
}

// Channel messages (0x80 - 0xEF)
void SimpleTxRx::rx_midi_message(unsigned char statusByte, const uint8_t* msg)
{
    unsigned char channel = statusByte & 0x0F;  // Extract the channel from the status byte
    int bendValue = 0;

    switch (statusByte)
    {
        case MIDI_NOTE_ON:
            std::cout << "[MIDI Input] Channel Message: Note On, Channel " << (int)(channel + 1)
                      << ", Note: " << (int)msg[1]
                      << ", Velocity: " << (int)msg[2] << std::endl;
            break;
        case MIDI_NOTE_OFF:
            std::cout << "[MIDI Input] Channel Message: Note Off, Channel " << (int)(channel + 1)
                      << ", Note: " << (int)msg[1]
                      << ", Velocity: " << (int)msg[2] << std::endl;
            break;
        case MIDI_NOTE_AFTERTOUCH:
            std::cout << "[MIDI Input] Channel Message: Polyphonic Key Pressure, Channel " << (int)(channel + 1)
                      << ", Note: " << (int)msg[1]
                      << ", Pressure: " << (int)msg[2] << std::endl;
            break;
        case MIDI_CONTROL_CHANGE:
            std::cout << "[MIDI Input] Channel Message: Control Change, Channel " << (int)(channel + 1)
                      << ", Controller: " << (int)msg[1]
                      << ", Value: " << (int)msg[2] << std::endl;
            break;
        case MIDI_PROG_CHANGE:
            std::cout << "[MIDI Input] Channel Message: Program Change, Channel " << (int)(channel + 1)
                      << ", Program: " << (int)msg[1] << std::endl;
            break;
        case MIDI_CHANNEL_PRESSURE:
            std::cout << "[MIDI Input] Channel Message: Channel Pressure, Channel " << (int)(channel + 1)
                      << ", Pressure: " << (int)msg[1] << std::endl;
            break;
        case MIDI_PITCH_BEND:
            bendValue = (msg[1] | (msg[2] << 7)) - 8192;
            std::cout << "[MIDI Input] Channel Message: Pitch Bend, Channel " << (int)(channel + 1)
                      << ", Value: " << bendValue << std::endl;
            break;
        default:
            std::cout << "[MIDI Input] Unknown Channel Message: " << std::hex << (int)statusByte << std::dec << std::endl;
            break;
    }
}

// Real-time messages (0xF0 - 0xF7)
void SimpleTxRx::rx_midi_realtime(unsigned char statusByte, const uint8_t* msg)
{
    if (statusByte >= 0xF0)
    {
        switch (statusByte)
        {
            case MIDI_MTC:
                std::cout << "[MIDI Input] Real-time Message: MTC (F1) - "
                          << (int)msg[1] << std::endl;
                break;
            case MIDI_SONG_POSITION:
                std::cout << "[MIDI Input] Real-time Message: Song Position (F2) - " 
                          << (int)msg[1] << " / "
                          << (int)msg[2] << std::endl;
                break;
            case MIDI_SONG_SELECT:
                std::cout << "[MIDI Input] Real-time Message: Song Select (F3) - "
                          << (int)msg[1] << std::endl;
                break;
            case MIDI_TUNE_REQUEST:
                std::cout << "[MIDI Input] Real-time Message: Tune Request (F6)" << std::endl;
                break;
            case MIDI_RT_CLOCK:
                std::cout << "[MIDI Input] Real-time Message: Timing Clock (F8)" << std::endl;
                break;
            case MIDI_RT_START:
                std::cout << "[MIDI Input] Real-time Message: Start (FA)" << std::endl;
                break;
            case MIDI_RT_CONTINUE:
                std::cout << "[MIDI Input] Real-time Message: Continue (FB)" << std::endl;
                break;
            case MIDI_RT_STOP:
                std::cout << "[MIDI Input] Real-time Message: Stop (FC)" << std::endl;
                break;
            case MIDI_RT_ACTIVE_SENSE:
                std::cout << "[MIDI Input] Real-time Message: Active Sense (FE)" << std::endl;
                break;
            case MIDI_RT_RESET:
                std::cout << "[MIDI Input] Real-time Message: System Reset (FF)" << std::endl;
                break;
            default: // F4, F5, F9, FD are undefined in MIDI spec
                std::cout << "[MIDI Input] Unknown Real-time Message: " << std::hex << (int)statusByte << std::dec << std::endl;
                break;
        }
    }
}

void SimpleTxRx::midiInputCallback(double timeStamp, std::vector<unsigned char>* message, void* userData)
{
    auto self = static_cast<SimpleTxRx*>(userData);

    uint8_t msg[3] = {0};

    for (auto byte : *message)
    {
        // 
        if (self->byteParser.parse(byte))
        {
            unsigned char statusByte = self->byteParser.msg[0];

            if (self->byteParser.msgLen <= 3)
            {
                std::copy(self->byteParser.msg, self->byteParser.msg + self->byteParser.msgLen, msg);

                if (statusByte >= 0xF0)
                {
                    // Real-time message
                    self->rx_midi_realtime(statusByte, msg);
                }
                else
                {
                    // Channel message
                    self->rx_midi_message(statusByte, msg);
                }
            }
        }
    }
}