// Copyright (c) 2026 KMI Music, Inc.
// SPDX-License-Identifier: MIT
// Author: Eric Bateman <eric@musekinetics.com>

#ifndef UTILS_CRC_H
#define UTILS_CRC_H

#include <stdint.h>

//*****************************************************************************
//* CRC Utility Functions
//* Version: 1.1 - 02/01/2024 - EB
//*****************************************************************************
//
// Description:
// - Functions to compute a CRC over a byte stream.
// - The CRC is initialized to 0xFFFF.
// - Designed to preserve the lower byte while mixing the MSB thoroughly.
//
//*****************************************************************************

inline void crc_init(uint16_t *this_crc)
{
    *this_crc = 0xFFFF;
}

// WARNING: CRC checks are dependent on system architecture (8bit, 32bit, little/big endian). 
// Legacy KMI devices that crc was implemented under were 8bit little-endian, mismatches in
// architecture can cause issues during the calculation below, not just in the final result.
inline void crc_byte(uint16_t *this_crc, uint8_t val)
{
	uint16_t temp;
	uint16_t quick;
    uint16_t crc_val = *this_crc;

	//printf("crc input: %d val: %d ", *this_crc, val);

							    			    //if we represent crc at start as 0xHHLL
	temp = ((crc_val >> 8) ^ val) & 0xFFFF;     //xor 8 bit val with upper byte of crc (0x00HH ^ val) = 0x00XX

	crc_val = (crc_val << 8) & 0xFFFF;		    // left shift crc now 0xLL00
	quick = (temp ^ (temp >> 4)) & 0xFFFF;		// 0x00XX ^ 0x000X = 0x00XY
	crc_val = (crc_val ^ (quick)) & 0xFFFF;  	// 0xLL00 ^ 0x00XY = 0xLLXY 

											    //effect of all this is to preserve the information in  
											    //LSB (LL) intact, while mixing the new data and the old MSB thoroughly

	quick = (quick << 5) & 0xFFFF;				//hash	(0x00XY << 5) = 0xNNN0	 (quick * 2 to the fifth)
	crc_val = (crc_val ^ quick) & 0xFFFF;		//hash

	quick = (quick << 7) & 0xFFFF;				//hash	(0xNNN0 << 7) = 0xN000	 (quick * 2 to the seventh)
	crc_val = (crc_val ^ quick) & 0xFFFF; 		//hash

	*this_crc = crc_val;

	//printf("output %d\n", crc_val);
}

inline void crc_append_buf(uint16_t *this_crc, const uint8_t *ptr, uint16_t length)
{
    while (length--)
    {
        crc_byte(this_crc, *ptr++);
    }
}

inline uint16_t crc_buf(uint16_t *this_crc, const uint8_t *ptr, uint16_t length)
{
    crc_init(this_crc);
    crc_append_buf(this_crc, ptr, length);
    return *this_crc;
}

//*****************************************************************************
//* Legacy signed-char CRC variant
//*****************************************************************************
//
// The pre-bootloader, v93-era 8051 KMI firmware (e.g. the original SoftStep /
// 12 Step "bootloader trojan horse" images) computed this same CRC with a
// *signed* char, so every byte with the high bit set (>= 0x80) was
// sign-extended before the XOR - effectively mixing an extra 0xFF00 into the
// accumulator relative to the unsigned crc_byte() above. This is precisely the
// architecture hazard the WARNING on crc_byte() calls out.
//
// The result: crc_byte() (unsigned) verifies every modern KMI product, but
// does NOT reproduce a legacy image's checksums on any payload containing a
// high-bit byte (small/low-value payloads happen to match under both). Host
// tools that need to decode/verify - or re-encode - firmware captured from, or
// destined for, those legacy products must use this signed variant to match
// what the device's own crc_byte() produced. On-device firmware should keep
// using the plain (architecture-native) crc_byte().
//
inline void crc_byte_signed(uint16_t *this_crc, uint8_t val)
{
    uint16_t crc_val = *this_crc;
    // reproduce the legacy signed-char sign extension of the input byte
    uint16_t sval = (uint16_t)(int16_t)(int8_t)val;

    uint16_t temp = ((crc_val >> 8) ^ sval) & 0xFFFF;
    crc_val = (crc_val << 8) & 0xFFFF;
    uint16_t quick = (temp ^ (temp >> 4)) & 0xFFFF;
    crc_val = (crc_val ^ quick) & 0xFFFF;
    quick = (quick << 5) & 0xFFFF;
    crc_val = (crc_val ^ quick) & 0xFFFF;
    quick = (quick << 7) & 0xFFFF;
    crc_val = (crc_val ^ quick) & 0xFFFF;

    *this_crc = crc_val;
}

inline void crc_append_buf_signed(uint16_t *this_crc, const uint8_t *ptr, uint16_t length)
{
    while (length--)
    {
        crc_byte_signed(this_crc, *ptr++);
    }
}

inline uint16_t crc_buf_signed(uint16_t *this_crc, const uint8_t *ptr, uint16_t length)
{
    crc_init(this_crc);
    crc_append_buf_signed(this_crc, ptr, length);
    return *this_crc;
}

#endif /* UTILS_CRC_H */