#include <gba.h>
#include "EepromSave.h"
#include "FlashSave.h"
#include "Save.h"
#include "string.h"

u32 romSize;

#define SAVE_TYPE_COUNT		25

static const struct save_type sSaveTypes[SAVE_TYPE_COUNT] =
{
	{"EEPROM_V111", 12, SAVE_TYPE_EEPROM_V111, 512, eeprom_patchV111},
	{"EEPROM_V120", 12, SAVE_TYPE_EEPROM_V120, 8 * 1024, eeprom_patchV120},
	{"EEPROM_V121", 12, SAVE_TYPE_EEPROM_V121, 8 * 1024, eeprom_patchV120},
	{"EEPROM_V122", 12, SAVE_TYPE_EEPROM_V122, 8 * 1024, eeprom_patchV120},
	{"EEPROM_V124", 12, SAVE_TYPE_EEPROM_V124, 8 * 1024, eeprom_patchV124},
	{"EEPROM_V125", 12, SAVE_TYPE_EEPROM_V125, 8 * 1024, eeprom_patchV124},
	{"EEPROM_V126", 12, SAVE_TYPE_EEPROM_V126, 8 * 1024, eeprom_patchV126},

	{"FLASH_V120", 11, SAVE_TYPE_FLASH_V120, 64 * 1024, flash_patchV120},
	{"FLASH_V121", 11, SAVE_TYPE_FLASH_V121, 64 * 1024, flash_patchV120},
	{"FLASH_V123", 11, SAVE_TYPE_FLASH_V123, 64 * 1024, flash_patchV123},
	{"FLASH_V124", 11, SAVE_TYPE_FLASH_V124, 64 * 1024, flash_patchV123},
	{"FLASH_V125", 11, SAVE_TYPE_FLASH_V125, 64 * 1024, flash_patchV123},
	{"FLASH_V126", 11, SAVE_TYPE_FLASH_V126, 64 * 1024, /*flash_patchV126*/ flash_patchV123},
	{"FLASH512_V130", 14, SAVE_TYPE_FLASH512_V130, 64 * 1024, flash_patch512V130},
	{"FLASH512_V131", 14, SAVE_TYPE_FLASH512_V131, 64 * 1024, flash_patch512V130},
	{"FLASH512_V133", 14, SAVE_TYPE_FLASH512_V133, 64 * 1024, flash_patch512V130},
	{"FLASH1M_V102", 13, SAVE_TYPE_FLASH1M_V102, 128 * 1024, flash_patch1MV102},
	{"FLASH1M_V103", 13, SAVE_TYPE_FLASH1M_V103, 128 * 1024, flash_patch1MV103},

	//sram does not require patching
	{"SRAM_F_V100", 12, SAVE_TYPE_SRAM_F_V100, 32 * 1024, NULL},
	{"SRAM_F_V102", 12, SAVE_TYPE_SRAM_F_V102, 32 * 1024, NULL},
	{"SRAM_F_V103", 12, SAVE_TYPE_SRAM_F_V103, 32 * 1024, NULL},

	{"SRAM_V110", 10, SAVE_TYPE_SRAM_V110, 32 * 1024, NULL},
	{"SRAM_V111", 10, SAVE_TYPE_SRAM_V111, 32 * 1024, NULL},
	{"SRAM_V112", 10, SAVE_TYPE_SRAM_V112, 32 * 1024, NULL},
	{"SRAM_V113", 10, SAVE_TYPE_SRAM_V113, 32 * 1024, NULL},
};

const struct save_type* save_findTag()
{
	u32  curAddr = 0x080000C0;
	char saveTag[16];
	while (curAddr < 0x08000000+romSize) {
		u32 fst = *(u32*)curAddr;
		// tonccpy(&saveTag, (u8*)curAddr, 16);
		((u32*)saveTag)[0] = ((u32*)curAddr)[0];
		((u32*)saveTag)[1] = ((u32*)curAddr)[1];
		((u32*)saveTag)[2] = ((u32*)curAddr)[2];
		((u32*)saveTag)[3] = ((u32*)curAddr)[3];
		enum SaveType type = SAVE_TYPE_NONE;
		if (fst == 0x53414C46 && (saveTag[5] == '_' || saveTag[5] == '5' || saveTag[5] == '1')) {
			//FLAS
			type = SAVE_TYPE_FLASH;
		} else if (fst == 0x4D415253) {
			//SRAM
			type = SAVE_TYPE_SRAM;
		} else if (fst == 0x52504545 && saveTag[6] == '_') {
			//EEPR
			type = SAVE_TYPE_EEPROM;
		}

		if (type != SAVE_TYPE_NONE) {
			for (int i = 0; i < SAVE_TYPE_COUNT; i++) {
				if (strncmp(saveTag, sSaveTypes[i].tag, sSaveTypes[i].tagLength) != 0)
					continue;
				return &sSaveTypes[i];
			}
		}
		curAddr += 4;
	}
	return NULL;
}

//tonccpy
//
void twoByteCpy(u16 *dst, const u16 *src, u32 size){
	if (size == 0 || dst == NULL || src == NULL)
		return;

	u32 count;
	u16 *dst16;	 // hword destination
	u8  *src8;	  // byte source

	// Ideal case: copy by 4x words. Leaves tail for later.
	if (((u32)src|(u32)dst) %4 == 0 && size >= 4) {
		u32 *src32= (u32*)src, *dst32= (u32*)dst;

		count = size/4;
		u32 tmp = count&3;
		count /= 4;

		// Duff's Device, good friend!
		switch(tmp) {
			do {	*dst32++ = *src32++;
		case 3:	 *dst32++ = *src32++;
		case 2:	 *dst32++ = *src32++;
		case 1:	 *dst32++ = *src32++;
		case 0:	 ; } while (count--);
		}

		// Check for tail
		size &= 3;
		if (size == 0)
			return;

		src8 = (u8*)src32;
		dst16 = (u16*)dst32;
	} else {
		// Unaligned

		u32 dstOfs = (u32)dst&1;
		src8 = (u8*)src;
		dst16 = (u16*)(dst-dstOfs);

		// Head: 1 byte.
		if (dstOfs != 0) {
			*dst16 = (*dst16 & 0xFF) | *src8++<<8;
			dst16++;
			if (--size == 0)
				return;
		}
	}

	// Unaligned main: copy by 2x byte.
	count = size/2;
	while (count--) {
		*dst16++ = src8[0] | src8[1]<<8;
		src8 += 2;
	}

	// Tail: 1 byte.
	if (size & 1)
		*dst16 = (*dst16 &~ 0xFF) | *src8;
}