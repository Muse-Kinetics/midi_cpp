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

#define PID_MIDI_SOUNDSTATION 0x30

#define PID_MIDI_EPERC				0x32 // 50




#define SX_ADDRESS				0x00 // the sysex address/id/channel of this device


extern uint8_t deviceIDraw[DEVICE_ID_REPLY_HEADER_SIZE];

// Runtime product ID — initialized from SYX_PRODUCT_ID_LSB (MIDI_CPP_config.hpp).
// Use setMidiProductId() to change at runtime; getMidiProductId() to read back.
// deviceIDraw[8] is kept in sync automatically.
uint8_t getMidiProductId();
void    setMidiProductId(uint8_t id);

#endif/* MIDI_DEVICE_METADATA_H */
