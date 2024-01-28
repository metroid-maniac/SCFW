#include <gba.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>

void tryAgain() {
	iprintf("Critical failure.\nPress A to restart.");
	for (;;) {
		scanKeys();
		if (keysDown() & KEY_A)
			for (;;) {
				scanKeys();
				if (keysUp() & KEY_A)
					((void(*)()) 0x02000000)();
			}
		VBlankIntrWait();
	}
}

#define GBA_ROM ((vu32*) 0x08000000)

enum
{
	SC_RAM_RO = 0x1,
	SC_MEDIA = 0x3,
	SC_RAM_RW = 0x5,
};

void sc_mode(u32 mode)
{
    *(vu16*)0x9FFFFFE = 0xA55A;
    *(vu16*)0x9FFFFFE = 0xA55A;
    *(vu16*)0x9FFFFFE = mode;
    *(vu16*)0x9FFFFFE = mode;
}

EWRAM_BSS u8 filebuf[0x20000];

int main() {
	irqInit();
	irqEnable(IRQ_VBLANK);

	consoleDemoInit();

	iprintf("SCFW Kernel v0.1 GBA-mode\n\n");

	if (fatInitDefault()) {
		iprintf("FAT system initialised\n");
	} else {
		iprintf("FAT initialisation failed!\n");
		tryAgain();
	}

	for (;;) {
		char cwd[PATH_MAX];
		getcwd(cwd, PATH_MAX);
		DIR *dir = opendir(".");
		for (;;) {
			struct dirent *dirent = readdir(dir);
			if (!dirent) {
				rewinddir(dir);
				dirent = readdir(dir);
			}
			if (!dirent) {
				tryAgain();
			}

			iprintf("\x1b[2J");
			iprintf("%s\n", cwd);
			iprintf("%s\n", dirent->d_name);
			
			u32 pressed;
			do {
				scanKeys();
				pressed = keysDownRepeat();
				VBlankIntrWait();
			} while (!pressed);

			if (pressed & KEY_A) {
				FILE *rom = fopen(dirent->d_name, "rb");

				u32 total_bytes = 0;
				u32 bytes = 0;
				do {
					bytes = fread(filebuf, 1, sizeof filebuf, rom);
					sc_mode(SC_RAM_RW);
					for (u32 i = 0; i < bytes; i += 4) {
						GBA_ROM[(i + total_bytes) >> 2] = *(vu32*) &filebuf[i];
							if (GBA_ROM[(i + total_bytes) >> 2] != *(vu32*) &filebuf[i]) {
								iprintf("SDRAM write failed!\n");
								tryAgain();
							}
					}
					sc_mode(SC_MEDIA);
					total_bytes += bytes;
					iprintf("Bytes read: 0x%x\n", total_bytes);
				} while (bytes);

				sc_mode(SC_RAM_RO);
				SoftReset(ROM_RESTART);
			}
		}
	}


	tryAgain();
}