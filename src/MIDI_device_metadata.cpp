#include "MIDI_device_metadata.hpp"

/*
  ----------------------------------------------------------------------------
  File:        MIDI_device_metadata.cpp
  Description: Constants, enums, and structs to store device metadata

  Copyright (c) 2026 KMI Music, Inc.
  SPDX-License-Identifier: MIT

  Author: Eric Bateman <eric@musekinetics.com>

  ----------------------------------------------------------------------------
*/

// Runtime product ID — initialized from SYX_PRODUCT_ID_LSB in MIDI_CPP_config.hpp.
// Changing via setMidiProductId() updates both this variable and deviceIDraw[8].
static uint8_t productIdLsb = SYX_PRODUCT_ID_LSB;

// Sysex Device ID Response
uint8_t deviceIDraw[DEVICE_ID_REPLY_HEADER_SIZE] = {
        MIDI_SX_START, 
        SX_UNIVERSAL_NON_REALTIME, 
        SX_ADDRESS, 
        SX_UNV_GENERAL_INFO,            // sub_id_1
        SX_UNV_DEVID_REPLY,             // sub_id_2
        kmi_id_1, kmi_id_2, kmi_id_3,   // mfg_id
        SYX_PRODUCT_ID_LSB, PID_MIDI_MSB,   // prod_id — static init; use setMidiProductId() to change at runtime
        kmi_family_lsb, kmi_family_msb  // family_id
};

uint8_t getMidiProductId()
{
    return productIdLsb;
}

void setMidiProductId(uint8_t id)
{
    productIdLsb    = id;
    deviceIDraw[8]  = id;   // keep Device ID Reply packet in sync
}

//SYSEX_DEVICE_INQUIRY_UNION *deviceID = (SYSEX_DEVICE_INQUIRY_UNION *) deviceIDraw;
