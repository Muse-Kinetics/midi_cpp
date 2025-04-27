/*
  ----------------------------------------------------------------------------
  File:        bytestream_parser.cpp
  Description: Receive a MIDI bytestream, put SysEx into a SysExMessage object,
  all other data gets constructed into Realtime and Channel messages.

  Copyright © 2025 KMI Music, Inc. All rights reserved.
  Unauthorized copying of this file, via any medium, is strictly prohibited.
  Proprietary and confidential.

  ----------------------------------------------------------------------------
*/

#include "MIDI_bytestream_parser.hpp"

MidiBytestreamParser::MidiBytestreamParser(SysExMessageRX* sysexObj)
    : syx(sysexObj) 
{
    reset();
}

bool MidiBytestreamParser::parse(uint8_t byte) 
{
	// single byte realtime messages can interrupt sysex or even channel messages. Handle them first and do not change state or running status.
	switch (byte) 
	{
		case MIDI_RT_UNDEF1:
		case MIDI_RT_UNDEF2:
		case MIDI_RT_UNDEF3:
		case MIDI_RT_UNDEF4:
			return false;
		case MIDI_TUNE_REQUEST:
		case MIDI_RT_CLOCK:
		case MIDI_RT_START:
		case MIDI_RT_CONTINUE:
		case MIDI_RT_STOP:
		case MIDI_RT_ACTIVE_SENSE:
		case MIDI_RT_RESET:
			msg[0] = byte;
			msgLen = 1;
			return true;
	}

	// handle sysex
    if (state == State::SYSEX)
	{
		if (byte == MIDI_SX_STOP)
		{
			state = State::WAIT_STATUS;
			if (syx != nullptr)
				syx->sx_process(&byte, 1);
			return false;
		}
		else if (!(byte & 0x80)) // more sysex
		{
			if (syx != nullptr)
                syx->sx_process(&byte, 1);
			return false;
		}
        else // any other status byte during sysex is an error, break sysex transmission and process below
        {
            runningStatus = 0;
			state = State::WAIT_STATUS;
		}
    }

    
    if (byte & STATUS_BYTE_MASK) 
	{
        msg[0] = byte;
        msgIndex = 1;

        if (byte == MIDI_SX_START) {
            state = State::SYSEX;

            if (syx != nullptr)
                syx->sx_process(&byte, 1);

            return false;
        }

        state = State::READ_DATA;
        runningStatus = byte;

        switch (byte) {
            case MIDI_MTC:
            case MIDI_SONG_SELECT:
                expectedLength = 2;
                break;
            case MIDI_SONG_POSITION:
                expectedLength = 3;
                break;
            default:
                switch (byte & 0xF0) {
                    case MIDI_PROG_CHANGE:
                    case MIDI_CHANNEL_PRESSURE:
                        expectedLength = 2;
                        break;
                    case MIDI_NOTE_OFF:
                    case MIDI_NOTE_ON:
                    case MIDI_NOTE_AFTERTOUCH:
                    case MIDI_CONTROL_CHANGE:
                    case MIDI_PITCH_BEND:
                        expectedLength = 3;
                        break;
                    default:
                        expectedLength = 3; // shouldn't ever go here
                        break;
                }
                break;
        }

        return false;
    }

    // Support running status: treat incoming data as continuation of previous channel message
    if (state == State::READ_DATA || (state == State::WAIT_STATUS && runningStatus != 0)) 
    {
        if (state == State::WAIT_STATUS && runningStatus != 0) {
            msg[0] = runningStatus;
            msgIndex = 1;
            state = State::READ_DATA;

            switch (runningStatus & 0xF0) {
                case MIDI_PROG_CHANGE:
                case MIDI_CHANNEL_PRESSURE:
                    expectedLength = 2;
                    break;
                case MIDI_NOTE_OFF:
                case MIDI_NOTE_ON:
                case MIDI_NOTE_AFTERTOUCH:
                case MIDI_CONTROL_CHANGE:
                case MIDI_PITCH_BEND:
                    expectedLength = 3;
                    break;
                default:
                    expectedLength = 3;
                    break;
            }
        }

        if (msgIndex < sizeof(msg))
            msg[msgIndex++] = byte;

        if (msgIndex >= expectedLength) {
            msgLen = expectedLength;
            msgIndex = 1;
            return true;
        }
    }

    return false;
}

void MidiBytestreamParser::reset()
{
    state = State::WAIT_STATUS;
    msgIndex = 0;
    msgLen = 0;
    expectedLength = 0;
    runningStatus = 0;
}