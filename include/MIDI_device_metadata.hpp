#ifndef MIDI_DEVICE_METADATA_H
#define MIDI_DEVICE_METADATA_H

/*
  ----------------------------------------------------------------------------
  File:        MIDI_device_metadata.hpp
  Description: Constants, enums, and structs to store device metadata

  Copyright (c) 2026 KMI Music, Inc.
  SPDX-License-Identifier: MIT

  Author: Eric Bateman <eric@musekinetics.com>

  ----------------------------------------------------------------------------
*/

#include "stdint.h"
#include "MIDI.hpp"
#include "MIDI_sysex.hpp"

#define DEVICE_ID_REPLY_HEADER_SIZE 12

#define kmi_id_1 				0x00
#define kmi_id_2 				0x01
#define kmi_id_3				0x5F

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
#define PID_MIDI_SOUNDSTATION 0x30

#define SX_ADDRESS				0x00 // the sysex address/id/channel of this device


extern uint8_t deviceIDraw[DEVICE_ID_REPLY_HEADER_SIZE];

#endif/* MIDI_DEVICE_METADATA_H */
