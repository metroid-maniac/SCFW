/*
	io_scsd.c 

	Hardware Routines for reading a Secure Digital card
	using the SC SD
	
	Some code based on scsd_c.c, written by Amadeus 
	and Jean-Pierre Thomasset as part of DSLinux.

 Copyright (c) 2006 Michael "Chishm" Chisholm
	
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation and/or
     other materials provided with the distribution.
  3. The name of the author may not be used to endorse or promote products derived
     from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "my_io_scsd.h"
#include "my_io_sd_common.h"
#include "my_io_sc_common.h"

#define printf(...)

//---------------------------------------------------------------
// SCSD register addresses
#define REG_SCSD_CMD	        (*(vu16*)(0x09800000))
	/* bit 0: command bit to read  		*/
	/* bit 7: command bit to write 		*/

#define REG_SCSD_DATAWRITE	    (*(vu16*)(0x09000000))
#define REG_SCSD_DATAREAD	    (*(vu16*)(0x09100000))
#define REG_SCSD_DATAREAD_32	(*(vu32*)(0x09100000))
#define REG_SCSD_LITE_ENABLE	(*(vu16*)(0x09440000))
#define REG_SCSD_LOCK		    (*(vu16*)(0x09FFFFFE))
	/* bit 0: 1				*/
	/* bit 1: enable IO interface (SD,CF)	*/
	/* bit 2: enable R/W SDRAM access 	*/

//---------------------------------------------------------------
// Responses
#define SCSD_STS_BUSY			0x100
#define SCSD_STS_INSERTED		0x300

//---------------------------------------------------------------
// Send / receive timeouts, to stop infinite wait loops
#define NUM_STARTUP_CLOCKS 100	// Number of empty (0xFF when sending) bytes to send/receive to/from the card
#define TRANSMIT_TIMEOUT 100000 // Time to wait for the SC to respond to transmit or receive requests
#define RESPONSE_TIMEOUT 256	// Number of clocks sent to the SD card before giving up
#define BUSY_WAIT_TIMEOUT 500000
#define WRITE_TIMEOUT	3000	// Time to wait for the card to finish writing

#define BYTES_PER_READ 512

//---------------------------------------------------------------
// Variables required for tracking SD state
u32 _SCSD_relativeCardAddress = 0;	// Preshifted Relative Card Address
bool isSDHC;
//---------------------------------------------------------------
// Internal SC SD functions

extern bool _SCSD_writeData_s (u8 *data, u16* crc);

void _SCSD_unlock (void) {
	_SC_changeMode (SC_MODE_MEDIA);	
}

void _SCSD_enable_lite (void) {
	REG_SCSD_LITE_ENABLE = 0;
}

bool _SCSD_sendCommand (u8 command, u32 argument) {
	u8 databuff[6];	//SD卡的命令是6个字节
	u8 *tempDataPtr = databuff;
	int length = 6;
	u16 dataByte;
	int curBit;
	int i;

	*tempDataPtr++ = command | 0x40;	//Byte1:最高位为01，剩下的6bit是command
	*tempDataPtr++ = argument>>24;
	*tempDataPtr++ = argument>>16;
	*tempDataPtr++ = argument>>8;
	*tempDataPtr++ = argument;			//剩下的4byte是命令
	*tempDataPtr = _SD_CRC7 (databuff, 5);	//最后的是进行CRC的校验

	i = BUSY_WAIT_TIMEOUT;
	while (((REG_SCSD_CMD & 0x01) == 0) && (--i));
	if (i == 0) {
		return false;
	}
		
	dataByte = REG_SCSD_CMD;

	tempDataPtr = databuff;
	
	while (length--) {
		dataByte = *tempDataPtr++;
		for (curBit = 7; curBit >=0; curBit--){//#7bit，也就是第8位是用来输出的，不断移位输出
			REG_SCSD_CMD = dataByte;
			dataByte = dataByte << 1;
		}
	}
	
	return true;
}

// Returns the response from the SD card to a previous command.
bool _SCSD_getResponse (u8* dest, u32 length) {
	u32 i;	
	int dataByte;
	int numBits = length * 8;
	
	// Wait for the card to be non-busy
	i = BUSY_WAIT_TIMEOUT;
	while (((REG_SCSD_CMD & 0x01) != 0) && (--i));
	if (dest == NULL) {
		return true;
	}
	
	if (i == 0) {
		// Still busy after the timeout has passed
		return false;
	}
	
	// The first bit is always 0
	dataByte = 0;	
	numBits--;
	// Read the remaining bits in the response.
	// It's always most significant bit first
	while (numBits--) {
		dataByte = (dataByte << 1) | (REG_SCSD_CMD & 0x01);
		if ((numBits & 0x7) == 0) {
			// It's read a whole byte, so store it
			*dest++ = (u8)dataByte;
			dataByte = 0;
		}
	}

	// Send 16 more clocks, 8 more than the delay required between a response and the next command
	for (i = 0; i < 16; i++) {
		dataByte = REG_SCSD_CMD;
	}
	
	return true;
}

bool _SCSD_getResponse_R1 (u8* dest) {
	return _SCSD_getResponse (dest, 6);
}

bool _SCSD_getResponse_R1b (u8* dest) {
	return _SCSD_getResponse (dest, 6);
}

inline bool _SCSD_getResponse_R2 (u8* dest) {
	return _SCSD_getResponse (dest, 17);
}

inline bool _SCSD_getResponse_R3 (u8* dest) {
	return _SCSD_getResponse (dest, 6);
}

inline bool _SCSD_getResponse_R6 (u8* dest) {
	return _SCSD_getResponse (dest, 6);
}

void _SCSD_sendClocks (u32 numClocks) {
	do {
		REG_SCSD_CMD;
	} while (numClocks--);
}

bool _SCSD_cmd_6byte_response_my (u8* responseBuffer, u8 command, u32 data) {
	_SCSD_sendCommand (command, data);
	return _SCSD_getResponse (responseBuffer, 6);
}

bool _SCSD_cmd_17byte_response_my (u8* responseBuffer, u8 command, u32 data) {
	_SCSD_sendCommand (command, data);
	return _SCSD_getResponse (responseBuffer, 17);
}


bool _SCSD_initCard (void) {
	_SCSD_enable_lite();
	
	// Give the card time to stabilise
	_SCSD_sendClocks (NUM_STARTUP_CLOCKS);
	
	// Reset the card
	if (!_SCSD_sendCommand (GO_IDLE_STATE, 0)) {
		return false;
	}

	_SCSD_sendClocks (NUM_STARTUP_CLOCKS);
	
	// Card is now reset, including it's address
	_SCSD_relativeCardAddress = 0;

	// Init the card
	return _SD_InitCard_SDHC (_SCSD_cmd_6byte_response_my, 
				_SCSD_cmd_17byte_response_my,
				true,
				&_SCSD_relativeCardAddress,&isSDHC);
}

bool _SCSD_readData (void* buffer) {
	u8* buff_u8 = (u8*)buffer;
	u16* buff = (u16*)buffer;
	volatile register u32 temp;
	int i;
	
	i = BUSY_WAIT_TIMEOUT;
	while ((REG_SCSD_DATAREAD & SCSD_STS_BUSY) && (--i));
	if (i == 0) {
		return false;
	}

	i=256;
	if ((u32)buff_u8 & 0x01) {
		while(i--) {
			temp = REG_SCSD_DATAREAD_32;
			temp = REG_SCSD_DATAREAD_32 >> 16;
			*buff_u8++ = (u8)temp;
			*buff_u8++ = (u8)(temp >> 8);
		}
	} else {
		while(i--) {
			temp = REG_SCSD_DATAREAD_32;
			temp = REG_SCSD_DATAREAD_32 >> 16;
			*buff++ = temp; 
		}
	}

	
	for (i = 0; i < 8; i++) {
		temp = REG_SCSD_DATAREAD_32;
	}
	
	temp = REG_SCSD_DATAREAD;
	
	return true;
}

//---------------------------------------------------------------
// Functions needed for the external interface

bool _SCSD_startUp_my (void) {
    //printf("Starting _SCSD_startUp_my...\n");
    
    _SCSD_unlock();
    //printf("Unlocked in _SCSD_startUp_my.\n");
    
    bool initResult = _SCSD_initCard();
    //printf("Init card in _SCSD_startUp_my returned %d.\n", initResult);
    
    return initResult;
}


bool _SCSD_isInserted_my (void) {
    //printf("Starting _SCSD_isInserted_my...\n");

    u8 responseBuffer[6];

    if (!_SCSD_sendCommand(SEND_STATUS, 0)) {
        //printf("Failed to send command in _SCSD_isInserted_my.\n");
        return false;
    }

    if (!_SCSD_getResponse_R1(responseBuffer)) {
        //printf("Failed to get response in _SCSD_isInserted_my.\n");
        return false;
    }

    if (responseBuffer[0] != SEND_STATUS) {
        //printf("Incorrect response in _SCSD_isInserted_my: Expected %d, got %d\n", SEND_STATUS, responseBuffer[0]);
        return false;
    }

    //printf("Successful execution of _SCSD_isInserted_my.\n");
    return true;
}


bool _SCSD_readSectors_my (u32 sector, u32 numSectors, void* buffer) {
    //printf("Starting _SCSD_readSectors_my with sector %u and numSectors %u.\n", sector, numSectors);
    
    u32 i;
	u8* dest = (u8*) buffer;
	u8 responseBuffer[6];
	u32 argument = isSDHC ? sector : sector * BYTES_PER_READ;
    if (numSectors == 1) {
        //printf("Reading single sector.\n");
        if (!_SCSD_sendCommand(READ_SINGLE_BLOCK, argument)) {
            //printf("Failed to send READ_SINGLE_BLOCK command.\n");
            return false;
        }

        if (!_SCSD_readData(buffer)) {
            //printf("Failed to read data for single sector.\n");
            return false;
        }
    } else {
        //printf("Reading multiple sectors.\n");
        if (!_SCSD_sendCommand(READ_MULTIPLE_BLOCK, argument)) {
            //printf("Failed to send READ_MULTIPLE_BLOCK command.\n");
            return false;
        }

        for (i = 0; i < numSectors; i++, dest += BYTES_PER_READ) {
            if (!_SCSD_readData(dest)) {
                //printf("Failed to read data at sector %u.\n", i + sector);
                return false;
            }
        }

        //printf("Stopping transmission after multiple sectors.\n");
        _SCSD_sendCommand(STOP_TRANSMISSION, 0);
        _SCSD_getResponse_R1b(responseBuffer);
    }

    _SCSD_sendClocks(64);
    //printf("Completed _SCSD_readSectors_my.\n");
    return true;
}


bool _SCSD_writeSectors_my (u32 sector, u32 numSectors, const void* buffer) {
    //printf("Starting _SCSD_writeSectors_my with sector %u and numSectors %u.\n", sector, numSectors);

    u16 crc[4];
	u8 responseBuffer[6];
    u32 offset = isSDHC ? sector : sector * BYTES_PER_READ;
    u8* data = (u8*) buffer;
    int i;

    while (numSectors--) {
        //printf("Writing sector at offset %u.\n", offset);

        _SD_CRC16(data, BYTES_PER_READ, (u8*)crc);
        
        _SCSD_sendCommand(WRITE_BLOCK, offset);
        if (!_SCSD_getResponse_R1(responseBuffer)) {
            //printf("Failed to get response after WRITE_BLOCK command.\n");
            return false;
        }

        if (!_SCSD_writeData_s(data, crc)) {
            //printf("Failed to write data and CRC.\n");
            return false;
        }

        _SCSD_sendClocks(64);

        offset += isSDHC ? 1 : BYTES_PER_READ;
        data += BYTES_PER_READ;

        i = WRITE_TIMEOUT;
        do {
            _SCSD_sendCommand(SEND_STATUS, _SCSD_relativeCardAddress);
            if (!_SCSD_getResponse_R1(responseBuffer) || i-- <= 0) {
                //printf("Timeout or error during write confirmation.\n");
                return false;
            }
        } while (((responseBuffer[3] & 0x1f) != ((SD_STATE_TRAN << 1) | READY_FOR_DATA)));

        //printf("Sector at offset %u written successfully.\n", offset - BYTES_PER_READ);
    }

    //printf("Completed _SCSD_writeSectors_my.\n");
    return true;
}


bool _SCSD_clearStatus_my (void) {
    //printf("Starting _SCSD_clearStatus_my...\n");
    
    bool initResult = _SCSD_initCard();
    //printf("Initialization in _SCSD_clearStatus_my returned %d.\n", initResult);

    return initResult;
}


bool _SCSD_shutdown_my (void) {
    //printf("Starting _SCSD_shutdown_my...\n");

    _SC_changeMode(SC_MODE_RAM_RO);
    //printf("Changed mode to SC_MODE_RAM_RO in _SCSD_shutdown_my.\n");

    return true;
}


const DISC_INTERFACE _my_io_scsd = {
	DEVICE_TYPE_SCSD,
	FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_SLOT_GBA,
	(FN_MEDIUM_STARTUP)&_SCSD_startUp_my,
	(FN_MEDIUM_ISINSERTED)&_SCSD_isInserted_my,
	(FN_MEDIUM_READSECTORS)&_SCSD_readSectors_my,
	(FN_MEDIUM_WRITESECTORS)&_SCSD_writeSectors_my,
	(FN_MEDIUM_CLEARSTATUS)&_SCSD_clearStatus_my,
	(FN_MEDIUM_SHUTDOWN)&_SCSD_shutdown_my
} ;


