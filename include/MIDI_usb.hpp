#ifndef MIDI_USB_H
#define MIDI_USB_H

/*
  ----------------------------------------------------------------------------
  File:        MIDI_usb.hpp
  Description: Common enums, structs, and constants for handling USB MIDI messages

  Copyright (c) 2026 KMI Music, Inc.
  SPDX-License-Identifier: MIT

  Author: Eric Bateman <eric@musekinetics.com>

  ----------------------------------------------------------------------------
*/


#define	USB_TX_TIMEOUT_COUNT  	100  // this will need to be decremeted somewhere

//These cable index numbers are described in section 4 of the USB MIDI Device Class Def
#define	CIN0	0	//reserved
#define	CIN1	1	//reserved
#define	CIN2	2	//two byte system common message
#define	CIN3	3	//three byte system common message
#define	CIN4	4	//sysex starts or continues, three bytes
#define	CIN5	5	//one byte system common, or sysex ends with following single byte
#define	CIN6	6   //sysex ends with following two bytes
#define	CIN7	7	//sysex ends with following three bytes
#define	CIN8	8	//Note Off, three bytes
#define	CIN9	9   //Note On, three bytes
#define	CIN10	0x0A	//Poly KeyPress, three bytes
#define	CIN11	0x0B	//control change, three bytes
#define	CIN12	0x0C	// program change, two bytes
#define	CIN13	0x0D	// channel pressure, two bytes
#define	CIN14	0x0E	// pitch bend, three bytes
#define	CIN15	0x0F	// single byte is implemented, could be system common

#define	CIN_MASK	0x0F

#define CABLE_CODE_NTOF		CIN8
#define CABLE_CODE_NTON		CIN9
#define CABLE_CODE_POLY		CIN10
#define CABLE_CODE_CC		CIN11
#define CABLE_CODE_PGM		CIN12
#define CABLE_CODE_PRSS		CIN13
#define CABLE_CODE_PTCH		CIN14


enum 
{
    USB_MSG_HEADER,
    USB_MSG_DATUM1,
    USB_MSG_DATUM2,
    USB_MSG_DATUM3,
    USB_MSG_NUM_BYTES
};

/* ******************************************************************************************/
/* These are the parts and whole of the new and improved MIDI_USB_MESSAGE struct ************/
/* ******************************************************************************************/

typedef struct {
    unsigned char CIN : 4;   // code index number
    unsigned char cable : 4; // usb midi port 0-15, has to be defined in descriptor
    unsigned char datums[3]; // Bytes 1-3: typically status and two data bytes
} MIDI_TYPE_USB_MSG_RAW;

typedef struct {
    union {
        unsigned char statusByte; // MIDI status byte = status | channel
        struct {
            unsigned char channel : 4;
            unsigned char status : 4; // 4bit status opcode without channel info (ie 8 for channel)
        };
    };
    unsigned char dataByte1, dataByte2;
} MIDI_TYPE_MIDI_MSG_RAW;

typedef struct {
    unsigned char channel : 4;
    unsigned char status : 4;
    unsigned char note, velocity;
} MIDI_TYPE_NOTE_ON;

typedef struct {
    unsigned char channel : 4;
    unsigned char status : 4;
    unsigned char note, after_touch;
} MIDI_TYPE_NOTE_OFF;

typedef struct {
    unsigned char channel : 4;
    unsigned char status : 4;
    unsigned char note, pressure;
} MIDI_TYPE_POLY_PRESSURE;

typedef struct {
    unsigned char channel : 4;
    unsigned char status : 4;
    unsigned char cnum, val;
} MIDI_TYPE_CONTROLLER;

typedef struct {
    unsigned char channel : 4;
    unsigned char status : 4;
    unsigned char program;
} MIDI_TYPE_PROGRAM_CHANGE;

typedef struct {
    unsigned char channel : 4;
    unsigned char status : 4;
    unsigned char pressure;
} MIDI_TYPE_CHANNEL_PRESSURE;

typedef struct {
    unsigned char channel : 4;
    unsigned char status : 4;
    unsigned char lsb, msb;
} MIDI_TYPE_PITCH_WHEEL;

typedef union {
    struct { unsigned char d1, d2, d3; } sysEx;
    struct { unsigned char status, d1, d2; } MTC;
    struct { unsigned char status, lsb, msb; } songPositionPointer;
    struct { unsigned char status, song; } songSelect;
} MIDI_TYPE_SYS_COMMON;

typedef struct {
    unsigned char usbHeader;

    union {
        MIDI_TYPE_MIDI_MSG_RAW raw;
        MIDI_TYPE_NOTE_ON noteOn;
        MIDI_TYPE_NOTE_OFF noteOff;
        MIDI_TYPE_POLY_PRESSURE polyPressure;
        MIDI_TYPE_CONTROLLER controller;
        MIDI_TYPE_PROGRAM_CHANGE programChange;
        MIDI_TYPE_CHANNEL_PRESSURE channelPressure;
        MIDI_TYPE_PITCH_WHEEL pitchWheel;
        MIDI_TYPE_SYS_COMMON sysCommon;
    };
} MIDI_TYPE_MIDI;

typedef struct {
    union {
        MIDI_TYPE_USB_MSG_RAW usb;
        MIDI_TYPE_MIDI midi;
    };
} __attribute__((packed)) MIDI_USB_MESSAGE;



/* ******************************************************************************************/
/* ******************************************************************************************/

#endif/* MIDI_USB_H */
