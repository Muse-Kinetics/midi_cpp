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

// user includes
#include "sharedEnums.h"
#include "main.h"
#include "EMProRiserFW.h"
#include "utils.h"

// SysEx ID Reply Versions
#define SYX_ID_BL_VER1 fwImage_bootByte.app_ver[0] // Riser firmware
#define SYX_ID_BL_VER2 fwImage_bootByte.app_ver[1]
#define SYX_ID_BL_VER3 fwImage_bootByte.app_ver[2]

#define SYX_ID_APP_VER1 SYS_FW_VERSION_MAJOR // STM32 app firmware 
#define SYX_ID_APP_VER2 SYS_FW_VERSION_MINOR
#define SYX_ID_APP_VER3 SYS_FW_VERSION_PATCH


// Status and error codes - replace with the equivalent enums that your application uses
#define SYX_SEND_RETURN_CODE_OK 				STATUS_OK						// success code 
#define SYX_SEND_RETURN_CODE_NO_SEND_FUNCTION 	ERR_TX_NO_SYX_SEND_FUNCTION 	// if no syx send function has been defined (nullptr) then return with this 

// block size to limit size of vectors
#define SYX_BLOCK_SIZE 48 

// ************************************
// Library Features and Functionality
// ************************************

// comment these two lines out if you don't want to verify product IDs when receiving KMI formatted messages (packets or dan's editor message)
#define CHECK_PRODUCT_IDS
#define DO_PRODUCT_ID_CHECK switch (headerStd->product) { case PID_MIDI_EMPRO: case PID_MIDI_EMPRO_RISER_BL: case PID_MIDI_EMPRO_RISER:


#endif/* MIDI_CPP_CONFIG_H */
