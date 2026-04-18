#ifndef MIDI_INCLUDED
#define	MIDI_INCLUDED

/*
  ----------------------------------------------------------------------------
  File:        MIDI.hpp
  Description: Common enums, structs, and constants for handling MIDI messages

  Copyright (c) 2026 KMI Music, Inc.
  SPDX-License-Identifier: MIT

  Author: Eric Bateman <eric@musekinetics.com>

  ----------------------------------------------------------------------------
*/
enum
{
	MIDI_CH_1,		// 0
	MIDI_CH_2,
	MIDI_CH_3,
	MIDI_CH_4,
	MIDI_CH_5,
	MIDI_CH_6,
	MIDI_CH_7,
	MIDI_CH_8,
	MIDI_CH_9,
	MIDI_CH_10,
	MIDI_CH_11,
	MIDI_CH_12,
	MIDI_CH_13,
	MIDI_CH_14,
	MIDI_CH_15,
	MIDI_CH_16,
	NUM_MIDI_CHANNELS // 16
};

// use these when assigning to a 4 bit "status" nibble
#define MIDI_STATUS_NOTE_OFF          0x08
#define MIDI_STATUS_NOTE_ON           0x09
#define MIDI_STATUS_NOTE_AFTERTOUCH   0x0A
#define MIDI_STATUS_CONTROL_CHANGE    0x0B
#define MIDI_STATUS_PROG_CHANGE       0x0C
#define MIDI_STATUS_CHANNEL_PRESSURE  0x0D
#define MIDI_STATUS_PITCH_BEND        0x0E
#define MIDI_STATUS_SYS_COMMON        0x0F 

#define	MIDI_NOTE_OFF			0x80
#define	MIDI_NOTE_ON			0x90
#define	MIDI_NOTE_AFTERTOUCH	0xA0
#define	MIDI_CONTROL_CHANGE		0xB0
#define	MIDI_PROG_CHANGE		0xC0
#define	MIDI_CHANNEL_PRESSURE	0xD0
#define	MIDI_PITCH_BEND			0xE0
#define MIDI_SYS_COMMON			0xF0 // alias for code readability

#define MIDI_SYSTEM				0xF0
#define	MIDI_SX_START			0xF0
#define	MIDI_MTC				0xF1
#define	MIDI_SONG_POSITION		0xF2
#define	MIDI_SONG_SELECT		0xF3
#define MIDI_RT_UNDEF1			0xF4
#define MIDI_RT_UNDEF2			0xF5
#define	MIDI_TUNE_REQUEST		0xF6
#define	MIDI_SX_STOP			0xF7
#define MIDI_RT_CLOCK			0xF8
#define	MIDI_RT_UNDEF3			0xF9
#define MIDI_RT_START			0xFA
#define	MIDI_RT_CONTINUE		0xFB
#define	MIDI_RT_STOP			0xFC
#define	MIDI_RT_UNDEF4			0xFD
#define	MIDI_RT_ACTIVE_SENSE	0xFE
#define	MIDI_RT_RESET			0xFF

#define RPN_NULL				127	
#define RPN_PB_RANGE			0
#define RPN_TUNE_FINE			1
#define RPN_TUNE_COARSE			2
#define RPN_TUNE_PROG			3
#define RPN_TUNE_BANK			4
#define RPN_MCM					6
  
  
#define RESET_ALL_CONTROLLERS		0, 121
#define	MCM_RPN_LSB					6, 100
#define MCM_RPN_MSB					0, 101
#define CC_DATA_ENTRY				6
#define MPE_LOWERZ_MASTERCH			0
#define MPE_UPPERZ_MASTERCH			15

#define MPE_MANAGER_CHANNEL MIDI_CH_1
#define NUM_MPE_MEMBER_CHANNELS 15

#define MIDI_MAX_7BIT_INDEXES		128
#define	NOTE_VELOCITY_DEFAULT		0x40
#define MIDI_PITCH_BEND_CENTER_14B	8192
#define MIDI_PITCH_BEND_MAX_14B		16383

// Standard MIDI Control Change numbers 
#define MIDI_CC_BANK_SELECT_MSB 0
#define MIDI_CC_MODULATION_WHEEL 1
#define MIDI_CC_BREATH_CONTROL 2
#define MIDI_CC_3 3
#define MIDI_CC_FOOT_CONTROLLER 4
#define MIDI_CC_PORTAMENTO_TIME 5
#define MIDI_CC_DATA_MSB 6
#define MIDI_CC_CHANNEL_VOLUME 7
#define MIDI_CC_BALANCE 8
#define MIDI_CC_PAN 10
#define MIDI_CC_EXPRESSION_CONTROLLER 11
#define MIDI_CC_EFFECT_CONTROL_1 12
#define MIDI_CC_EFFECT_CONTROL_2 13
#define MIDI_CC_14 14
#define MIDI_CC_15 15
#define MIDI_CC_GENERAL_PURPOSE_CONTROLLER_1 16
#define MIDI_CC_GENERAL_PURPOSE_CONTROLLER_2 17
#define MIDI_CC_GENERAL_PURPOSE_CONTROLLER_3 18
#define MIDI_CC_GENERAL_PURPOSE_CONTROLLER_4 19
#define MIDI_CC_BANK_SELECT_LSB 32
#define MIDI_CC_MODULATION_WHEEL_LSB 33
#define MIDI_CC_BREATH_CONTROL_LSB 34
#define MIDI_CC_FOOT_CONTROLLER_LSB 36
#define MIDI_CC_PORTAMENTO_TIME_LSB 37
#define MIDI_CC_DATA_LSB 38
#define MIDI_CC_CHANNEL_VOLUME_LSB 39
#define MIDI_CC_BALANCE_LSB 40
#define MIDI_CC_PAN_LSB 42
#define MIDI_CC_EXPRESSION_CONTROLLER_LSB 43
#define MIDI_CC_EFFECT_CONTROL_1_LSB 44
#define MIDI_CC_EFFECT_CONTROL_2_LSB 45
#define MIDI_CC_DAMPER_PEDAL 64
#define MIDI_CC_PORTAMENTO 65
#define MIDI_CC_SUSTENUTO 66
#define MIDI_CC_SOFT_PEDAL 67
#define MIDI_CC_LEGATO_FOOTSWITCH 68
#define MIDI_CC_HOLD_2 69
#define MIDI_CC_SOUND_CONTROLLER_1 70
#define MIDI_CC_SOUND_CONTROLLER_2 71
#define MIDI_CC_SOUND_CONTROLLER_3 72
#define MIDI_CC_SOUND_CONTROLLER_4 73
#define MIDI_CC_SOUND_CONTROLLER_5 74
#define MIDI_CC_SOUND_CONTROLLER_6 75
#define MIDI_CC_SOUND_CONTROLLER_7 76
#define MIDI_CC_SOUND_CONTROLLER_8 77
#define MIDI_CC_SOUND_CONTROLLER_9 78
#define MIDI_CC_SOUND_CONTROLLER_10 79
#define MIDI_CC_GENERAL_PURPOSE_CONTROLLER_5 80
#define MIDI_CC_GENERAL_PURPOSE_CONTROLLER_6 81
#define MIDI_CC_GENERAL_PURPOSE_CONTROLLER_7 82
#define MIDI_CC_GENERAL_PURPOSE_CONTROLLER_8 83
#define MIDI_CC_PORTAMENTO_CONTROL 84
#define MIDI_CC_EFFECTS_1_DEPTH 91
#define MIDI_CC_EFFECTS_2_DEPTH 92
#define MIDI_CC_EFFECTS_3_DEPTH 93
#define MIDI_CC_EFFECTS_4_DEPTH 94
#define MIDI_CC_EFFECTS_5_DEPTH 95
#define MIDI_CC_DATA_INC 96
#define MIDI_CC_DATA_DEC 97
#define MIDI_CC_NRPN_LSB 98
#define MIDI_CC_NRPN_MSB 99
#define MIDI_CC_RPN_LSB 100
#define MIDI_CC_RPN_MSB 101
#define MIDI_CC_ALL_SOUND_OFF 120
#define MIDI_CC_RESET_ALL_CONTROLLERS 121
#define MIDI_CC_LOCAL_CONTROL 122
#define MIDI_CC_ALL_NOTES_OFF 123
#define MIDI_CC_OMNI_OFF 124
#define MIDI_CC_OMNI_ON 125
#define MIDI_CC_POLY_MODE_OFF 126
#define MIDI_CC_POLY_MODE_ON 127



inline static uint8_t expectedBytesForStatus(uint8_t status)
{
	switch (status & 0xF0)
	{
		case MIDI_NOTE_OFF:
		case MIDI_NOTE_ON:
		case MIDI_NOTE_AFTERTOUCH:
		case MIDI_CONTROL_CHANGE:
		case MIDI_PITCH_BEND:
			return 3; // status + 2 data bytes

		case MIDI_PROG_CHANGE:
		case MIDI_CHANNEL_PRESSURE:
			return 2; // status + 1 data byte

		case MIDI_SYSTEM:
			switch (status)
			{
				case MIDI_SX_START: // SysEx start
					return 0; // variable length, terminated by F7
				case MIDI_MTC: // MIDI Time Code Quarter Frame
				case MIDI_SONG_SELECT:
					return 2; // status + 1 data byte
				case MIDI_SONG_POSITION:
					return 3; // status + 2 data bytes
				case MIDI_TUNE_REQUEST:
				case MIDI_SX_STOP:
				case MIDI_RT_CLOCK:
				case MIDI_RT_START:
				case MIDI_RT_CONTINUE:
				case MIDI_RT_STOP:
				case MIDI_RT_ACTIVE_SENSE:
				case MIDI_RT_RESET:
					return 1; // just the status byte
				default:
					return 1; // unknown system message, assume no data bytes
			}
		default:
			return 1; // unknown message, assume no data bytes
	}
}

#endif //MIDI_INCLUDED
