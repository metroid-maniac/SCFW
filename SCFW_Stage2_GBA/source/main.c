#include <gba.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.H>

#include "my_io_scsd.h"

#define GBA_ROM ((vu32*) 0x08000000)

enum
{
	SC_RAM_RO = 0x1,
	SC_MEDIA = 0x3,
	SC_RAM_RW = 0x5,
};

void sc_mode(u32 mode)
{
    u32 ime = REG_IME;
    REG_IME = 0;
    *(vu16*)0x9FFFFFE = 0xA55A;
    *(vu16*)0x9FFFFFE = 0xA55A;
    *(vu16*)0x9FFFFFE = mode;
    *(vu16*)0x9FFFFFE = mode;
    REG_IME = ime;
}


void waitForever() {
	for (;;) {
		VBlankIntrWait();
	}
}

EWRAM_BSS u8 filebuf[0x20000];

int main() {
	irqInit();
	irqEnable(IRQ_VBLANK);

	consoleDemoInit();

	iprintf("SCFW v0.3.3 GBA-mode\n\n");

	_my_io_scsd.startup();
	if (fatMountSimple("fat", &_my_io_scsd)) {
		iprintf("FAT system initialised\n");
	} else {
		iprintf("FAT initialisation failed!\n");
		waitForever();
	}
	
	FILE *kernel = fopen("scfw/kernel.gba","rb");
	if (kernel) {
		iprintf("Kernel file opened successfully\n");
	} else {
		iprintf("Kernel file open failed\n");
		waitForever();
	}

	fseek(kernel, 0, SEEK_END);
	u32 kernel_size = ftell(kernel);
	if (kernel_size > 0x40000) {
		iprintf("Kernel too large to load!\n");
		waitForever();
	}
	iprintf("Kernel size is 0x%x bytes\n", kernel_size);
	fseek(kernel, 0, SEEK_SET);

	u32 total_bytes = 0;
	u32 bytes = 0;
	do {
		bytes = fread(filebuf, 1, sizeof filebuf, kernel);
		sc_mode(SC_RAM_RW);
		for (u32 i = 0; i < bytes; i += 4) {
			GBA_ROM[(i + total_bytes) >> 2] = *(vu32*) &filebuf[i];
			if (GBA_ROM[(i + total_bytes) >> 2] != *(vu32*) &filebuf[i]) {
				iprintf("SDRAM write failed at\n0x%x\n", i + total_bytes);
			}
		}
		sc_mode(SC_MEDIA);
		total_bytes += bytes;
		iprintf("Bytes read: 0x%x\n", total_bytes);
	} while (bytes);

	if (ferror(kernel)) {
		iprintf("Error reading kernel.\n");
		waitForever();
	}

	sc_mode(SC_RAM_RO);

	if ((*GBA_ROM & 0xff000000) != 0xea000000) {
		iprintf("Unexpected ROM entrypont, kernel not GBA ROM?");
		waitForever();
	}

	iprintf("Kernel loaded successfully.\n");
	iprintf("Let's go.\n");
	SoftReset(ROM_RESTART);
	iprintf("Unreachable, panic!\n");
	waitForever();
}