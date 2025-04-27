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

#ifdef __cplusplus
extern "C" {
#endif

static inline void crc_init(uint16_t *this_crc)
{
    *this_crc = 0xFFFF;
}

// WARNING: CRC checks are dependent on system architecture (8bit, 32bit, little/big endian). 
// Legacy KMI devices that crc was implemented under were 8bit little-endian, mismatches in
// architecture can cause issues during the calculation below, not just in the final result.
static inline void crc_byte(uint16_t *this_crc, uint8_t val)
{
	uint16_t temp;
	uint16_t quick;
    uint16_t crc_val = *this_crc;
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
}

static inline void crc_append_buf(uint16_t *this_crc, const uint8_t *ptr, uint16_t length)
{
    while (length--)
    {
        crc_byte(this_crc, *ptr++);
    }
}

static inline uint16_t crc_buf(uint16_t *this_crc, const uint8_t *ptr, uint16_t length)
{
    crc_init(this_crc);
    crc_append_buf(this_crc, ptr, length);
    return *this_crc;
}

#ifdef __cplusplus
}
#endif

#endif /* UTILS_CRC_H */