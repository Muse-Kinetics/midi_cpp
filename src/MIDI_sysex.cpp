#include "MIDI_sysex.hpp"

#include <stdio.h>
/*
  ----------------------------------------------------------------------------
  File:        MIDI_sysex.cpp
  Description: Methods to encode and decode KMI formatted SysEx Messages

  Copyright © 2025 KMI Music, Inc. All rights reserved.
  Unauthorized copying of this file, via any medium, is strictly prohibited.
  Proprietary and confidential.

  ----------------------------------------------------------------------------
*/


#include "MIDI_sysex.hpp"
#include "utils_crc.h"
#include <cstring>
#include <cstdio>

//----------------------------------------
// TX Implementation
//----------------------------------------

SysExMessageTX::SysExMessageTX()
{
    clear();
}

void SysExMessageTX::clear()
{
	memset(buffer, 0, sizeof(buffer)); // clear the buffer
    size = 0;
}

int16_t SysExMessageTX::single(uint8_t byte)
{
	int16_t returnCode = SYX_SEND_RETURN_CODE_OK;
	buffer[size++] = byte;

	if (size >= sizeof(buffer) || byte == MIDI_SX_STOP)
	{
		if (cb_debugPrint)
			cb_debugPrint(context_dp, "Emptying buffer");

		if (cb_tx_Send)
			returnCode = cb_tx_Send(context_tx, &buffer[0], size); // send the buffer
		clear(); // reset the buffer
	}
	return returnCode;
}

int16_t SysExMessageTX::array(const uint8_t* bytes, size_t length)
{
	int16_t returnCode = SYX_SEND_RETURN_CODE_OK;

    for (size_t i = 0; i < length; ++i)
	{
        returnCode = single(bytes[i]);
	}
	
	return returnCode;
}


// **************************************
// functions below encode 8bits to 7 bits, with/without crc
// **************************************

void SysExMessageTX::init_crc()
{
    crc = 0xFFFF;
}

void SysExMessageTX::init_encode()
{
    midi_hi_bits = midi_hi_count = 0;
}

int16_t SysExMessageTX::encode_char(uint8_t val)
{
	int16_t returnCode = SYX_SEND_RETURN_CODE_OK;

    midi_hi_bits |= (val & 0x80);
    midi_hi_bits >>= 1;
    returnCode = single(val & 0x7F);
    if (++midi_hi_count == SX_ENCODE_LEN) {
        midi_hi_count = 0;
        returnCode = single(midi_hi_bits);
    }
	return returnCode;
}

int16_t SysExMessageTX::encode_crc_char(uint8_t val)
{
    crc_byte(&crc, val);
    return encode_char(val);
}

int16_t SysExMessageTX::encode_crc_int(uint16_t val)
{
    encode_crc_char(val >> 8);
    return encode_crc_char(val);
}

int16_t SysExMessageTX::encode_int(uint16_t val)
{
    encode_char(val >> 8);
    return encode_char(val);
}

int16_t SysExMessageTX::flush_encode()
{
	int16_t returnCode = SYX_SEND_RETURN_CODE_OK;
    while (midi_hi_count)
		returnCode = encode_char(0);
	return returnCode;
}

//----------------------------------------
// TX Methods
//----------------------------------------

int16_t SysExMessageTX::makeSyxHeader(uint8_t targetPID)
{


    // Define the header array
    uint8_t header[] = 
    {
        MIDI_SX_START,
        kmi_id_1,
        kmi_id_2,
        kmi_id_3,
        PID_MIDI_MSB, // banish chuck's number from whence it came
        targetPID,
        SYX_FORMAT_KMI,          
        0, // four zeroes to align midi monitor any any encoding flush
        0,
        0,
        0,
        1, // start encoding
    };
    
	clear();
    return array(header, sizeof(header));
}

int16_t SysExMessageTX::sendSysExIDRequest()
{
	clear();
    uint8_t idReq[] = {MIDI_SX_START, SX_UNIVERSAL_NON_REALTIME, SX_ADDRESS, SX_UNV_GENERAL_INFO, SX_UNV_DEVID_REQ, MIDI_SX_STOP};
    array(idReq, sizeof(idReq));
	// MIDI_SX_STOP triggers send, so we don't need to call send here

	return SYX_SEND_RETURN_CODE_NO_SEND_FUNCTION;
}

int16_t SysExMessageTX::sendSysExIDReply()
{
	if (!cb_tx_Send)
		return SYX_SEND_RETURN_CODE_NO_SEND_FUNCTION;  

    uint8_t versionBL[] = {SYX_ID_BL_VER1, SYX_ID_BL_VER2, SYX_ID_BL_VER3}; 
    uint8_t versionAPP[] = {SYX_ID_APP_VER1, SYX_ID_APP_VER2, SYX_ID_APP_VER3};

    clear(); // safety
    
    array(deviceIDraw, sizeof(deviceIDraw)); 
    array(versionBL, sizeof(versionBL));
    array(versionAPP, sizeof(versionAPP));
    single(MIDI_SX_STOP); // MIDI_SX_STOP triggers send, so we don't need to call send here
		
	return SYX_SEND_RETURN_CODE_NO_SEND_FUNCTION;
}


// This is the PacketData format, used by most KMI/MK products.
// Note: certain product firmware (like Dan's Riser Bootloader) requires that we flush the syx encoding after the preamble 
// and before the data payload. This option has to be handled by the application.
// Note2: Products like SoftStep and 12Step would send multiple presets (packets) with a single preamble, see "LENGTH OF NEXT PACKET" below.
// Multiple packets are not currently implemented in this library.
int16_t SysExMessageTX::sendSyxFormattedMessage(uint8_t targetPID, uint8_t category, uint8_t type, uint8_t* ptr, uint16_t length)
{  
	if (!cb_tx_Send)
    	return SYX_SEND_RETURN_CODE_NO_SEND_FUNCTION; 

	int returnCode = SYX_SEND_RETURN_CODE_OK;

    makeSyxHeader(targetPID);
    
    // begin 7/8bit encoding
    init_encode();
    init_crc();

    // preamble
    encode_crc_char(category);            // message category
    encode_crc_char(type);               // message type
    
    encode_crc_int(length + 2 + 2); // this is the length of the payload, plus 2 for length of next packet (int), +2 for crc (int)
    encode_int(crc);
    

    // payload
    if (length) 
    {
        init_crc();

        if (flushAfterPreamble == SYX_FLUSH_YES)
        {
            flush_encode(); // EM Pro Riser bootloader expects us to flush the encoding here, most other applications do not
        }

        returnCode = cb_tx_Send(context_tx, &buffer[0], size); // transmit the preamble before encoding data
        clear();
        if (returnCode != SYX_SEND_RETURN_CODE_OK)
            return returnCode;

        while(length--)
        {
            returnCode = encode_crc_char(*ptr++); 

			if (returnCode != SYX_SEND_RETURN_CODE_OK)
				return returnCode;
        }
        
		// LENGTH OF NEXT PACKET
        encode_crc_int(0);  // if 0 then this is the last packet, if something other than zero, we can
                            // send additional blocks of data, ie multiple presets

        encode_int(crc); // this is the crc of the payload
    }

    // end 7/8bit encoding
    flush_encode();
    single(MIDI_SX_STOP); // MIDI_SX_STOP triggers send, so we don't need to call send here

	clear();
	return returnCode;
}


//----------------------------------------
// RX Implementation
//----------------------------------------

SysExMessageRX::SysExMessageRX(SysExMessageTX* syxSend)
	: syxSendPtr(syxSend)
{
	clear();
	updatePointers();
    rx_init();
}

void SysExMessageRX::clear()
{
    size = 0;
    ignore_rx = false;
}

void SysExMessageRX::single(uint8_t byte)
{
    if (size < sizeof(buffer))
        buffer[size++] = byte;
}

void SysExMessageRX::array(const uint8_t* bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        single(bytes[i]);
}


void SysExMessageRX::updatePointers()
{
    headerUniv = reinterpret_cast<SYSEX_UNIVERSAL_HEADER*>(buffer);
    headerStd  = reinterpret_cast<SYSEX_STANDARD*>(buffer);
}

void SysExMessageRX::rx_init()
{
    rx_state = CORE_SX_HEADER;
    rx_decode_active = false;
    rx_decode_count = 0;
    ignore_rx = false;
    clear();
}

void SysExMessageRX::rx_set_ignore()
{
    rx_init();
    ignore_rx = true;
}

// **************************************
// functions below decode 7bits to 8 bits
// **************************************

void SysExMessageRX::init_crc()
{
    crc = 0xFFFF;
}

void SysExMessageRX::init_decode()
{
    core_sx_decode.index_in = 0;
    core_sx_decode.index_out = 0;
}

void SysExMessageRX::decode_put(uint8_t val)
{
    core_sx_decode.buf[core_sx_decode.index_in++] = val;
}

bool SysExMessageRX::decode_get(uint8_t* val)
{
	if (core_sx_decode.index_in==SX_ENCODE_LEN+1) 
    {
		*val = core_sx_decode.buf[core_sx_decode.index_out++];
		if (core_sx_decode.buf[SX_ENCODE_LEN] & 1)
			*val |= 0x80;
		core_sx_decode.buf[SX_ENCODE_LEN] >>=1;
		if (core_sx_decode.index_out==SX_ENCODE_LEN) 
        {
			init_decode();
		}
		rx_decode_count++;
		return true; // bytes are ready
	}
	return false; // still gathering
}

// Test the CRC value for a chunk of data within the decoded message
// Assumes that the two bytes following the chunk are the crc
bool SysExMessageRX::testDecodedCRC(uint16_t startIndex, uint16_t length)
{
	if ((startIndex + length + 2) > (uint16_t)size)
	{
		return 0; // bad data
	}

	// Extract the chunk of data
    const uint8_t* dataChunk = &buffer[startIndex];

    // Calculate CRC for the data chunk
    uint16_t calculatedCRC = 0xFFFF; // Initialize CRC
    for (uint16_t i = 0; i < length; ++i) {
        crc_byte(&calculatedCRC, dataChunk[i]);
    }

	// Extract the expected CRC from the message
    uint16_t expectedCRC = (buffer[startIndex + length] << 8) | buffer[startIndex + length + 1];

	// Compare the calculated CRC with the expected CRC
    return (calculatedCRC == expectedCRC);
}





// *********************************************************************
// METHODS TO PROCESS SYSEX MESSAGES
// *********************************************************************

void SysExMessageRX::sx_process(uint8_t *msg, uint16_t length)
{
	for (uint8_t i = 0; i < length; i++) 
	{
		uint8_t sx_char = msg[i];

		if (sx_char == MIDI_RT_ACTIVE_SENSE)
		{
			if (cb_rx_ActiveSense) 
				cb_rx_ActiveSense(context_rx);
		}
		else if (sx_char == MIDI_SX_STOP)// SX Stop received, process the buffer
		{
			//printf("\n[%s] SX STOP\n", name);
			// process the message
			switch (rx_state)
			{
				case CORE_SX_ID_REPLY:
				{
					uint8_t *dataPtr = &buffer[0];

					// update device metadata
					SYSEX_DEVICE_INQUIRY_REPLY *idReply = (SYSEX_DEVICE_INQUIRY_REPLY*)dataPtr; // cast as struct
					if (cb_rx_id_reply) 
						cb_rx_id_reply(context_rx, idReply);

					
					break; // end CORE_SX_ID_REPLY
				}
				case CORE_SX_HOST_DATA: // this format is used by the riser bootloader, Dan's EDITOR/HOST MESSAGE formatting
				{
					//printf("[%s] ...HOST_DATA\n", name);
					uint8_t msgIndex = 0;
					uint8_t length = packet_data[msgIndex++];
					uint8_t msg_type = packet_data[msgIndex++];
					uint8_t data_val = packet_data[msgIndex++]; // only 7 bits
					uint16_t int_val = 0;                       // 16 bits

					(void)length; // silence unused variable warning

					int_val |= (packet_data[msgIndex++] & 0x7F) << 14; // bits 14-20 (7 bits)
					int_val |= (packet_data[msgIndex++] & 0x7F) << 7;  // bits 7-13 (7 bits)
					int_val |= (packet_data[msgIndex++] & 0x7F); // bits 0-6 (7 bits)
					
					if (cb_rx_HostMessage)
						cb_rx_HostMessage(context_rx, msg_type, data_val, int_val);

					break; 
					// end CORE_SX_HOST_DATA
				}
				case CORE_SX_PACKET_DATA: // KMI formatted data payload, used by Riser Application, likely also Sound Card/Linux and Editors
				{
					//printf("\n"); // we just printed the packet data so add a line break
					//printf("[%s] ...PACKET_DATA\n", name);
					if (testDecodedCRC(packet_data_index, preamble->length - 2) == false) // verify the CRC of the data, don't crc the crc (-2)
					{
						if (cb_debugPrint)
							cb_debugPrint(context_dp, "CRC FAIL!");
					}
					else
					{
						//printf("[%s] CRC pass\n", name);
						preamble->length -= 4; // remove the crc and length bytes from the length
						if (cb_rx_PacketData)
							cb_rx_PacketData(context_rx, preamble, packet_data); // child classes (riser, computer, soundcard etc) determine how this is handled
					}
					break; // end CORE_SX_PACKET_DATA
				}
				case CORE_SX_IGNORE:
					break;
				default:
				{
					// // EB TODO: does it matter if this is bootloader or not?
					// if (endpointType() == SERIAL_ENDPOINT && con_status != CON_RISER_APP) // we received sysex as our first uart message but we haven't completed a handshake, so request id
					if (syxSendPtr)
						syxSendPtr->sendSysExIDRequest(); 
					break;
				}
			}
			return;
		} // end MIDI_SX_STOP

		else if (sx_char == MIDI_SX_START) 
		{
			//printf("\n[%s] SX START\n", name);
			rx_init();
		} 
		else // body of sysex payload 
		{
			uint16_t core_sx_count = getSize();
			// make sure we aren't overloading our buffers/memory
			if (core_sx_count > SYX_MSG_SIZE || ignore_rx == true)
			{
				rx_set_ignore();
				return;
			}

			switch (rx_state)
			{
	
				case CORE_SX_HEADER:
				{
					if (core_sx_count <= sizeof(SYSEX_STANDARD)) 
					{
						single(sx_char);                    //stuff the buffer with incoming sysex data 
						core_sx_count++; // faster than calling getSize() again

						// ***************************************************
						// "Universal" SysEx
						// ***************************************************
						if (core_sx_count == sizeof(SYSEX_UNIVERSAL_HEADER)) 
						{
							updatePointers();
							if 	(								 		
									headerUniv->syx_universal == SX_UNIVERSAL_NON_REALTIME   &&		
									headerUniv->sub_id_1      == SX_UNV_GENERAL_INFO	 	   
								)	
							{    
								// process universal sysex messages here
								switch(headerUniv->sub_id_2)
								{
									case SX_UNV_DEVID_REQ:
										if (cb_rx_id_request) 
											cb_rx_id_request(context_rx);
										break;
									case SX_UNV_DEVID_REPLY:
										rx_state = CORE_SX_ID_REPLY;
										break;
									default:
										rx_set_ignore();
										break;
								}
							} //***end of if          
							

						} //***end of if size == SYSEX_UNIVERSAL_HEADER   
						
						// *************************************************
						// KMI header processing
						// *************************************************
						else if (core_sx_count == sizeof(SYSEX_STANDARD)) 
						{
							updatePointers();
							//printf("[%s] reached SYSEX_STANDARD\n", name);

							if (headerStd->manufacturer_id1 == kmi_id_1 && 
								headerStd->manufacturer_id2 == kmi_id_2 &&  
								headerStd->manufacturer_id3 == kmi_id_3)      // test if this is a KMI product
							{
								//printf("[%s] KMI SysEx ID Match, PID: %d\n", name, headerStd->product);
								
								if (headerStd->format == SYX_FORMAT_KMI) 
								{
									//printf("[%s] begin CORE_SX_PACKET_START_SEARCH\n", name);
									rx_state = CORE_SX_PACKET_START_SEARCH;
								}
								else if (headerStd->format == SYX_FORMAT_DANS_EDITOR_MESSAGE)
								{
									//printf("[%s] begin SYX_FORMAT_DANS_EDITOR_MESSAGE\n", name);
									packet_data_index = size;
									packet_data = &buffer[packet_data_index];
									rx_state = CORE_SX_HOST_DATA;
								}
								else
								{
									if (cb_debugPrint)
										cb_debugPrint(context_dp, "SysEx format unrecognized");
								}
								break;
							}
						}
					}
					break; // end CORE_SX_HEADER
				}
				case CORE_SX_HOST_DATA:
				case CORE_SX_ID_REPLY:
				{
					single(sx_char); // process unencoded data when we get to end of sysex
					break; // end CORE_SX_ID_REPLY
				}
				case CORE_SX_PACKET_START_SEARCH:
				{
					// Some KMI products will send a number of 0s before sending a 0x01 to indicate that the preamble is coming 
					// also helps data look nice in midi monitor
					// so we will wait until we get SX_PACKET_START, which also indicates that we are now supposed to decode 7bit->8bit
					if (sx_char==SX_PACKET_START) 
					{
						//%s] SX_PACKET_START\n", name);
						//printf("[%s] PREAMBLE: ", name);
						init_decode();
						init_crc();
						preamble_index = size; // this is actually the next byte, but zero indexing should put this in the right location
						rx_decode_active = true;
						rx_decode_count = 0; // reset the count
						rx_state = CORE_SX_PACKET_PREAMBLE;

					}
					break; // end CORE_SX_PACKET_START_SEARCH
				}
				case CORE_SX_PACKET_PREAMBLE: 
				{
					// the preamble is:
					// [msg msb/category] [msg lsb/type] - see SYX_MSG_CATEGORY and SYX_MSG_TYPES
					// [length msb] [length lsb] - the length of the next payload
					// [crc msb] [crc lsb] - the crc value of the preceding 4 bytes
					decode_put(sx_char); // all data received from this point on needs to be decoded 7bit->8bit

					while (decode_get(&sx_char))  // this will return true 7 times when 8 bytes have been received
					{
						//printf("%d:%x, ", rx_decode_count, sx_char);
						single(sx_char); // add decoded byte to message array/vector

						if (rx_decode_count == sizeof(PACKET_PREAMBLE))
						{
							//printf("\n[%s] reached PACKET_PREAMBLE\n", name);
							preamble = (PACKET_PREAMBLE*)(&buffer[preamble_index]); // cast as struct

							if (testDecodedCRC(preamble_index, sizeof(PACKET_PREAMBLE) - 2)) // verify the CRC of the preamble. don't crc the crc (-2)
							{
								// fix packet length for big endian <-> little endian
								preamble->length = SWAP_BYTES(preamble->length);

								//printf("[%s] CRC pass, proceed to CORE_SX_PACKET_DATA\n", name);
								//printf("[%s] DECODED DATA: ", name);
								// EB TODO: implement packet_data_init and packet_data_process
								rx_state = CORE_SX_PACKET_DATA;
								packet_data_index = preamble_index + sizeof(PACKET_PREAMBLE);
								packet_data = &buffer[packet_data_index];
								init_crc();
								rx_decode_count = 0; // reset the count
								if (flushAfterPreamble == SYX_FLUSH_YES)
								{
									init_decode(); // bootloader flushes the decode buffer here
								}
							}
							else
							{
								if (cb_debugPrint)
									cb_debugPrint(context_dp, "CRC FAIL!");

								rx_set_ignore();
							}
						}
						else if (rx_decode_count > sizeof(PACKET_PREAMBLE))
						{
							if (cb_debugPrint)
								cb_debugPrint(context_dp, "PACKET_PREAMBLE FAIL!");

							rx_set_ignore();
						}
					}
					break; // end CORE_SX_PACKET_PREAMBLE
				}
				case CORE_SX_PACKET_DATA:
				{
					decode_put(sx_char); // all data received from this point on needs to be decoded 7bit->8bit

					while (decode_get(&sx_char))  // this will return true 7 times when 8 bytes have been received
					{
						//printf("%x ", sx_char);
						single(sx_char); // add decoded byte to message array/vector
					}
					break; // end CORE_SX_PACKET_DATA
				}
				case CORE_SX_IGNORE:
					break; // end CORE_SX_IGNORE
				default:
					break;
			}
		}

	}
}
