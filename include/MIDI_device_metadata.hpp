#ifndef MIDI_DEVICE_METADATA_H
#define MIDI_DEVICE_METADATA_H

/*
  ----------------------------------------------------------------------------
  File:        MIDI_device_metadata.hpp
  Description: Constants, enums, and structs to store device metadata

  Copyright © 2025 KMI Music, Inc. All rights reserved.
  Unauthorized copying of this file, via any medium, is strictly prohibited.
  Proprietary and confidential.

  ----------------------------------------------------------------------------
*/

#include "stdint.h"
#include "MIDI.hpp"
#include "MIDI_sysex.hpp"

#define DEVICE_ID_REPLY_HEADER_SIZE 12

#define kmi_id_1 				0x00
#define kmi_id_2 				0x01
#define kmi_id_3				0x5F

#define chuck_magic_number		0x7A // might not have been Chuck but this is our mysterious fourth sysex manufacturer ID number

#define kmi_family_lsb			0x00
#define kmi_family_msb			0x00


#define PID_MIDI_MSB            0x00 

#define PID_MIDI_EM1				0x27
#define PID_MIDI_EMPRO_RISER_BL		0x2B // 43
#define PID_MIDI_EMPRO_RISER		0x2C // 44
#define PID_USB_EMPRO_DEV			0x2D // not implemented in MIDI comms
#define PID_MIDI_EMPRO				0x2E // 46
#define PID_MIDI_EMPRO_SAMPLER		0x2F // 47
#define PID_MIDI_EPERC				0x32 // 50

#define SX_ADDRESS				0x00 // the sysex address/id/channel of this device


enum SYX_EMPRO_MSG_CATEGORY
{
    MSG_EMPRO_CAT_NULL,
    MSG_EMPRO_CAT_SYSTEM,         // 0x00 = direct access to EM PRO (stm32) event system
    NUM_EMPRO_MSG_CATEGORIES
};

enum SYX_EMPRO_MSG_SYSTEM
{
    MSG_EMPRO_SYS_NULL,
    MSG_EMPRO_SYS_EVENT,   // has data payload that matches the event_t struct, which means this can handle most requests/messages from the host (payloads need separate messages)
    NUM_EMPRO_SYSTEM_MSG_TYPES
};

extern uint8_t deviceIDraw[DEVICE_ID_REPLY_HEADER_SIZE];

#endif/* MIDI_DEVICE_METADATA_H */
