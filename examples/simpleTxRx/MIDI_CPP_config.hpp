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

// Product ID - change to match your product
#define SYX_PRODUCT_ID_LSB 0x36

// SysEx ID Reply Versions
#define SYX_ID_BL_VER1 0x09
#define SYX_ID_BL_VER2 0x09
#define SYX_ID_BL_VER3 0x09

#define SYX_ID_APP_VER1 0x01
#define SYX_ID_APP_VER2 0x02
#define SYX_ID_APP_VER3 0x03


// Status and error codes - replace with the equivalent enums that your application uses
#define SYX_SEND_RETURN_CODE_OK 				0						// success code 
#define SYX_SEND_RETURN_CODE_NO_SEND_FUNCTION 	-1 	// if no syx send function has been defined (nullptr) then return with this 

// block size to limit size of vectors
#define SYX_TX_BLOCK_SIZE 4800 
#define SYX_RX_BLOCK_SIZE 6400

// ************************************
// Library Features and Functionality
// ************************************


#endif/* MIDI_CPP_CONFIG_H */
