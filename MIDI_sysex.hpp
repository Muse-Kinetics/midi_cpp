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
    #include "utils.h"
#ifdef __cplusplus
}
#endif

/*****************************************************************************************************/


/* C++ Includes  *************************************************************************************/
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstddef>

#include "MIDI_CPP_config.hpp"
#include "MIDI.hpp"
#include "MIDI_device_metadata.hpp"

/*****************************************************************************************************/


//------------------------------------------------
//	defines, constants, and enums
//------------------------------------------------

#define SYX_MSG_SIZE 100

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
    CORE_SX_IGNORE
};

enum SYX_FORMAT
{
    SYX_FORMAT_KMI,
    SYX_FORMAT_DANS_EDITOR_MESSAGE
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
    uint8_t app_ver[3], bl_ver[3];  
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

typedef struct 
{
    uint8_t debug[45];
} DEBUG_ARRAY;

//------------------------------------------------
//	Externs and Variables
//------------------------------------------------
 


//------------------------------------------------
//	function prototypes
//------------------------------------------------

//------------------------------------------------
//	classes
//------------------------------------------------

class SysExMessage {

public:
	SysExMessage();
	~SysExMessage();


    // --- Callback types ---
    using VoidCallback = void (*)(void* ctx);
    using SendCallback = int16_t (*)(void* ctx, uint8_t* data, uint16_t length);
    using HostMessageCallback = void (*)(void* ctx, uint8_t msg_type, uint8_t data_val, uint16_t int_val);
    using PacketDataCallback = void (*)(void* ctx, PACKET_PREAMBLE *preamble, uint8_t *packet_data);
    using IDReplyCallback = void (*)(void* ctx, SYSEX_DEVICE_INQUIRY_REPLY *reply);
    using DebugPrintCallback = void(*)(void* ctx, char *string);

    // --- Callback setters ---
    void setCB_rx_Context(void *ctx) { context_rx = ctx; }
    void setCB_tx_Context(void *ctx) { context_rx = ctx; }

    void setCB_debugPrint_Context(void *ctx) { context_dp = ctx; }
    void setCB_debugPrint(DebugPrintCallback cb) { cb_debugPrint = cb; }

    void setCB_rx_ActiveSense(VoidCallback cb) { cb_rx_ActiveSense = cb; }
    void setCB_rx_id_request(VoidCallback cb) { cb_rx_id_request = cb; }
    void setCB_rx_id_reply(IDReplyCallback cb) { cb_rx_id_reply = cb; }

    void setCB_rx_HostMessage(HostMessageCallback cb) { cb_rx_HostMessage = cb; }
    void setCB_rx_PacketData(PacketDataCallback cb) { cb_rx_PacketData = cb; }
    void setCB_tx_send(SendCallback func) { cb_tx_Send = func; }


    // --- add data to the buffer ---
    void single(uint8_t byte);
    void array(const uint8_t* bytes, size_t length);

    // --- manage the buffer ---
    void reserve(size_t size);
	size_t getSize() const;
	uint8_t* getData();
    DEBUG_ARRAY *d;
	void clear();



    //------------------------
    // RX methods
    //------------------------

    SYX_RX_STATE rx_state;
    bool rx_decode_active;
    uint16_t rx_decode_count;
    bool ignore_rx;

    uint8_t preamble_index;
    PACKET_PREAMBLE *preamble;

    uint16_t packet_data_index;
    uint8_t *packet_data;

    void rx_init(void);
    void rx_set_ignore(void);

    void sx_process(uint8_t *msg, uint16_t length);


    //------------------------
    // TX methods
    //------------------------


    int16_t sendSysExIDRequest(void);
    int16_t sendSysExIDReply(void);
    
    void makeSyxHeader(uint8_t targetPID);

    void setFlush(SYX_FLUSH flush) { flushAfterPreamble = flush; };
    int16_t sendSyxFormattedMessage(uint8_t targetPID, uint8_t category, uint8_t type, uint8_t *ptr, uint16_t length);

    //------------------------
    // Encode 8 bits to 7 bits
    //------------------------

	void init_crc(void);
	uint16_t getCRC();

    void init_encode(void);
	void encode_char(uint8_t val);
    void encode_int(uint16_t val);
    void encode_crc_int(uint16_t val);
    void encode_crc_char(uint8_t val);
	void flush_encode(void);

	//------------------------
    // Decode 7bits to 8 bits
    //------------------------

    void init_decode(void);
    void decode_put(uint8_t val);
    bool decode_get(uint8_t *val);

	bool testDecodedCRC(uint16_t startIndex, uint16_t length);

    void updatePointers();
    SYSEX_UNIVERSAL_HEADER *headerUniv;
    SYSEX_STANDARD *headerStd;
    

private:
	std::vector<uint8_t> message;    // Message buffer
	uint16_t crc;
    uint8_t midi_hi_bits;
    uint8_t midi_hi_count;

	CORE_SX_DECODE core_sx_decode;

    SYX_FLUSH flushAfterPreamble; 

    void* context_rx = nullptr;
    void* context_tx = nullptr;
    void* context_dp = nullptr;

    DebugPrintCallback cb_debugPrint = nullptr;

    SendCallback cb_tx_Send = nullptr;
    VoidCallback cb_rx_ActiveSense = nullptr;
    VoidCallback cb_rx_id_request = nullptr;
    IDReplyCallback cb_rx_id_reply = nullptr;
    HostMessageCallback cb_rx_HostMessage = nullptr;
    PacketDataCallback cb_rx_PacketData = nullptr;
};

#endif/* MIDI_SYSEX_H */
