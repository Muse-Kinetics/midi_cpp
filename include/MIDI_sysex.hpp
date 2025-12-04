#ifndef MIDI_SYSEX_H
#define MIDI_SYSEX_H

/*
  ----------------------------------------------------------------------------
  File:        MIDI_sysex.hpp
  Description: Methods to encode and decode KMI formatted SysEx Messages

  Copyright © 2025 KMI Music, Inc. All rights reserved.
  Unauthorized copying of this file, via any medium, is strictly prohibited.
  Proprietary and confidential.

  ----------------------------------------------------------------------------
*/

#ifdef __cplusplus
/* C Includes  *************************************************************************************/
extern "C" 
{
#endif
#ifdef __cplusplus
}
#endif

/*****************************************************************************************************/


/* C++ Includes  *************************************************************************************/
#include <cstdint>
#include <cstring>
#include <cstddef>

#include "MIDI_CPP_config.hpp"
#include "MIDI.hpp"
#include "MIDI_device_metadata.hpp"

/*****************************************************************************************************/


//------------------------------------------------
//	defines, constants, and enums
//------------------------------------------------

#define SWAP_BYTES(x) (((x & 0xFF) << 8) | ((x & 0xFF00) >> 8))

#define SX_UNIVERSAL_NON_REALTIME	0x7E // see "Universal System Exclusive Messages.pdf"
#define SX_UNIVERSAL_REALTIME	    0x7F // see "Universal System Exclusive Messages.pdf"

#define SX_ADDRESS_ALL		        0x7F // Devices can be assigned a "SysEx ID", 0x7F is reserved for all devices

// sysex universal sub-id #1
#define SX_UNV_GENERAL_INFO		    0x06 // general info request (device id)

// sysex universal sub-id #2
#define SX_UNV_DEVID_REQ		    0x01 // device id request
#define SX_UNV_DEVID_REPLY		    0x02 // device id reply

// 8bit <> 7bit encoding
#define	SX_PACKET_START	            0x01 // magic number in a sysex packet indicating that the rest of the packet will be encoded 7bit<=>8bit
#define SX_ENCODE_LEN	            0x07 // 7 encoded bytes followed by an 8th byte that encodes the high bits of those 7 bytes

enum SYX_FLUSH
{
    SYX_FLUSH_NO,
    SYX_FLUSH_YES
};

// state machine for processing incoming sysex payloads
enum SYX_RX_STATE
{
	CORE_SX_HEADER,
    CORE_SX_ID_REPLY,
	CORE_SX_PACKET_START_SEARCH,
	CORE_SX_PACKET_PREAMBLE,
	CORE_SX_PACKET_DATA,
    CORE_SX_HOST_DATA,
    CORE_SX_RAW_DATA,
    CORE_SX_IGNORE
};

enum SYX_FORMAT
{
    SYX_FORMAT_KMI,
    SYX_FORMAT_DANS_EDITOR_MESSAGE, // could also be OSC for EM1
    SYX_FORMAT_NO_ENCODING
};

//------------------------------------------------
//	Structs
//------------------------------------------------

// Standard SysEx
typedef struct
{
    uint8_t mfg_id[3],
            prod_id[2],     // LSB / MSB
            family_id[2];   // LSB / MSB
} SYSEX_DEVICE_METADATA;

typedef struct 
{
	uint8_t 	syx_universal, // either realtime or non-realtime
                syx_device_id, // 0x7F = send to all devices, kmi products should ignore this byte     
				sub_id_1,
				sub_id_2;
} SYSEX_UNIVERSAL_HEADER;

typedef struct 
{
	SYSEX_UNIVERSAL_HEADER hdr;
    SYSEX_DEVICE_METADATA metadata;  
    uint8_t bl_ver[3], app_ver[3];
} SYSEX_DEVICE_INQUIRY_REPLY;

typedef union
{
    SYSEX_DEVICE_INQUIRY_REPLY reply;
    uint8_t raw[sizeof(SYSEX_DEVICE_INQUIRY_REPLY)];  // Make sure the array size matches the struct size
} SYSEX_DEVICE_INQUIRY_UNION; 

// This is the KMI standard sysex header
typedef struct 
{
	uint8_t 	manufacturer_id1,
                manufacturer_id2,
                manufacturer_id3,
                product_msb,
                product,
                format;
} SYSEX_STANDARD;

// KMI SysEx structs


typedef struct PACKET_PREAMBLE 
{
    uint8_t category, type;
    uint16_t length, crc;
} PACKET_PREAMBLE;


// SYSEX_HANDLER is a struct to define multiple sysex messages and their methods
typedef struct {
	void *data_header_ptr;                          // pointer to where we write any incoming data payloads
	uint8_t data_header_len;                  // the length of the data
    uint8_t (*open)(void);                    // a function called when the packet is opened, returns a uchar
	void (*datum)(uint8_t schar);             // a function to write schar to flash
	void (*close)(uint8_t success);           // a functino called when the packet is closed
} SYSEX_HANDLER;

//Tail contains checksum and length information for verifying packet validity
typedef union 
{
	uint8_t raw[4];
	struct 
    {
        uint16_t length, crc;
    } fmt; 
} TAIL;	   //4 bytes total

// this is a single struct used in sysex rx to manage info about the incoming packet
typedef struct 
{
    uint8_t index;
	uint8_t *header;			
	uint8_t packet_count;
	SYSEX_HANDLER *sysex_handler;
	TAIL tail;
} PACKET_DATA_INFO;

// encode/decode

typedef struct 
{
	uint8_t 	index_in,
					index_out,
					buf[SX_ENCODE_LEN+1];
} CORE_SX_DECODE;


//------------------------------------------------
//	Externs and Variables
//------------------------------------------------
 


//------------------------------------------------
//	function prototypes
//------------------------------------------------

//------------------------------------------------
//	classes
//------------------------------------------------

//----------------------------------------
// Callback types
//----------------------------------------

using VoidCallback = void (*)(void* ctx);
using SendCallback = int16_t (*)(void* ctx, uint8_t* data, uint16_t length);
using HostMessageCallback = void (*)(void* ctx, uint8_t msg_type, uint8_t data_val, uint16_t int_val);
using PacketDataCallback = void (*)(void* ctx, PACKET_PREAMBLE* preamble, uint8_t* packet_data);
using IDReplyCallback = void (*)(void* ctx, SYSEX_DEVICE_INQUIRY_REPLY* reply);
using DebugPrintCallback = void (*)(void* ctx, const char* string);


//----------------------------------------
// TX Class
//----------------------------------------

class SysExMessageTX {
    public:
        SysExMessageTX();
    
        void setCB_DebugPrint_Context(void* ctx) { context_dp = ctx; }
        void setCB_DebugPrint(DebugPrintCallback cb) { cb_debugPrint = cb; }

        void setCB_tx_Context(void* ctx) { context_tx = ctx; }
        void setCB_send(SendCallback cb) { cb_tx_Send = cb; }
        void setFlush(SYX_FLUSH flush) { flushAfterPreamble = flush; }
    
        int16_t makeSyxHeader(uint8_t targetPID);
        int16_t sendSysExIDRequest();
        int16_t sendSysExIDReply();
        int16_t sendSyxFormattedMessage(uint8_t targetPID, uint8_t category, uint8_t type, uint8_t* ptr, uint16_t length);
        int16_t sendSyxUnEncodedMessage(uint8_t targetPID, uint8_t category, uint8_t type, uint8_t* ptr, uint16_t length);

        uint8_t* getData() { return buffer; };
        size_t getSize() const { return size; };
        uint16_t getCRC() const { return crc; }

        void clear();
        int16_t single(uint8_t byte); // pass return code to the callback
        int16_t array(const uint8_t* bytes, size_t length);

        void init_encode();
        void init_crc();
        int16_t encode_char(uint8_t val);
        int16_t encode_crc_char(uint8_t val);
        int16_t encode_int(uint16_t val);
        int16_t encode_crc_int(uint16_t val);
        int16_t flush_encode();


    private:
        uint8_t buffer[SYX_TX_BLOCK_SIZE];
        size_t size = 0;
        uint16_t crc = 0;
        uint8_t midi_hi_bits = 0;
        uint8_t midi_hi_count = 0;
        SYX_FLUSH flushAfterPreamble = SYX_FLUSH_NO;
    
        void* context_tx = nullptr;
        void* context_dp = nullptr;
        SendCallback cb_tx_Send = nullptr;
        DebugPrintCallback cb_debugPrint = nullptr;
    

   };
    
//----------------------------------------
// RX Class
//----------------------------------------
    
class SysExMessageRX {
    public:
        SysExMessageRX(SysExMessageTX* syxSend = nullptr);
    
        void setCB_rx_Context(void* ctx) { context_rx = ctx; }
        void setCB_DebugPrint_Context(void* ctx) { context_dp = ctx; }

        void setCB_DebugPrint(DebugPrintCallback cb) { cb_debugPrint = cb; }
        void setCB_rx_ActiveSense(VoidCallback cb) { cb_rx_ActiveSense = cb; } // EMC requires we check this in the sysex handler, the bytestream object will take priority if implemented
        void setCB_rx_IDRequest(VoidCallback cb) { cb_rx_id_request = cb; }
        void setCB_rx_IDReply(IDReplyCallback cb) { cb_rx_id_reply = cb; }
        void setCB_rx_HostMessage(HostMessageCallback cb) { cb_rx_HostMessage = cb; }
        void setCB_rx_PacketData(PacketDataCallback cb) { cb_rx_PacketData = cb; }
    
        void setSendPtr(SysExMessageTX* syxSend) { syxSendPtr = syxSend; }
        void rx_init();
        void rx_set_ignore();
        void setFlush(SYX_FLUSH flush) { flushAfterPreamble = flush; }

        void single(uint8_t byte);
        void array(const uint8_t* bytes, size_t length);

        void updatePointers();
        void sx_process(uint8_t* msg, uint16_t length);

        SYSEX_UNIVERSAL_HEADER* headerUniv;
        SYSEX_STANDARD* headerStd;

        uint8_t* getData() { return buffer; };
        size_t getSize() const { return size; };
        uint16_t getCRC() const { return crc; }

        void clear();
        void init_crc();
        void init_decode();
        void decode_put(uint8_t val);
        bool decode_get(uint8_t* val);
        bool testDecodedCRC(uint16_t startIndex, uint16_t length);
        SYX_RX_STATE rx_state;

    private:
        SysExMessageTX* syxSendPtr = nullptr;
        uint8_t buffer[SYX_RX_BLOCK_SIZE];
        size_t size = 0;
        SYX_FLUSH flushAfterPreamble = SYX_FLUSH_NO;
    
        
        bool rx_decode_active;
        uint16_t rx_decode_count;
        bool ignore_rx;
    
        CORE_SX_DECODE core_sx_decode;
        uint16_t crc;
    
        uint8_t preamble_index;
        PACKET_PREAMBLE* preamble;
    
        uint16_t packet_data_index;
        uint8_t* packet_data;
    
        
    
        void* context_rx = nullptr;
        void* context_dp = nullptr;
    
        VoidCallback cb_rx_ActiveSense = nullptr;
        VoidCallback cb_rx_id_request = nullptr;
        IDReplyCallback cb_rx_id_reply = nullptr;
        HostMessageCallback cb_rx_HostMessage = nullptr;
        PacketDataCallback cb_rx_PacketData = nullptr;
        DebugPrintCallback cb_debugPrint = nullptr;
    
        
    };
#endif/* MIDI_SYSEX_H */
