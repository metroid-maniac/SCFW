#include <gba.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>

#include "Save.h"
#include "WhiteScreenPatch.h"

char *stpcpy(char*, char*);

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

__attribute__((packed)) struct settings { 
	u8 autosave;
};
struct settings settings = {
	.autosave = 1
};
FILE *settings_file;

union paging_index {
	s32 abs;
	struct {
		u32 row : 4;
		s32 page : 28;
	};
};

#define GBA_ROM ((vu32*) 0x08000000)
#define GBA_BUS ((vu16*) 0x08000000)
#define GBA_SRAM ((vu8*) 0x0e000000)

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

enum
{
	SC_RAM_RO = 0x1,
	SC_MEDIA = 0x3,
	SC_FLASH_RW,
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

EWRAM_DATA u8 filebuf[0x4000];

u32 pressed;
bool savingAllowed;

void setLastPlayed(char *path) {
	/*
	FILE *lastPlayed = fopen("/scfw/lastplayed.txt", "rb");
	char old_path[PATH_MAX];
	fread(old_path, PATH_MAX, 1, lastPlayed);
	if (strcmp(path, old_path)) {
		freopen("/scfw/lastplayed.txt", "wb", lastPlayed);
		fwrite(path, strlen(path), 1, lastPlayed);
	}
	fclose(lastPlayed);
	*/
	FILE *lastPlayed = fopen("/scfw/lastplayed.txt", "wb");
	fwrite(path, strlen(path), 1, lastPlayed);
	fclose(lastPlayed);
}

void selectFile(char *path) {
	u32 pathlen = strlen(path);
	if (pathlen > 4 && !strcmp(path + pathlen - 4, ".gba")) {
		FILE *rom = fopen(path, "rb");
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
		fclose(rom);

		if (settings.autosave) {
			char savname[PATH_MAX];
			strcpy(savname, path);
			strcpy(savname + pathlen - 4, ".sav");

			FILE *sav = fopen(savname, "ab");
			freopen(savname, "rb", sav);
			iprintf("Loading SRAM:\n\n");
			total_bytes = 0;
			bytes = 0;
			do {
				bytes = fread(filebuf, 1, sizeof filebuf, sav);
				sc_mode(SC_RAM_RO);
				for (int i = 0; i < bytes; ++i)
					GBA_SRAM[total_bytes + i] = filebuf[i];
				sc_mode(SC_MEDIA);
				total_bytes += bytes;
				iprintf("\x1b[1A\x1b[K0x%x/0x10000\n", total_bytes);
			} while (bytes);
			fclose(sav);

			FILE *lastSaved = fopen("/scfw/lastsaved.txt", "wb");
			fwrite(savname, pathlen, 1, lastSaved);
			fclose(lastSaved);
		}

		iprintf("Applying patches...\n");
		sc_mode(SC_RAM_RW);
		patchGeneralWhiteScreen();
		patchSpecificGame();
		
		printf("White Screen patch done!\nNow patching Save\n");
		
		const struct save_type* saveType = savingAllowed ? save_findTag() : NULL;
		if (saveType != NULL && saveType->patchFunc != NULL){
			bool done = saveType->patchFunc(saveType);
			if(!done)
			printf("Save Type Patch Error\n");
		}else{
			printf("No need to patch\n");
		}

		iprintf("Let's go.\n");
		setLastPlayed(path);

		sc_mode(SC_RAM_RO);
		SoftReset(ROM_RESTART);
	} else if (pathlen > 4 && !strcmp(path + pathlen - 4, ".frm")) {
		u32 ime = REG_IME;
		REG_IME = 0;

		iprintf("Probing flash ID.\n");
		sc_mode(SC_FLASH_RW);
		SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_MAGIC_1;
		SC_FLASH_MAGIC_ADDR_2 = SC_FLASH_MAGIC_2;
		SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_IDENTIFY;
		u32 flash_id = SC_FLASH_MAGIC_ADDR_1;
		flash_id |= *GBA_BUS << 16;
		*GBA_BUS = SC_FLASH_IDLE;
		iprintf("Flash ID is 0x%x\n", flash_id);
		if (((flash_id >> 8) & 0xff) != 0x22) {
			iprintf("Unrecognised flash ID.");
			goto fw_end;
		}
		REG_IME = ime;

		iprintf("Flash the Supercard firmware?\n"
		        "It may brick your Supercard!\n"
		        "Press A to flash.\n"
		        "Press any other key to cancel.\n");
		do {
			scanKeys();
			pressed = keysDownRepeat();
			VBlankIntrWait();
		} while (!pressed);
		if (pressed & KEY_A) {
			sc_mode(SC_MEDIA);
			iprintf("Opening firmware\n");
			FILE *fw = fopen(path, "rb");
			fseek(fw, 0, SEEK_END);
			u32 fwsize = ftell(fw);
			fseek(fw, 0, SEEK_SET);
			if (fwsize > 0x80000) {
				iprintf("Firmware too large!\n");
				goto fw_flash_end;
			}

			ime = 0;
			iprintf("Erasing flash.\n");
			sc_mode(SC_FLASH_RW);
			SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_MAGIC_1;
			SC_FLASH_MAGIC_ADDR_2 = SC_FLASH_MAGIC_2;
			SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_ERASE;
			SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_MAGIC_1;
			SC_FLASH_MAGIC_ADDR_2 = SC_FLASH_MAGIC_2;
			SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_ERASE_CHIP;

			while (*GBA_BUS != *GBA_BUS) {
			}
			*GBA_BUS = SC_FLASH_IDLE;

			u32 total_bytes = 0;
			u32 bytes = 0;
			iprintf("Programming flash.\n\n");
			do {
				sc_mode(SC_MEDIA);
				bytes = fread(filebuf, 1, sizeof filebuf, fw);
				if (ferror(fw)) {
					iprintf("Error reading file!\n");
					goto fw_flash_end;
				}
				sc_mode(SC_FLASH_RW);
				for (u32 i = 0; i < bytes; i += 2) {
					SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_MAGIC_1;
					SC_FLASH_MAGIC_ADDR_2 = SC_FLASH_MAGIC_2;
					SC_FLASH_MAGIC_ADDR_1 = SC_FLASH_PROGRAM;
					GBA_BUS[(total_bytes + i)>>1] = filebuf[i] | (filebuf[i+1] << 8);

					while (*GBA_BUS != *GBA_BUS) {
					}
					*GBA_BUS = SC_FLASH_IDLE;
				}
				sc_mode(SC_MEDIA);
				total_bytes += bytes;
				iprintf("\x1b[1A\x1b[K0x%x/0x%x\n", total_bytes, fwsize);
			} while (bytes);

			iprintf("Done!\n");
			fw_flash_end:
			if (fw)
				fclose(fw);
		}
		fw_end:
		REG_IME = ime;
		iprintf("Press A to continue.\n");
		do {
			scanKeys();
			pressed = keysDownRepeat();
			VBlankIntrWait();
		} while (!(pressed & KEY_A));
	} else {
		iprintf("Unrecognised file extension!\n");
		do {
			scanKeys();
			pressed = keysDownRepeat();
			VBlankIntrWait();
		} while (!(pressed & KEY_A));
	}
}

int main() {
	irqInit();
	irqEnable(IRQ_VBLANK);

	scanKeys();
	keysDownRepeat();

	consoleDemoInit();

	iprintf("SCFW Kernel v0.2 GBA-mode\n\n");

	if (fatInitDefault()) {
		iprintf("FAT system initialised\n");
	} else {
		iprintf("FAT initialisation failed!\n");
		tryAgain();
	}

	settings_file = fopen("/scfw/settings.bin", "rb");
	if (settings_file) {
		struct settings loaded_settings = settings;
		fread(&loaded_settings, sizeof loaded_settings, 1, settings_file);
		if (memcmp(&loaded_settings, &settings, sizeof settings)) {
			settings = loaded_settings;
			freopen("/scfw/settings.bin", "wb", settings_file);
			fwrite(&settings, sizeof settings, 1, settings_file);
		}
	} else {
		settings_file = fopen("/scfw/settings.bin", "wb");
		fwrite(&settings, sizeof settings, 1, settings_file);
	}
	fclose(settings_file);

	if (settings.autosave) {
		FILE *lastSaved = fopen("/scfw/lastsaved.txt", "rb");
		if (lastSaved) {
			char path[PATH_MAX];
			fread(path, PATH_MAX, 1, lastSaved);
			iprintf("Saving SRAM to %s\n\n", path);
			FILE *sav = fopen(path, "wb");
			for (int i = 0; i < 0x00010000; i += sizeof filebuf) {
				sc_mode(SC_RAM_RO);
				for (int j = 0; j < sizeof filebuf; ++j)
					filebuf[j] = GBA_SRAM[i + j];
				sc_mode(SC_MEDIA);
				fwrite(filebuf, sizeof filebuf, 1, sav);
				iprintf("\x1b[1A\x1b[K0x%x/0x10000\n", i);
			}
			fclose(sav);
			remove("/scfw/lastsaved.txt");
		}
	}

	for (;;) {
		char cwd[PATH_MAX];
		getcwd(cwd, PATH_MAX);
		u32 cwdlen = strlen(cwd);
		DIR *dir = opendir(".");
		u32 diroffs[0x200];
		union paging_index diroffs_len;
		diroffs_len.abs = 0;
		for (;;) {
			long diroff = telldir(dir);
			struct dirent *dirent = readdir(dir);
			if (!dirent)
				break;
			if (!strcmp(dirent->d_name, "."))
				continue;
			diroffs[diroffs_len.abs++] = diroff;
		}
		if (!diroffs_len.abs) {
			iprintf("No directory entries!\n");
			tryAgain();
		}

		for (union paging_index cursor = { .abs = 0 };;) {
			iprintf("\x1b[2J");
			iprintf("%s\n%d/%d\n", cwdlen > 28 ? cwd + cwdlen - 28 : cwd, 1 + cursor.page, (union paging_index){ .abs = 15 + diroffs_len.abs }.page);

			for (union paging_index i = { .page = cursor.page }; i.abs < diroffs_len.abs && i.page == cursor.page; ++i.abs) {
				seekdir(dir, diroffs[i.abs]);
				struct dirent *dirent = readdir(dir);
				u32 dirent_namelen = strlen(dirent->d_name);

				char arrow = i.abs == cursor.abs ? '>' : ' ';
				if (dirent->d_type == DT_DIR)
					if (dirent_namelen > 27)
						iprintf("%c%.20s*%s/\n", arrow, dirent->d_name, dirent->d_name + dirent_namelen - 6);
					else
						iprintf("%c%s/\n", arrow, dirent->d_name);
				else
					if (dirent_namelen > 28)
						iprintf("%c%.20s*%s\n", arrow, dirent->d_name, dirent->d_name + dirent_namelen - 7);
					else
						iprintf("%c%s\n", arrow, dirent->d_name);
			}

			do {
				scanKeys();
				pressed = keysDownRepeat();
				VBlankIntrWait();
			} while (!(pressed & (KEY_A | KEY_B | KEY_START | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)));

			seekdir(dir, diroffs[cursor.abs]);
			struct dirent *dirent = readdir(dir);
			if (pressed & KEY_A) {
				if (dirent->d_type == DT_DIR) {
					chdir(dirent->d_name);
					break;
				} else {
					char path[PATH_MAX];
					char *ptr = stpcpy(path, cwd);
					if (ptr[-1] != '/')
						ptr = stpcpy(ptr, "/");
					ptr = stpcpy(ptr, dirent->d_name);
					selectFile(path);
				}
			}
			if (pressed & KEY_B) {
				chdir("..");
				break;
			}
			if (pressed & KEY_START) {
				FILE *lastPlayed = fopen("/scfw/lastplayed.txt", "rb");
				if (lastPlayed) {
					char path[PATH_MAX];
					fread(path, PATH_MAX, 1, lastPlayed);
					fclose(lastPlayed);
					selectFile(path);
				} else {
					iprintf("Could not open last played.\n");
					do {
						scanKeys();
						pressed = keysDownRepeat();
						VBlankIntrWait();
					} while (!(pressed & KEY_A));
				}
			}
			if (pressed & KEY_DOWN) {
				++cursor.row;
				if (cursor.abs >= diroffs_len.abs)
					cursor.row = 0;
			}
			if (pressed & KEY_UP) {
				--cursor.row;
				if (cursor.abs >= diroffs_len.abs)
					cursor.row = diroffs_len.row - 1;
			}
			if (pressed & KEY_LEFT) {
				--cursor.page;
				if (cursor.abs < 0) {
					u32 row = cursor.row;
					cursor.abs = diroffs_len.abs - 1;
					if (row < cursor.row)
						cursor.row = row;
				}
			}
			if (pressed & KEY_RIGHT) {
				++cursor.page;
				if (cursor.abs >= diroffs_len.abs) {
					cursor.page = 0;
				} 
			}
		}
		closedir(dir);
	}


	tryAgain();
}