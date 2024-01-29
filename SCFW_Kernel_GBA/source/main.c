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

EWRAM_DATA u8 filebuf[0x4000];


int main() {
	irqInit();
	irqEnable(IRQ_VBLANK);

	scanKeys();
	keysDownRepeat();

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
		long diroffs[0x200];
		u32 diroffs_len = 0;
		for (;;) {
			long diroff = telldir(dir);
			struct dirent *dirent = readdir(dir);
			if (!dirent)
				break;
			if (!strcmp(dirent->d_name, "."))
				continue;
			diroffs[diroffs_len++] = diroff;
		}
		if (!diroffs_len) {
			iprintf("No directory entries!\n");
			tryAgain();
		}

		for (int i = 0;;) {
			seekdir(dir, diroffs[i]);
			struct dirent *dirent = readdir(dir);
			u32 namelen = strlen(dirent->d_name);

			iprintf("\x1b[2J");
			iprintf("%s\n", cwd);
			iprintf("%ld: %s\n", i, dirent->d_name);

			u32 pressed;
			do {
				scanKeys();
				pressed = keysDownRepeat();
				VBlankIntrWait();
			} while (!(pressed & (KEY_A | KEY_B | KEY_UP | KEY_DOWN)));

			if (pressed & KEY_A) {
				if (dirent->d_type == DT_DIR) {
					chdir(dirent->d_name);
					break;
				} else if (namelen > 4 && !strcmp(dirent->d_name + namelen - 4, ".gba")) {
					FILE *rom = fopen(dirent->d_name, "rb");

					fseek(rom, 0, SEEK_END);
					u32 romsize = ftell(rom);
					fseek(rom, 0, SEEK_SET);

					u32 total_bytes = 0;
					u32 bytes = 0;
					iprintf("Loading ROM:\n\n");
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
						iprintf("\x1b[1A\x1b[K0x%x/0x%x\n", total_bytes, romsize);
					} while (bytes);

					sc_mode(SC_RAM_RO);
					SoftReset(ROM_RESTART);
				} else {
					iprintf("Unrecognised file extension!\n");
					do {
						scanKeys();
						pressed = keysDownRepeat();
						VBlankIntrWait();
					} while (!(pressed & KEY_A));
				}
			}
			if (pressed & KEY_B) {
				chdir("..");
				break;
			}
			if (pressed & KEY_DOWN) {
				++i;
				if (i >= diroffs_len)
					i -= diroffs_len;
			}
			if (pressed & KEY_UP) {
				--i;
				if (i < 0)
					i += diroffs_len;
			}
		}
		closedir(dir);
	}


	tryAgain();
}