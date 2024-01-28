#include <nds.h>
#include <fat.h>

#include <stdio.h>

#include "scfw.bin.h"

#define SC_MODE_REG (*(vu16*) 0x09fffffe)
#define SC_MODE_MAGIC ((u16) 0xa55a)
#define SC_MODE_FLASH_RW ((u16) 4)

#define SC_FLASH_MAGIC_ADDR_1 (*(vu16*) 0x08000b92)
#define SC_FLASH_MAGIC_ADDR_2 (*(vu16*) 0x0800046c)
#define SC_FLASH_MAGIC_1 ((u16) 0xaa)
#define SC_FLASH_MAGIC_2 ((u16) 0x55)
#define SC_FLASH_ERASE ((u16) 0x80)
#define SC_FLASH_ERASE_BLOCK ((u16) 0x30)
#define SC_FLASH_ERASE_CHIP ((u16) 0x10)
#define SC_FLASH_PROGRAM ((u16) 0xA0)
#define SC_FLASH_IDLE ((u16) 0xF0)
#define SC_FLASH_IDENTIFY ((u16) 0x90)

u32 sc_flash_id()
{
	SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_MAGIC_1;
	SC_FLASH_MAGIC_ADDR_2 = SC_FLASH_MAGIC_2;
	SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_IDENTIFY;
	
	// should equal 0x000422b9
	u32 res = SC_FLASH_MAGIC_ADDR_1;
	res |= *GBA_BUS << 16;
	
	*GBA_BUS = SC_FLASH_IDLE;
	
	return res;

}

void sc_flash_rw_enable()
{
	bool buf = REG_IME;
	REG_IME = 0;
	SC_MODE_REG = SC_MODE_MAGIC;
	SC_MODE_REG = SC_MODE_MAGIC;
	SC_MODE_REG = SC_MODE_FLASH_RW;
	SC_MODE_REG = SC_MODE_FLASH_RW;
	REG_IME = buf;
}

void sc_flash_erase_chip()
{
	bool buf = REG_IME;
	REG_IME = 0;
	SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_MAGIC_1;
	SC_FLASH_MAGIC_ADDR_2 = SC_FLASH_MAGIC_2;
	SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_ERASE;
	SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_MAGIC_1;
	SC_FLASH_MAGIC_ADDR_2 = SC_FLASH_MAGIC_2;
	SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_ERASE_CHIP;
	
	while (*GBA_BUS != *GBA_BUS) {
		
	}
	*GBA_BUS = SC_FLASH_IDLE;
	REG_IME = buf;
}

void sc_flash_erase_block(vu16 *addr)
{
	bool buf = REG_IME;
	REG_IME = 0;
	SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_MAGIC_1;
	SC_FLASH_MAGIC_ADDR_2 = SC_FLASH_MAGIC_2;
	SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_ERASE;
	SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_MAGIC_1;
	SC_FLASH_MAGIC_ADDR_2 = SC_FLASH_MAGIC_2;
	*addr = SC_FLASH_ERASE_BLOCK;
	
	while (*GBA_BUS != *GBA_BUS) {
		
	}
	*GBA_BUS = SC_FLASH_IDLE;
	REG_IME = buf;
}

void sc_flash_program(vu16 *addr, u16 val)
{
	bool buf = REG_IME;
	REG_IME = 0;
	SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_MAGIC_1;
	SC_FLASH_MAGIC_ADDR_2 = SC_FLASH_MAGIC_2;
	SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_PROGRAM;
	*addr = val;
	
	while (*GBA_BUS != *GBA_BUS) {
		
	}
	*GBA_BUS = SC_FLASH_IDLE;
	REG_IME = buf;
}


//---------------------------------------------------------------------------------
int main(void) {
//---------------------------------------------------------------------------------
	consoleDemoInit();
	
	sysSetCartOwner(true);

	iprintf("      Death 2 supercard :3\n");
 
	sc_flash_rw_enable();
	
	iprintf("      Flash ID %lx\n", sc_flash_id());
	
	iprintf("      Erasing whole chip\n");
	sc_flash_erase_chip();
	iprintf("      Erased whole chip\n");
	
	for (u32 off = 0; off < scfw_bin_len; off += 2)
	{
		u16 val = 0;
		val |= scfw_bin[off];
		val |= scfw_bin[off+1] << 8;
		sc_flash_program((vu16*) (0x08000000 + off), val);
		if (!(off & 0x00ff))
			iprintf("      Programmed %lx\n", 0x08000000 + off);
	}
	
	iprintf("      Ded!\n");
 
	while(1) {
		
	}

	return 0;
}
