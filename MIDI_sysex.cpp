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

// empty constructor
SysExMessage::SysExMessage()
	: flushAfterPreamble(SYX_FLUSH_NO)
{
	ignore_rx = false;	
	d = (DEBUG_ARRAY*) getData();
}

// Destructor to clean up the message buffer
SysExMessage::~SysExMessage() {
    clear();
}


// add a single byte to the message
void SysExMessage::single(uint8_t byte) {
    message.push_back(byte);
}

// add an array to the message
void SysExMessage::array(const uint8_t* bytes, size_t length) {
    message.insert(message.end(), bytes, bytes + length);
}

// get the size of the message
size_t SysExMessage::getSize() const 
{
    return message.size();
}

void SysExMessage::reserve(size_t size)
{
	message.reserve(size);
}

// Clear the message buffer
void SysExMessage::clear() 
{
    message.clear();
	ignore_rx = false;
}

// Get a pointer to the message starting at the first byte
uint8_t* SysExMessage::getData() 
{
    return message.data();
}

void SysExMessage::updatePointers()
{
	headerUniv = (SYSEX_UNIVERSAL_HEADER*)(message.data());
	headerStd =  (SYSEX_STANDARD*)(message.data());
}

// **************************************
// RX Methods
// **************************************


void SysExMessage::rx_init(void)
{
	rx_state = CORE_SX_HEADER;
	rx_decode_active = false;
	rx_decode_count = 0;
	ignore_rx = false;
	clear();
}

void SysExMessage::rx_set_ignore(void)
{
	rx_init();
    ignore_rx = true;
}

// **************************************
// functions below encode 8bits to 7 bits, with/without crc
// **************************************

void SysExMessage::init_crc(void)
{
	crc = 0xFFFF; // avoid an uneccessary function call to utils.c
}

// get the size of the message
uint16_t SysExMessage::getCRC(){
    return crc;
}

void SysExMessage::init_encode(void) 
{
	midi_hi_bits = midi_hi_count = 0;
}

void SysExMessage::encode_char(uint8_t val) 
{
	midi_hi_bits |= (val & 0x80);
	midi_hi_bits >>= 1;
	single(val & 0x7f);
	if (++midi_hi_count == SX_ENCODE_LEN) 
	{
		midi_hi_count = 0;
		single(midi_hi_bits);
	}
}

void SysExMessage::encode_crc_char(uint8_t val) 
{
	crc_byte(&crc, val); 
	encode_char(val);
}

void SysExMessage::encode_crc_int(uint16_t val) 
{
	encode_crc_char(val>>8);
	encode_crc_char(val);
}

void SysExMessage::encode_int(uint16_t val) 
{
	encode_char(val>>8);
	encode_char(val);
}

// 
void SysExMessage::flush_encode(void) 
{
	while(midi_hi_count)
		encode_char(0);
}

// **************************************
// functions below decode 7bits to 8bits
// **************************************

void SysExMessage::init_decode(void) {
	core_sx_decode.index_in = core_sx_decode.index_out = 0;
}

void SysExMessage::decode_put(uint8_t val) 
{
	core_sx_decode.buf[core_sx_decode.index_in++] = val;
}

bool SysExMessage::decode_get(uint8_t *val) 
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
bool SysExMessage::testDecodedCRC(uint16_t startIndex, uint16_t length)
{
	if ((startIndex + length + 2) > (uint16_t)getSize())
	{
		return 0; // bad data
	}

	// Extract the chunk of data
    const uint8_t* dataChunk = getData() + startIndex;

    // Calculate CRC for the data chunk
    uint16_t calculatedCRC = 0xFFFF; // Initialize CRC
    for (uint16_t i = 0; i < length; ++i) {
        crc_byte(&calculatedCRC, dataChunk[i]);
    }

	// Extract the expected CRC from the message
    uint16_t expectedCRC = (getData()[startIndex + length] << 8) | getData()[startIndex + length + 1];

	// Compare the calculated CRC with the expected CRC
    return (calculatedCRC == expectedCRC);
}


// *********************************************************************
// METHODS TO PROCESS SYSEX MESSAGES
// *********************************************************************

void SysExMessage::sx_process(uint8_t *msg, uint16_t length)
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
					uint8_t *dataPtr = getData();

					// update device metadata
					SYSEX_DEVICE_INQUIRY_REPLY *idReply = (SYSEX_DEVICE_INQUIRY_REPLY*)dataPtr; // cast as struct
					if (cb_rx_id_reply) 
						cb_rx_id_reply(context_rx, idReply);

					
					break; // end CORE_SX_ID_REPLY
				}
				case CORE_SX_HOST_DATA: // this format is used by the riser bootloader, Dan's EDITOR/HOST MESSAGE formatting
				{
					// EB TODO: Callback for host data
					//printf("[%s] ...HOST_DATA\n", name);
					uint8_t msgIndex = 0;
					uint8_t length = packet_data[msgIndex++];
					uint8_t msg_type = packet_data[msgIndex++];
					uint8_t data_val = packet_data[msgIndex++]; // only 7 bits
					uint16_t int_val = 0;                       // 16 bits

					int_val |= (packet_data[msgIndex++] & 0x7F) << 14; // bits 14-20 (7 bits)
					int_val |= (packet_data[msgIndex++] & 0x7F) << 7;  // bits 7-13 (7 bits)
					int_val |= (packet_data[msgIndex++] & 0x7F); // bits 0-6 (7 bits)

					UNUSED(length); // for now don't worry about this, but if the need arises we have it
					
					if (cb_rx_HostMessage)
						cb_rx_HostMessage(context_rx, msg_type, data_val, int_val);

					break; 
					// end CORE_SX_HOST_DATA
				}
				case CORE_SX_PACKET_DATA: // KMI formatted data payload, used by Riser Application, likely also Sound Card/Linux and Editors
				{
					printf("\n"); // we just printed the packet data so add a line break
					//printf("[%s] ...PACKET_DATA\n", name);
					if (testDecodedCRC(packet_data_index, preamble->length - 2) == false) // verify the CRC of the data, don't crc the crc (-2)
					{
						if (cb_debugPrint)
							cb_debugPrint(context_dp, "CRC FAIL!");
					}
					else
					{
						uint8_t *dataPtr = getData();
						//printf("[%s] CRC pass\n", name);
						if (cb_rx_PacketData)
							cb_rx_PacketData(context_rx, preamble, dataPtr); // child classes (riser, computer, soundcard etc) determine how this is handled
					}
					break; // end CORE_SX_PACKET_DATA
				}
				case CORE_SX_IGNORE:
					break;
				default:
				{
					// // EB TODO: does it matter if this is bootloader or not?
					// if (endpointType() == SERIAL_ENDPOINT && con_status != CON_RISER_APP) // we received sysex as our first uart message but we haven't completed a handshake, so request id
					 	sendSysExIDRequest(); 
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
									packet_data_index = getSize();
									packet_data = getData() + packet_data_index;
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
						preamble_index = getSize(); // this is actually the next byte, but zero indexing should put this in the right location
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
						printf("%d:%x, ", rx_decode_count, sx_char);
						single(sx_char); // add decoded byte to message array/vector

						if (rx_decode_count == sizeof(PACKET_PREAMBLE))
						{
							d = (DEBUG_ARRAY*) getData(); 
							//printf("\n[%s] reached PACKET_PREAMBLE\n", name);
							preamble = (PACKET_PREAMBLE*)(getData() + preamble_index); // cast as struct

							if (testDecodedCRC(preamble_index, sizeof(PACKET_PREAMBLE) - 2)) // verify the CRC of the preamble. don't crc the crc (-2)
							{
								// fix packet length for big endian <-> little endian
								preamble->length = SWAP_BYTES(preamble->length);

								//printf("[%s] CRC pass, proceed to CORE_SX_PACKET_DATA\n", name);
								//printf("[%s] DECODED DATA: ", name);
								// EB TODO: implement packet_data_init and packet_data_process
								rx_state = CORE_SX_PACKET_DATA;
								packet_data_index = preamble_index + sizeof(PACKET_PREAMBLE);
								packet_data = getData() + packet_data_index;
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
						printf("%x ", sx_char);
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

// *********************************************************************
// METHODS TO CREATE AND SEND SYSEX MESSAGES
// *********************************************************************

int16_t SysExMessage::sendSysExIDRequest(void)
{
	clear();
    uint8_t idReq[] = {MIDI_SX_START, SX_UNIVERSAL_NON_REALTIME, SX_ADDRESS, SX_UNV_GENERAL_INFO, SX_UNV_DEVID_REQ, MIDI_SX_STOP};
    array(idReq, sizeof(idReq));
	if (cb_tx_Send)
		return cb_tx_Send(context_tx, getData(), getSize()); 

	return SYX_SEND_RETURN_CODE_NO_SEND_FUNCTION;
}

int16_t SysExMessage::sendSysExIDReply(void)
{
	if (!cb_tx_Send)
		return SYX_SEND_RETURN_CODE_NO_SEND_FUNCTION;  

    uint8_t versionBL[] = {SYX_ID_BL_VER1, SYX_ID_BL_VER2, SYX_ID_BL_VER3}; 
    uint8_t versionAPP[] = {SYX_ID_APP_VER1, SYX_ID_APP_VER2, SYX_ID_APP_VER3};

    clear(); // safety
    
    array(deviceIDraw, sizeof(deviceIDraw)); 
    array(versionBL, sizeof(versionBL));
    array(versionAPP, sizeof(versionAPP));
    single(MIDI_SX_STOP);

    if (cb_tx_Send)
		return cb_tx_Send(context_tx, getData(), getSize()); 
		
	return SYX_SEND_RETURN_CODE_NO_SEND_FUNCTION;
}

// Create a KMI formatted sysex header for a given SysExMessage
void SysExMessage::makeSyxHeader(uint8_t targetPID)
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
    
    array(header, sizeof(header));
}


// Note: certain product firmware (like Dan's Riser Bootloader) requires that we flush the syx encoding after the preamble 
// and before the data payload. This option has to be handled by the application
int16_t SysExMessage::sendSyxFormattedMessage(uint8_t targetPID, uint8_t category, uint8_t type, uint8_t *ptr, uint16_t length)
{   

	if (!cb_tx_Send)
    	return SYX_SEND_RETURN_CODE_NO_SEND_FUNCTION; 

	int returnCode = SYX_SEND_RETURN_CODE_OK;

    reserve(SYX_BLOCK_SIZE);

    makeSyxHeader(targetPID);
    
    // begin 7/8bit encoding
    init_encode();
    init_crc();

    // preamble
    encode_crc_char(category);            // message category
    encode_crc_char(type);               // message type
    
    encode_crc_int(length + 2 + 2); // this is the length of the payload, plus 2 for length of next packet (int), +2 for crc (int)
    encode_int(getCRC());
    

    // payload
    if (length) 
    {
        init_crc();

        if (flushAfterPreamble == SYX_FLUSH_YES)
        {
            flush_encode(); // EM Pro Riser bootloader expects us to flush the encoding here, most other applications do not
        }

        returnCode = cb_tx_Send(context_tx, getData(), getSize()); // load the preamble into appropriate tx buffer
        clear();
        if (returnCode != SYX_SEND_RETURN_CODE_OK)
            return returnCode;

        uint8_t blockCount = 0;
        while(length--)
        {
            encode_crc_char(*ptr++);
            
            if (++blockCount >= SYX_BLOCK_SIZE)
            {
                returnCode = cb_tx_Send(context_tx, getData(), getSize()); // load the block into appropriate tx buffer
                blockCount = 0;
                clear();
                if (returnCode != SYX_SEND_RETURN_CODE_OK)
                    return returnCode;
            }
        }
        
        encode_crc_int(0);  // length of the next packet - if 0 then this is the last packet, if something
                                // other than zero, we can send additional blocks of data, ie multiple presets

        encode_int(getCRC()); // this is the crc of the payload
    }

    // end 7/8bit encoding
    flush_encode();
    single(MIDI_SX_STOP);

    returnCode = cb_tx_Send(context_tx, getData(), getSize()); // load the rest of the message into the tx buffer
	clear();
	return returnCode;
}