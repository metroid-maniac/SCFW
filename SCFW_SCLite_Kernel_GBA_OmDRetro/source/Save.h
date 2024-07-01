#pragma once

// #ifdef __cplusplus
// extern "C" {
// #endif

enum SaveType 
{
	SAVE_TYPE_NONE = 0,

	SAVE_TYPE_EEPROM = (1 << 14),
	SAVE_TYPE_EEPROM_V111,
	SAVE_TYPE_EEPROM_V120,
	SAVE_TYPE_EEPROM_V121,
	SAVE_TYPE_EEPROM_V122,
	SAVE_TYPE_EEPROM_V124,
	SAVE_TYPE_EEPROM_V125,
	SAVE_TYPE_EEPROM_V126,

	SAVE_TYPE_FLASH = (2 << 14),
	SAVE_TYPE_FLASH512 = SAVE_TYPE_FLASH | (0 << 13),
	SAVE_TYPE_FLASH_V120,
	SAVE_TYPE_FLASH_V121,
	SAVE_TYPE_FLASH_V123,
	SAVE_TYPE_FLASH_V124,
	SAVE_TYPE_FLASH_V125,
	SAVE_TYPE_FLASH_V126,
	SAVE_TYPE_FLASH512_V130,
	SAVE_TYPE_FLASH512_V131,
	SAVE_TYPE_FLASH512_V133,

	SAVE_TYPE_FLASH1M = SAVE_TYPE_FLASH | (1 << 13),
	SAVE_TYPE_FLASH1M_V102,
	SAVE_TYPE_FLASH1M_V103,

	SAVE_TYPE_SRAM = (3 << 14),
	SAVE_TYPE_SRAM_F_V100,
	SAVE_TYPE_SRAM_F_V102,
	SAVE_TYPE_SRAM_F_V103,
	SAVE_TYPE_SRAM_V110,
	SAVE_TYPE_SRAM_V111,
	SAVE_TYPE_SRAM_V112,
	SAVE_TYPE_SRAM_V113,

	SAVE_TYPE_TYPE_MASK = (3 << 14)
};

struct save_type
{
	char     tag[16];
	u16      tagLength;
	u16 type;
	u32      size;
	bool (*  patchFunc)(const struct save_type* type);
};

extern u32 romSize;
const struct save_type* save_findTag();

void twoByteCpy(u16 *dst, const u16 *src, u32 size);
// #ifdef __cplusplus
// }
// #endif