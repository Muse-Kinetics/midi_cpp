#ifndef MIDI_CPP_CONFIG_H
#define MIDI_CPP_CONFIG_H

/*
  ----------------------------------------------------------------------------
  File:        MIDI_sysex.hpp.template
  Description: Configures the MIDI_CPP library. This is a template that you 
  should copy out of the library and into your application, and then remove the
  .template from the file name.

  Copyright © 2025 KMI Music, Inc. All rights reserved.
  Unauthorized copying of this file, via any medium, is strictly prohibited.
  Proprietary and confidential.

  ----------------------------------------------------------------------------
*/

// library includes
#include "MIDI_device_metadata.hpp" // ok to update with new products

// user includes
#include "sharedEnums.h"
#include "main.h"
#include "EMProRiserFW.h" // riser firmware version
#include "utils.h"


// Product ID - change to match your product
#define SYX_PRODUCT_ID_LSB PID_MIDI_EMPRO

// SysEx ID Reply Versions
#define SYX_ID_BL_VER1 fwImage_bootByte.app_ver[0] // EMProRiserFW.h
#define SYX_ID_BL_VER2 fwImage_bootByte.app_ver[1]
#define SYX_ID_BL_VER3 fwImage_bootByte.app_ver[2]

#define SYX_ID_APP_VER1 SYS_FW_VERSION_MAJOR // main.h
#define SYX_ID_APP_VER2 SYS_FW_VERSION_MINOR
#define SYX_ID_APP_VER3 SYS_FW_VERSION_PATCH


// Status and error codes - replace with the equivalent enums that your application uses
#define SYX_SEND_RETURN_CODE_OK 				STATUS_OK						// success code 
#define SYX_SEND_RETURN_CODE_NO_SEND_FUNCTION 	ERR_TX_NO_SYX_SEND_FUNCTION 	// if no syx send function has been defined (nullptr) then return with this 

// block size to limit size of vectors
#define SYX_TX_BLOCK_SIZE 48 
#define SYX_RX_BLOCK_SIZE 64

// ************************************
// Library Features and Functionality
// ************************************


#endif/* MIDI_CPP_CONFIG_H */
