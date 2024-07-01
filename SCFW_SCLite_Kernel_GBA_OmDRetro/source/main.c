#include <gba.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#include <time.h> //For PseudoRTC
#include "Save.h"
#include "WhiteScreenPatch.h"

#include "my_io_scsd.h"
#include "irq_hook.h"

char *stpcpy(char*, char*);
int strcasecmp(char*, char*);
u32 total_bytes = 0, bytes = 0;

bool overclock_ewram();
void restore_ewram_clocks();

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

enum
{
	FILTER_ALL,
	FILTER_SELECTABLE,
	FILTER_GAME,
	FILTER_LEN
};

bool filter_all(struct dirent *dirent);
bool filter_game(struct dirent *dirent);
bool filter_selectable(struct dirent *dirent);

bool (*filters[FILTER_LEN])(struct dirent*) = { &filter_all, &filter_selectable, &filter_game };

struct dirent_brief {
    long off;
    bool isdir;
    char nickname[31];
};

enum
{
	SORT_NONE,
	SORT_NICKNAME,
	SORT_FOLDER_NICKNAME,
	SORT_LEN
};

int sort_nickname(void const *l, void const *r) {
	return strncasecmp(((struct dirent_brief*) l)->nickname, ((struct dirent_brief*) r)->nickname, 31);
}

int sort_folder_nickname(void const *lv, void const *rv) {
	struct dirent_brief *l = lv;
	struct dirent_brief *r = rv;
	if (l->isdir && !r->isdir)
		return -1;
	else if (!l->isdir && r->isdir)
		return 1;
	else
		return sort_nickname(l, r);
}

int (*sorts[SORT_LEN])(void const*, void const*) = { NULL, &sort_nickname, &sort_folder_nickname };

struct settings {
	int autosave;
	int sram_patch;
	int waitstate_patch;
	int filter;
	int sort;
	int biosboot;
	int soft_reset_patch;
	int cold_boot_save;
	int smsa_bios;
	int wsv_bios;
	int ngp_bios;
};
struct settings settings = {
	.autosave = 1,
	.sram_patch = 1,
	.waitstate_patch = 1,
	.filter = FILTER_ALL,
	.sort = SORT_NONE,
	.biosboot = 1,
	.soft_reset_patch = 1,
	.cold_boot_save = 1,
	.smsa_bios = 0,
	.wsv_bios = 0,
	.ngp_bios = 0
};

struct hvca_h{
	long id; //long magic equivalent to HEX 70 41 17 04
	char filename[32]; 
	char ext[4];
	long filesize;
};

struct ngp_h{
	u32 id; //NGP,0x1A
	u32 filesize;
	u32 flags; // flags + branch hacks + reserved
	u32 follow;
	u32 bios, res0, res1, res2;	// bit 0 = bios file.
	char name[32];
};

struct pcea_h {
	char name[32];
	u32 filesize;
	u32 flags;
	u32 follow;
	u32 f_address;
	u32 id;
	char unk[12];
};

struct pnes_h {
	char name[32];
	u32 filesize;
	u32 flags;
	u32 follow;
	u32 reserved;
};

struct smsa_h {
	u32 id;
	u32 filesize;
	u16 flags;
	u16 hacks;
	u32 follow;
	u32 B_flag, res0, res1, res2; //BIOS flags + 15 bytes of reserved data
	char name[32];
};

struct wsv_h{
	u32 id; //WSV,0x1A
	u32 filesize;
	u32 flags;
	u32 follow;
	u32 bios; // bit 0 = bios file.
	u32 res[3];
	char name[32];
};

union paging_index {
	s32 abs;
	struct {
		u32 row : 4;
		s32 page : 28;
	};
};

bool filter_all(struct dirent *dirent) {
	if (!strcmp(dirent->d_name, "."))
		return false;
	return true;
}
bool filter_game(struct dirent *dirent) {
	if (!strcmp(dirent->d_name, "."))
		return false;
	if (dirent->d_type == DT_DIR)
		return true;
	u32 namelen = strlen(dirent->d_name);
	if (namelen > 4 && (!strcasecmp(dirent->d_name + namelen - 4, ".fds") || !strcasecmp(dirent->d_name + namelen - 4, ".nsf")))
		return true;
	if (namelen > 4 && !strcasecmp(dirent->d_name + namelen - 4, ".gba"))
		return true;
	if (namelen > 4 && !strcasecmp(dirent->d_name + namelen - 4, ".gbc"))
		return true;
	if (namelen > 4 && !strcasecmp(dirent->d_name + namelen - 4, ".nes"))
		return true;
	if (namelen > 4 && (!strcasecmp(dirent->d_name + namelen - 4, ".ngp") || !strcasecmp(dirent->d_name + namelen - 4, ".ngc")))
		return true;
	if (namelen > 4 && !strcasecmp(dirent->d_name + namelen - 4, ".pce"))
		return true;
	if (namelen > 4 && !strcasecmp(dirent->d_name + namelen - 4, ".sms"))
		return true;
	if (namelen > 3 && !strcasecmp(dirent->d_name + namelen - 3, ".gb"))
		return true;
	if (namelen > 3 && (!strcasecmp(dirent->d_name + namelen - 3, ".gg") || !strcasecmp(dirent->d_name + namelen - 3, ".sg")))
		return true;
	if (namelen > 3 && !strcasecmp(dirent->d_name + namelen - 3, ".sv"))
		return true;
	return false;
}
bool filter_selectable(struct dirent *dirent) {
	if (!strcmp(dirent->d_name, "."))
		return false;
	if (dirent->d_type == DT_DIR)
		return true;
	u32 namelen = strlen(dirent->d_name);
	if (namelen > 4 && (!strcasecmp(dirent->d_name + namelen - 4, ".fds") || !strcasecmp(dirent->d_name + namelen - 4, ".nsf")))
		return true;
	if (namelen > 4 && !strcasecmp(dirent->d_name + namelen - 4, ".gba"))
		return true;
	if (namelen > 4 && !strcasecmp(dirent->d_name + namelen - 4, ".gbc"))
		return true;
	if (namelen > 4 && !strcasecmp(dirent->d_name + namelen - 4, ".nes"))
		return true;
	if (namelen > 4 && (!strcasecmp(dirent->d_name + namelen - 4, ".ngp") || !strcasecmp(dirent->d_name + namelen - 4, ".ngc")))
		return true;
	if (namelen > 4 && !strcasecmp(dirent->d_name + namelen - 4, ".pce"))
		return true;
	if (namelen > 4 && !strcasecmp(dirent->d_name + namelen - 4, ".sms"))
		return true;
	if (namelen > 3 && !strcasecmp(dirent->d_name + namelen - 3, ".gb"))
		return true;
	if (namelen > 3 && (!strcasecmp(dirent->d_name + namelen - 3, ".gg") || !strcasecmp(dirent->d_name + namelen - 3, ".sg")))
		return true;
	if (namelen > 3 && !strcasecmp(dirent->d_name + namelen - 3, ".sv"))
		return true;
	if (namelen > 4 && !strcasecmp(dirent->d_name + namelen - 4, ".frm"))
		return true;
	if (!settings.autosave && namelen > 4 && !strcasecmp(dirent->d_name + namelen - 4, ".sav"))
		return true;
	return false;
}

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
	// Bottom 16MB of SDRAM remains read/writable in this mode.
	SC_MEDIA = 0x7,
	SC_FLASH_RW = 0x4,
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

IWRAM_DATA u8 filebuf[0x4000];

u32 pressed;
bool savingAllowed = true;

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

void loadSram(char *path) {
	sc_mode(SC_MEDIA);
	FILE *sav = fopen(path, "rb");
	if (sav) {
		iprintf("Loading SRAM:\n\n");
		total_bytes = 0,bytes = 0;
		do {
			bytes = fread(filebuf, 1, sizeof filebuf, sav);
			sc_mode(SC_RAM_RO);
			for (int i = 0; i < bytes; ++i) {
				GBA_SRAM[total_bytes + i] = filebuf[i];
				if (GBA_SRAM[total_bytes + i] != filebuf[i]) {
					iprintf("\x1b[1A\x1b[KSRAM write failed at\n0x%x\n\n", i + total_bytes);
				}
			}
			sc_mode(SC_MEDIA);
			total_bytes += bytes;
			iprintf("\x1b[1A\x1b[K0x%x/0x10000\n", total_bytes);
		} while (bytes);
	fclose(sav);
	} else {
		iprintf("Save file does not exist.\n");
	}
}

void saveSram(char *path) {
	sc_mode(SC_MEDIA);
	iprintf("Saving SRAM to %s\n\n", path);
	FILE *sav = fopen(path, "wb");
	if (sav) {
		for (int i = 0; i < 0x00010000; i += sizeof filebuf) {
			sc_mode(SC_RAM_RO);
			for (int j = 0; j < sizeof filebuf; ++j)
				filebuf[j] = GBA_SRAM[i + j];
			sc_mode(SC_MEDIA);
			fwrite(filebuf, sizeof filebuf, 1, sav);
			iprintf("\x1b[1A\x1b[K0x%x/0x10000\n", i);
		}
		fclose(sav);
	}
}

bool is_empty(s32 *buf, int size) {
	bool ones = false;
	bool zeroes = false;
	for (int i = 0; i < size; ++i) {
		if (buf[i] == 0 && !ones) {
			zeroes = true;
		}
		else if (buf[i] == -1 && !zeroes) {
			ones = true;
		}
		else {
			return false;
		}
	}
	return true;
}

void resetPatch(u32 romsize) {
	iprintf("Soft reset patching...\n");
	sc_mode(SC_RAM_RW);
	
	u32 original_branch = *GBA_ROM;
	u32 original_entrypoint = ((original_branch & 0x00ffffff) << 2) + 0x08000008;
	u32 patched_entrypoint = 0x09ffff00 - irq_hook_bin_len - 4;
	if (patched_entrypoint < 0x08000000 + romsize)
		while (patched_entrypoint > 0x080000c0) {
			if (is_empty((s32*) patched_entrypoint, irq_hook_bin_len + 4))
				break;
			patched_entrypoint -= 4;
		}
	if (patched_entrypoint <= 0x080000c0) {
		iprintf("Could not soft reset patch\n");
		return;
	}
	u32 patched_branch = 0xea000000 | ((patched_entrypoint - 0x08000008) >> 2);
	
	int ctr = 0;
	for (int i = 0; i < romsize >> 2; ++i)
		if (GBA_ROM[i] == 0x03007ffc) {
			GBA_ROM[i] = 0x03fffff4;
			++ctr;
		}
	if (!ctr) {
		iprintf("Could not soft reset patch!\n");
		return;
	}
	*GBA_ROM = patched_branch; 
	int i;
	for (i = 0; i < irq_hook_bin_len >> 2; ++i)
		i[(u32*) patched_entrypoint] = i[(u32*) irq_hook_bin];
	i[(u32*) patched_entrypoint] = original_entrypoint;
	iprintf("Patched!\n");
}

u32 u32conv(const char* text) {
    u32 result = 0;
    for (int i = 0; i < 3; ++i) {
        result <<= 8; // Shift left by 8 bits
        result |= (u32)text[i]; // OR with ASCII value
    }
    return result;
}

void u_prompt(char *i)
{
	if(sizeof(i) > 0)
		printf(i);
	do {
		scanKeys();
		pressed = keysDownRepeat();
		VBlankIntrWait();
	} while (!(pressed & KEY_A));
}

void hvca_f(char path[], struct hvca_h *head, const char *out) {
    head->id = 0x04174170;
    char *dir_sep = strrchr(path, '/');

    if (dir_sep == NULL) {
        dir_sep = path;
    } else {
        dir_sep++;
    }

    char *f_end = strrchr(dir_sep, '.');
    if (f_end == NULL) {
        iprintf("ERROR: NO EXTENSION FOUND\n\n");
        return;
    }

    strncpy(head->filename, dir_sep, f_end - dir_sep);
    head->filename[f_end - dir_sep] = '\0';  // Null terminate

    strcpy(head->ext, f_end + 1);

    FILE *input_file = fopen(path, "rb");

    FILE *o_file = fopen(out, "wb");
	
	if(!input_file) {
		fclose(input_file);
		fclose(o_file);
		iprintf("\nUnable to find: \n %s \n\n", path);
		u_prompt("ERROR: MISSING DEPENDENCY\n\nPress A to acknowledge");
		tryAgain();
	} else {
		//
        fseek(input_file, 0, SEEK_END);
        head->filesize = ftell(input_file);
        rewind(input_file);
		fwrite(head, 1, sizeof(*head), o_file);
		fclose(input_file);
		fclose(o_file);
	}
}


char *basename(char *path)
{
    char *base = strrchr(path, '/');
    return base ? base+1 : path;
}

void FlashROM(char *path, u32 pathlen, FILE *rom, u32 romsize, bool F_EOL){
	//Placeholder for now
	//u32 total_bytes = 0, bytes = 0;
	do {
		bytes = fread(filebuf, 1, sizeof filebuf, rom);
		sc_mode(SC_RAM_RW);
		DMA_Copy(3, filebuf, &GBA_ROM[total_bytes >> 2], DMA32 | bytes >> 2);
		/*
		for (u32 i = 0; i < bytes; i += 4) {
			GBA_ROM[(i + total_bytes) >> 2] = *(vu32*) &filebuf[i];
			if (GBA_ROM[(i + total_bytes) >> 2] != *(vu32*) &filebuf[i]) {
				iprintf("\x1b[1A\x1b[KSDRAM write failed at\n0x%x\n\n", i + total_bytes);
			}
		}
		*/
		sc_mode(SC_MEDIA);
		total_bytes += bytes;
		iprintf("\x1b[1A\x1b[K0x%x/0x%x\n", total_bytes, romsize);
	} while (bytes && total_bytes < 0x02000000);
	
	if(F_EOL)
	{
		if (settings.autosave) {
			char savname[PATH_MAX];
			strcpy(savname, path);
			strcpy(savname + pathlen - 4, ".sav");
			loadSram(savname);

			FILE *lastSaved = fopen("/scfw/lastsaved.txt", "wb");
			fwrite(savname, pathlen, 1, lastSaved);
			fclose(lastSaved);
		}

		if (settings.waitstate_patch) {
			iprintf("Applying waitstate patches...\n");
			sc_mode(SC_RAM_RW);
			patchGeneralWhiteScreen();
			patchSpecificGame();
			iprintf("Waitstate patch done!\n");
		}

		if (settings.sram_patch) {
			iprintf("Applying SRAM patch...\n");
			sc_mode(SC_RAM_RW);
			const struct save_type* saveType = savingAllowed ? save_findTag() : NULL;
			if (saveType != NULL && saveType->patchFunc != NULL){
				bool done = saveType->patchFunc(saveType);
				if(!done)
					printf("Save Type Patch Error\n");
			} else {
				printf("No need to patch\n");
			}
		}
		
		if (settings.soft_reset_patch)
			resetPatch(romSize);
	}
}

void smsa_f(char path[], struct smsa_h *head, const char *out) {
	head->id = u32conv("SMS") | (0x1A << 24);

    FILE *i_file = fopen(path, "rb");
    FILE *o_file = fopen(out, "wb");
	
	if(!i_file) {
		fclose(i_file);
		fclose(o_file);
		iprintf("\nUnable to find: \n %s \n\n", path);
		u_prompt("ERROR: MISSING DEPENDENCY\n\nPress A to acknowledge");
		tryAgain();
	} else {
        fseek(i_file, 0, SEEK_END);
        head->filesize = ftell(i_file);
        rewind(i_file);
		head->flags = 0;
		iprintf("Analyzing...\n");
		head->hacks = 0;
		head->follow = 0;
		head->B_flag = 0;
		if (strcasestr(basename(path), "[BIOS]")) {
			head->B_flag |= (1 << 0);
			iprintf("BIOS detected\n");
		}
		else
		{
			head->B_flag |= (0 << 0);
		}
		head->res0 = 0;
		head->res1 = 0;
		head->res2 = 0;
		char bname_b[32];
		strncpy(bname_b, basename(path), sizeof(bname_b) - 1);
		bname_b[sizeof(bname_b) - 1] = '\0'; 
		strcpy(head->name, bname_b);
		fwrite(head, 1, sizeof(*head), o_file);
		fclose(i_file);
		fclose(o_file);
	}
}

void wsv_f(char path[], struct wsv_h *head, const char *out) {
	head->id = u32conv("VSW") | (0x1A << 24);

    FILE *i_file = fopen(path, "rb");
    FILE *o_file = fopen(out, "wb");
	
	if(!i_file) {
		fclose(i_file);
		fclose(o_file);
		iprintf("\nUnable to find: \n %s \n\n", path);
		u_prompt("ERROR: MISSING DEPENDENCY\n\nPress A to acknowledge");
		tryAgain();
	} else {
        fseek(i_file, 0, SEEK_END);
        head->filesize = ftell(i_file);
        rewind(i_file);
		head->flags = 0;
		iprintf("Analyzing...\n");
		head->follow = 0;
		head->bios = 0;
		if (strcasestr(basename(path), "[BIOS]")) {
			head->bios |= (1 << 0);
			iprintf("BIOS detected\n");
		}
		else
		{
			head->bios |= (0 << 0);
		}
		head->res[0] = 0;
		char bname_b[32];
		strncpy(bname_b, basename(path), sizeof(bname_b) - 1);
		bname_b[sizeof(bname_b) - 1] = '\0'; 
		strcpy(head->name, bname_b);
		fwrite(head, 1, sizeof(*head), o_file);
		fclose(i_file);
		fclose(o_file);
	}
}

void ngp_f(char path[], struct ngp_h *head, const char *out) {
	head->id = u32conv("PGN") | (0x1A << 24);
	
    FILE *i_file = fopen(path, "rb");
    FILE *o_file = fopen(out, "wb");
	
	if(!i_file) {
		fclose(i_file);
		fclose(o_file);
		iprintf("\nUnable to find: \n %s \n\n", path);
		u_prompt("ERROR: MISSING DEPENDENCY\n\nPress A to acknowledge");
		tryAgain();
	} else {
        fseek(i_file, 0, SEEK_END);
        head->filesize = ftell(i_file);
        rewind(i_file);
		head->flags = 0;
		iprintf("Analyzing...\n");
		head->follow = 0;
		head->bios = 0;
		if (strcasestr(basename(path), "[BIOS]")) {
			head->bios |= (1 << 0);
			iprintf("BIOS detected\n");
		}
		else
		{
			head->bios |= (0 << 0);
		}
		head->res0 = 0;
		head->res1 = 0;
		head->res2 = 0;
		char bname_b[32];
		strncpy(bname_b, basename(path), sizeof(bname_b) - 1);
		bname_b[sizeof(bname_b) - 1] = '\0'; 
		strcpy(head->name, bname_b);
		fwrite(head, 1, sizeof(*head), o_file);
		fclose(i_file);
		fclose(o_file);
	}
}

void L_Seq(char *path){
	sc_mode(SC_MEDIA);
	iprintf("Let's go.\n");
	setLastPlayed(path);

	sc_mode(SC_RAM_RO);
	REG_IME = 0;
	
	restore_ewram_clocks();
	
	if (settings.biosboot)
		__asm volatile("swi 0x26");
	else
		SoftReset(ROM_RESTART);
}

void selectFile(char *path) {
	u32 pathlen = strlen(path);
	if (pathlen > 4 && !strcasecmp(path + pathlen - 4, ".gba")) {
		FILE *rom = fopen(path, "rb");
		fseek(rom, 0, SEEK_END);
		u32 romsize = ftell(rom);
		romSize = romsize;
		fseek(rom, 0, SEEK_SET);

		total_bytes = 0, bytes = 0;
		iprintf("Loading ROM:\n\n");
		
		FlashROM(path,pathlen,rom,romSize,true);
		fclose(rom);
		L_Seq(path);
	} else if (pathlen > 4 && !strcasecmp(path + pathlen - 4, ".frm")) {
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

			total_bytes = 0;
			bytes = 0;
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
	} else if (pathlen > 4 && !strcasecmp(path + pathlen - 4, ".sav")) {
		if (settings.autosave) {
			iprintf("Disable autosave to manage\nSRAM manually.\n");
			do {
				scanKeys();
				pressed = keysDownRepeat();
				VBlankIntrWait();
			} while (!pressed);
		} else {
			iprintf("Push L to load file to SRAM\n"
			        "Push R to save SRAM to file.\n"
					"Push B to cancel.\n");
			do {
				scanKeys();
				pressed = keysDownRepeat();
				VBlankIntrWait();
			} while (!(pressed & (KEY_L | KEY_R | KEY_B)));
			if (pressed & KEY_L) {
				loadSram(path);
			}
			else if (pressed & KEY_R) {
				saveSram(path);
			}
		}
	} else if ((pathlen > 4 && !strcasecmp(path + pathlen - 4, ".fds")) || (pathlen > 4 && !strcasecmp(path + pathlen - 4, ".nsf"))){
		u32 romsize = 0;
		total_bytes = 0,bytes = 0;
		const char *emu_bin = "/scfw/hvca.gba";
		FILE *emu = fopen(emu_bin, "rb");
		if (!emu) {
			iprintf("Checking %s\n",emu_bin);
			u_prompt("No HVCA found!\n\n");
			fclose(emu);
		} else {
			fseek(emu,0,SEEK_END);
			romsize = ftell(emu);
			romSize = romsize;
			fseek(emu, 0, SEEK_SET);
			iprintf("Loading HVCA\n\n");
			FlashROM(path,pathlen,emu,romSize,false);
			struct hvca_h head;
			char hvca_deps[64];
			//First file
			strcpy(hvca_deps,"/scfw/hvca/font_a.raw");
			const char *output_path = "/scfw/hvca/hvca_0.dat";
			iprintf("... PLEASE WAIT ...\n\n");
			hvca_f(hvca_deps, &head, output_path);
			FILE *out_f0 = fopen("/scfw/hvca/hvca_0.dat", "rb");
			fseek(out_f0,0,SEEK_END);
			romsize = ftell(out_f0);
			romSize += romsize;
			fseek(out_f0, 0, SEEK_SET);
			FlashROM(path,pathlen,out_f0,romSize,false);
			FILE *out_f1 = fopen("/scfw/hvca/font_a.raw", "rb");
			fseek(out_f1, 0, SEEK_END);
			romsize = ftell(out_f1);
			romSize += romsize;
			fseek(out_f1, 0, SEEK_SET);
			iprintf("Loading HVCA dependency:\n\n");
			FlashROM(path,pathlen,out_f1,romSize,false);
			//Second file
			strcpy(hvca_deps,"/scfw/hvca/font_k.raw");
			output_path = "/scfw/hvca/hvca_1.dat";
			hvca_f(hvca_deps, &head, output_path);
			FILE *out_f2 = fopen("/scfw/hvca/hvca_1.dat", "rb");
			fseek(out_f2,0,SEEK_END);
			romsize = ftell(out_f2);
			romSize += romsize;
			fseek(out_f2, 0, SEEK_SET);
			FlashROM(path,pathlen,out_f2,romSize,false);
			FILE *out_f3 = fopen("/scfw/hvca/font_k.raw", "rb");
			fseek(out_f3, 0, SEEK_END);
			romsize = ftell(out_f3);
			romSize += romsize;
			fseek(out_f3, 0, SEEK_SET);
			iprintf("Loading HVCA dependency:\n\n");
			FlashROM(path,pathlen,out_f3,romSize,false);
			//Third file
			if(!strcasecmp(path + pathlen - 4, ".nsf"))
				strcpy(hvca_deps,"/scfw/hvca/mapr/mnsf.bin");
			else
				strcpy(hvca_deps,"/scfw/hvca/mapr/mfds.bin");
			output_path = "/scfw/hvca/hvca_2.dat";
			hvca_f(hvca_deps, &head, output_path);
			FILE *out_f4 = fopen("/scfw/hvca/hvca_2.dat", "rb");
			fseek(out_f4,0,SEEK_END);
			romsize = ftell(out_f4);
			romSize += romsize;
			fseek(out_f4, 0, SEEK_SET);
			FlashROM(path,pathlen,out_f4,romSize,false);
			FILE *out_f5;
			if(!strcasecmp(path + pathlen - 4, ".nsf"))
				out_f5 = fopen("/scfw/hvca/mapr/mnsf.bin", "rb");
			else
				out_f5 = fopen("/scfw/hvca/mapr/mfds.bin", "rb");
			fseek(out_f5, 0, SEEK_END);
			romsize = ftell(out_f5);
			romSize += romsize;
			fseek(out_f5, 0, SEEK_SET);
			iprintf("Loading HVCA dependency:\n\n");
			FlashROM(path,pathlen,out_f5,romSize,false);
			//Fourth file
			strcpy(hvca_deps,"/scfw/hvca/disksys.rom");
			output_path = "/scfw/hvca/hvca_3.dat";
			hvca_f(hvca_deps, &head, output_path);
			FILE *out_f6 = fopen("/scfw/hvca/hvca_3.dat", "rb");
			fseek(out_f6,0,SEEK_END);
			romsize = ftell(out_f6);
			romSize += romsize;
			fseek(out_f6, 0, SEEK_SET);
			FlashROM(path,pathlen,out_f6,romSize,false);
			FILE *out_f7 = fopen("/scfw/hvca/disksys.rom", "rb");
			fseek(out_f7, 0, SEEK_END);
			romsize = ftell(out_f7);
			romSize += romsize;
			fseek(out_f7, 0, SEEK_SET);
			iprintf("Loading HVCA dependency:\n\n");
			FlashROM(path,pathlen,out_f7,romSize,false);
			//Fifth file (FDS ROM!)
			strcpy(hvca_deps,path);
			output_path = "/scfw/hvca/hvca_4.dat";
			hvca_f(hvca_deps, &head, output_path);
			FILE *out_f8 = fopen("/scfw/hvca/hvca_4.dat", "rb");
			fseek(out_f8,0,SEEK_END);
			romsize = ftell(out_f8);
			romSize += romsize;
			fseek(out_f8, 0, SEEK_SET);
			FlashROM(path,pathlen,out_f8,romSize,false);
			FILE *rom = fopen(path, "rb");
			fseek(rom, 0, SEEK_END);
			romsize = ftell(rom);
			romSize += romsize;
			fseek(rom, 0, SEEK_SET);
			iprintf("Loading ROM:\n\n");
			FlashROM(path,pathlen,rom,romSize,false);
			// Write end of emu
			struct hvca_h head0;
			head0.id = 0x41700417;
			head0.filename[0] = 0;
			head0.ext[0] = 0;
			head0.filesize = 0;
			FILE *out_f9 = fopen("/scfw/hvca/hvca_5.dat", "wb");
			fwrite(&head0,1, sizeof head0, out_f9);
			fclose(out_f9);
			out_f9 = fopen("/scfw/hvca/hvca_5.dat", "rb");
			fseek(out_f9, 0, SEEK_END);
			romsize = ftell(out_f9);
			romSize += romsize;
			fseek(out_f9, 0, SEEK_SET);
			iprintf("Loading HVCA dependency:\n\n");
			FlashROM(path,pathlen,out_f9,romSize,true); //Close after the last rom
			fclose(out_f9);
			fclose(out_f8);
			fclose(out_f7);
			fclose(out_f6);
			fclose(out_f5);
			fclose(out_f4);
			fclose(out_f3);
			fclose(out_f2);
			fclose(out_f1);
			fclose(out_f0);
			fclose(emu);
			fclose(rom);
			L_Seq(path);
		}
	} else if ((pathlen > 3 && !strcasecmp(path + pathlen - 3, ".gb")) || (pathlen > 4 && !strcasecmp(path + pathlen - 4, ".gbc")) ){
		u32 romsize = 0;
		total_bytes = 0,bytes = 0;
		const char *emu_bin;
		if (!strcasecmp(path + pathlen - 3, ".gb"))
			emu_bin = "/scfw/gb.gba";
		else
			emu_bin = "/scfw/gbc.gba";
		FILE *emu = fopen(emu_bin, "rb");
		if (!emu) {
			iprintf("Checking %s\n",emu_bin);
			u_prompt("No Goomba found!\n\n");
			fclose(emu);
		} else {
			fseek(emu,0,SEEK_END);
			romsize = ftell(emu);
			romSize = romsize;
			fseek(emu, 0, SEEK_SET);
			iprintf("Loading Goomba \n\n");
			FlashROM(path,pathlen,emu,romSize,false);
			FILE *rom = fopen(path, "rb");
			fseek(rom, 0, SEEK_END);
			romsize = ftell(rom);
			romSize += romsize;
			fseek(rom, 0, SEEK_SET);
			iprintf("Loading ROM:\n\n");
			FlashROM(path,pathlen,rom,romSize,true);
			fclose(rom);
			fclose(emu);
			L_Seq(path);
		}
	} else if (pathlen > 4 && !strcasecmp(path + pathlen - 4, ".nes")){
		u32 romsize = 0;
		total_bytes = 0,bytes = 0;
		const char *emu_bin = "/scfw/nes.gba";
		FILE *emu = fopen(emu_bin, "rb");
		if (!emu) {
			iprintf("Checking %s\n",emu_bin);
			u_prompt("No PocketNES found!\n\n");
			fclose(emu);
		} else {
			fseek(emu,0,SEEK_END);
			romsize = ftell(emu);
			romSize = romsize;
			fseek(emu, 0, SEEK_SET);
			iprintf("Loading PocketNES\n\n");
			FlashROM(path,pathlen,emu,romSize,false);
			struct pnes_h header;
			char bname_b[32];
			strncpy(bname_b, basename(path), sizeof(bname_b) - 1);
			bname_b[sizeof(bname_b) - 1] = '\0'; 
			strcpy(header.name, bname_b);
			FILE *rom = fopen(path, "rb");
			fseek(rom, 0, SEEK_END);
			romsize = ftell(rom);
			header.filesize = 0;
			header.filesize = romsize;
			header.flags = 0;
			iprintf("Analyzing ROM...\n\n");
			if (strcasestr(basename(path), "(E)") || strcasestr(basename(path), "(EUR)") || strcasestr(basename(path), "(Europe)")) {
				header.flags |= (1 << 2);
				iprintf("PAL timing\n");
			} else {
				header.flags |= (1 << 4);
				iprintf("NTSC timing\n");
			}
			header.follow = 0;
			header.reserved = 0;
			FILE *out_h = fopen("/scfw/pnes_h.dat", "wb");
			fwrite(&header,1, sizeof header, out_h);
			fclose(out_h);
			out_h = fopen("/scfw/pnes_h.dat", "rb");
			fseek(out_h,0,SEEK_END);
			romsize = ftell(out_h);
			romSize += romsize;
			fseek(out_h, 0, SEEK_SET);
			FlashROM(path,pathlen,out_h,romSize,false);
			fseek(rom, 0, SEEK_END);
			romsize = ftell(rom);
			romSize += romsize;
			fseek(rom, 0, SEEK_SET);
			iprintf("Loading ROM:\n\n");
			FlashROM(path,pathlen,rom,romSize,true);
			fclose(rom);
			fclose(out_h);
			fclose(emu);
			L_Seq(path);
		}
	} else if (pathlen > 4 && !strcasecmp(path + pathlen - 4, ".pce")){
		u32 romsize = 0;
		total_bytes = 0,bytes = 0;
		const char *emu_bin = "/scfw/pcea.gba";
		FILE *emu = fopen(emu_bin, "rb");
		if (!emu) {
			iprintf("Checking %s\n",emu_bin);
			u_prompt("No PCEAdvance found!\n\n");
			fclose(emu);
		} else {
			fseek(emu,0,SEEK_END);
			romsize = ftell(emu);
			romSize = romsize;
			fseek(emu, 0, SEEK_SET);
			iprintf("Loading PCEAdvance\n\n");
			FlashROM(path,pathlen,emu,romSize,false);
			struct pcea_h header;
			char bname_b[32];
			strncpy(bname_b, basename(path), sizeof(bname_b) - 1);
			bname_b[sizeof(bname_b) - 1] = '\0'; 
			strcpy(header.name, bname_b);
			FILE *rom = fopen(path, "rb");
			fseek(rom, 0, SEEK_END);
			romsize = ftell(rom);
			header.filesize = 0;
			header.filesize = romsize;
			header.flags = 0;
			iprintf("Analyzing ROM...\n\n");
			if (strcasestr(basename(path), "(J)") || strcasestr(basename(path), "(JAPAN)")) {
				header.flags |= (0 << 0);
				header.flags |= (0 << 1);
				header.flags |= (0 << 2);
				header.flags |= (0 << 5);
				iprintf("Japan ROM\n\n");
			} else {
				header.flags |= (0 << 0);
				header.flags |= (0 << 1);
				header.flags |= (1 << 2);
				header.flags |= (0 << 5);
				iprintf("USA ROM\n\n");
			}
			header.follow = 0;
			header.f_address = 0;
			header.id = u32conv("SEN") | (0x1A << 24);
			header.unk[0] = '@';
			for (int i = 1; i < 12; ++i) {
				header.unk[i] = ' ';
			}
			FILE *out_h = fopen("/scfw/pcea_h.dat", "wb");
			fwrite(&header,1, sizeof header, out_h);
			fclose(out_h);
			out_h = fopen("/scfw/pcea_h.dat", "rb");
			fseek(out_h,0,SEEK_END);
			romsize = ftell(out_h);
			romSize += romsize;
			fseek(out_h, 0, SEEK_SET);
			FlashROM(path,pathlen,out_h,romSize,false);
			fseek(rom, 0, SEEK_END);
			romsize = ftell(rom);
			romSize += romsize;
			fseek(rom, 0, SEEK_SET);
			iprintf("Loading ROM:\n\n");
			FlashROM(path,pathlen,rom,romSize,true);
			fclose(rom);
			fclose(out_h);
			fclose(emu);
			L_Seq(path);
		}
	} else if ((pathlen > 4 && !strcasecmp(path + pathlen - 4, ".sms")) || (pathlen > 3 && !strcasecmp(path + pathlen - 3, ".gg")) || (pathlen > 3 && !strcasecmp(path + pathlen - 3, ".sg"))){
		u32 romsize = 0;
		total_bytes = 0,bytes = 0;
		const char *emu_bin = "/scfw/smsa.gba";
		FILE *emu = fopen(emu_bin, "rb");
		if (!emu) {
			iprintf("Checking %s\n",emu_bin);
			u_prompt("No SMSAdvance found!\n\n");
			fclose(emu);
		} else {
			fseek(emu,0,SEEK_END);
			romsize = ftell(emu);
			romSize = romsize;
			fseek(emu, 0, SEEK_SET);
			iprintf("Loading SMSAdvance\n\n");
			FlashROM(path,pathlen,emu,romSize,false);
			struct smsa_h head;
			char smsa_deps[64];
			const char *output_path;
			FILE *out_f0, *out_f1;
			if (settings.smsa_bios) {
				if (!strcasecmp(path + pathlen - 4, ".sms"))
					strcpy(smsa_deps,"/scfw/[BIOS]smsa_sms.rom");
				if (!strcasecmp(path + pathlen - 3, ".sg"))
					strcpy(smsa_deps,"/scfw/[BIOS]smsa_sg.rom");
				if (!strcasecmp(path + pathlen - 3, ".gg"))
					strcpy(smsa_deps,"/scfw/[BIOS]smsa_gg.rom");
				output_path = "/scfw/smsa_0.dat";
				iprintf("... PLEASE WAIT ...\n\n");
				smsa_f(smsa_deps, &head, output_path);
				out_f0 = fopen(output_path, "rb");
				fseek(out_f0,0,SEEK_END);
				romsize = ftell(out_f0);
				romSize += romsize;
				fseek(out_f0, 0, SEEK_SET);
				FlashROM(path,pathlen,out_f0,romSize,false);
				out_f1 = fopen(smsa_deps, "rb");
				fseek(out_f1, 0, SEEK_END);
				romsize = ftell(out_f1);
				romSize += romsize;
				fseek(out_f1, 0, SEEK_SET);
				iprintf("Loading SMSA BIOS:\n\n");
				FlashROM(path,pathlen,out_f1,romSize,false);
			}
			//Flash SMSA ROM
			head.id = u32conv("SMS") | (0x1A << 24);
			FILE *rom = fopen(path, "rb");
			fseek(rom, 0, SEEK_END);
			romsize = ftell(rom);
			head.filesize = 0;
			head.filesize = romsize;
			head.flags = 0;
			iprintf("Analyzing ROM...\n\n");
			if (strcasestr(basename(path), "(E)") || strcasestr(basename(path), "(EUR)") || strcasestr(basename(path), "(Europe)")) {
				head.flags |= (1 << 0);
				iprintf("PAL timing\n\n");
			} else {
				head.flags |= (0 << 0);
				iprintf("NTSC timing\n\n");
			}
			if (strcasestr(basename(path), "(J)") || strcasestr(basename(path), "(JAPAN)")) {
				head.flags |= (1 << 1);
				iprintf("Japan ROM\n\n");
			} else {
				head.flags |= (0 << 1);
				iprintf("USA/EUR ROM\n\n");
			}
			if(!strcasecmp(path + pathlen - 4, ".sms") || !strcasecmp(path + pathlen - 3, ".sg"))
				head.flags |= (0 << 2);
			else
				head.flags |= (1 << 2);
			head.hacks = 0;
			head.follow = 0;
			head.B_flag = 0;
			head.res0 = 0;
			head.res1 = 0;
			head.res2 = 0;
			char bname_b[32];
			strncpy(bname_b, basename(path), sizeof(bname_b) - 1);
			bname_b[sizeof(bname_b) - 1] = '\0'; 
			strcpy(head.name, bname_b);
			FILE *out_h = fopen("/scfw/smsa_1.dat", "wb");
			fwrite(&head,1, sizeof head, out_h);
			fclose(out_h);
			out_h = fopen("/scfw/smsa_1.dat", "rb");
			fseek(out_h,0,SEEK_END);
			romsize = ftell(out_h);
			romSize += romsize;
			fseek(out_h, 0, SEEK_SET);
			FlashROM(path,pathlen,out_h,romSize,false);
			fseek(rom, 0, SEEK_END);
			romsize = ftell(rom);
			romSize += romsize;
			fseek(rom, 0, SEEK_SET);
			iprintf("Loading ROM:\n\n");
			FlashROM(path,pathlen,rom,romSize,true);
			fclose(rom);
			fclose(out_h);
			fclose(out_f1);
			fclose(out_f0);
			fclose(emu);
			L_Seq(path);
		}
	} else if (pathlen > 3 && !strcasecmp(path + pathlen - 3, ".sv")){
		u32 romsize = 0;
		total_bytes = 0,bytes = 0;
		const char *emu_bin = "/scfw/wsv.gba";
		FILE *emu = fopen(emu_bin, "rb");
		if (!emu) {
			iprintf("Checking %s\n",emu_bin);
			u_prompt("No WasabiGBA found!\n\n");
			fclose(emu);
		} else {
			fseek(emu,0,SEEK_END);
			romsize = ftell(emu);
			romSize = romsize;
			fseek(emu, 0, SEEK_SET);
			iprintf("Loading WasabiGBA\n\n");
			FlashROM(path,pathlen,emu,romSize,false);
			struct wsv_h head;
			char wsv_deps[64];
			const char *output_path;
			FILE *out_f0, *out_f1;
			if (settings.wsv_bios) {
				if (!strcasecmp(path + pathlen - 3, ".sv"))
					strcpy(wsv_deps,"/scfw/[BIOS]wsv.rom");
				output_path = "/scfw/wsv_0.dat";
				iprintf("... PLEASE WAIT ...\n\n");
				wsv_f(wsv_deps, &head, output_path);
				out_f0 = fopen(output_path, "rb");
				fseek(out_f0,0,SEEK_END);
				romsize = ftell(out_f0);
				romSize += romsize;
				fseek(out_f0, 0, SEEK_SET);
				FlashROM(path,pathlen,out_f0,romSize,false);
				out_f1 = fopen(wsv_deps, "rb");
				fseek(out_f1, 0, SEEK_END);
				romsize = ftell(out_f1);
				romSize += romsize;
				fseek(out_f1, 0, SEEK_SET);
				iprintf("Loading WSV BIOS:\n\n");
				FlashROM(path,pathlen,out_f1,romSize,false);
			}
			head.id = u32conv("VSW") | (0x1A << 24);
			FILE *rom = fopen(path, "rb");
			fseek(rom, 0, SEEK_END);
			romsize = ftell(rom);
			head.filesize = 0;
			head.filesize = romsize;
			head.flags = 0;
			iprintf("Analyzing ROM...\n\n");
			head.follow = 0;
			head.bios = 0;
			head.res[0] = 0;
			char bname_b[32];
			strncpy(bname_b, basename(path), sizeof(bname_b) - 1);
			bname_b[sizeof(bname_b) - 1] = '\0'; 
			strcpy(head.name, bname_b);
			FILE *out_f2 = fopen("/scfw/wsv_1.dat", "wb");
			fwrite(&head,1, sizeof head, out_f2);
			fclose(out_f2);
			out_f2 = fopen("/scfw/wsv_1.dat", "rb");
			fseek(out_f2,0,SEEK_END);
			romsize = ftell(out_f2);
			romSize += romsize;
			fseek(out_f2, 0, SEEK_SET);
			FlashROM(path,pathlen,out_f2,romSize,false);
			fseek(rom, 0, SEEK_END);
			romsize = ftell(rom);
			romSize += romsize;
			fseek(rom, 0, SEEK_SET);
			iprintf("Loading ROM:\n\n");
			FlashROM(path,pathlen,rom,romSize,true);
			fclose(rom);
			fclose(out_f2);
			fclose(out_f1);
			fclose(out_f0);
			fclose(emu);
			L_Seq(path);
		}
	} else if ((pathlen > 4 && !strcasecmp(path + pathlen - 4, ".ngp")) || (pathlen > 4 && !strcasecmp(path + pathlen - 4, ".ngc"))){
		u32 romsize = 0;
		total_bytes = 0,bytes = 0;
		const char *emu_bin = "/scfw/ngp.gba";
		FILE *emu = fopen(emu_bin, "rb");
		if (!emu) {
			iprintf("Checking %s\n",emu_bin);
			u_prompt("No NGPGBA found!\n\n");
			fclose(emu);
		} else {
			fseek(emu,0,SEEK_END);
			romsize = ftell(emu);
			romSize = romsize;
			fseek(emu, 0, SEEK_SET);
			iprintf("Loading NGPGBA\n\n");
			FlashROM(path,pathlen,emu,romSize,false);
			struct ngp_h head;
			//
			char ngp_deps[64];
			const char *output_path;
			FILE *out_f0, *out_f1;
			if (settings.ngp_bios) {
				if (!strcasecmp(path + pathlen - 4, ".ngc"))
					strcpy(ngp_deps,"/scfw/[BIOS]ngp_color.rom");
				if (!strcasecmp(path + pathlen - 4, ".ngp"))
					strcpy(ngp_deps,"/scfw/[BIOS]ngp_og.rom");
				output_path = "/scfw/ngpgba_0.dat";
				iprintf("... PLEASE WAIT ...\n\n");
				ngp_f(ngp_deps, &head, output_path);
				out_f0 = fopen(output_path, "rb");
				fseek(out_f0,0,SEEK_END);
				romsize = ftell(out_f0);
				romSize += romsize;
				fseek(out_f0, 0, SEEK_SET);
				FlashROM(path,pathlen,out_f0,romSize,false);
				out_f1 = fopen(ngp_deps, "rb");
				fseek(out_f1, 0, SEEK_END);
				romsize = ftell(out_f1);
				romSize += romsize;
				fseek(out_f1, 0, SEEK_SET);
				iprintf("Loading NGPGBA BIOS:\n\n");
				FlashROM(path,pathlen,out_f1,romSize,false);
			}
			//BIOS FIRST THEN THIS
			head.id = u32conv("PGN") | (0x1A << 24);
			FILE *rom = fopen(path, "rb");
			fseek(rom, 0, SEEK_END);
			romsize = ftell(rom);
			head.filesize = 0;
			head.filesize = romsize;
			head.flags = 0;
			if(!strcasecmp(path + pathlen - 4, ".ngc"))
				head.flags |= (1 << 2);
			else
				head.flags |= (0 << 2);
			iprintf("Analyzing ROM...\n\n");
			head.follow = 0;
			head.bios = 0;
			head.res0 = 0;
			head.res1 = 0;
			head.res2 = 0;
			char bname_b[32];
			strncpy(bname_b, basename(path), sizeof(bname_b) - 1);
			bname_b[sizeof(bname_b) - 1] = '\0'; 
			strcpy(head.name, bname_b);
			FILE *out_h = fopen("/scfw/ngpgba_1.dat", "wb");
			fwrite(&head,1, sizeof head, out_h);
			fclose(out_h);
			out_h = fopen("/scfw/ngpgba_1.dat", "rb");
			fseek(out_h,0,SEEK_END);
			romsize = ftell(out_h);
			romSize += romsize;
			fseek(out_h, 0, SEEK_SET);
			FlashROM(path,pathlen,out_h,romSize,false);
			fseek(rom, 0, SEEK_END);
			romsize = ftell(rom);
			romSize += romsize;
			fseek(rom, 0, SEEK_SET);
			iprintf("Loading ROM:\n\n");
			FlashROM(path,pathlen,rom,romSize,true);
			fclose(rom);
			fclose(out_h);
			fclose(out_f1);
			fclose(out_f0);
			fclose(emu);
			L_Seq(path);
		}
	} else {
		u_prompt("Unrecognised file extension!\n");
	}
}

void change_settings(char *path) {
	for (int cursor = 0;;) {
		iprintf("\x1b[2J"
		        "SCFW Kernel v0.5.2-HVCA-B \nGBA-mode\n\n");
		
		iprintf("%cAutosave: %i\n", cursor == 0 ? '>' : ' ', settings.autosave);
		iprintf("%cSRAM Patch: %i\n", cursor == 1 ? '>' : ' ', settings.sram_patch);
		iprintf("%cWaitstate Patch: %i\n", cursor == 2 ? '>' : ' ', settings.waitstate_patch);
		iprintf("%cSoft reset Patch: %i\n", cursor == 3 ? '>' : ' ', settings.soft_reset_patch);
		iprintf("%cBoot games through BIOS: %i\n", cursor == 4 ? '>' : ' ', settings.biosboot);
		iprintf("%cAutosave after cold boot: %i\n", cursor == 5 ? '>' : ' ', settings.cold_boot_save);
		iprintf("%c[SMSAdvance] Load BIOS: %i\n", cursor == 6 ? '>' : ' ', settings.smsa_bios);
		iprintf("%c[WasabiGBA] Load BIOS: %i\n", cursor == 7 ? '>' : ' ', settings.wsv_bios);
		iprintf("%c[NGPGBA] Load BIOS: %i\n", cursor == 8 ? '>' : ' ', settings.ngp_bios);
		
		do {
			scanKeys();
			pressed = keysDownRepeat();
			VBlankIntrWait();
		} while (!(pressed & (KEY_A | KEY_B | KEY_UP | KEY_DOWN)));
		
		if (pressed & KEY_A) {
			switch (cursor) {
			case 0:
				settings.autosave = !settings.autosave;
				break;
			case 1: 
				settings.sram_patch = !settings.sram_patch;
				break;
			case 2:
				settings.waitstate_patch = !settings.waitstate_patch;
				break;
			case 3: 
				settings.soft_reset_patch = !settings.soft_reset_patch;
				break;
			case 4:
				settings.biosboot = !settings.biosboot;
				break;
			case 5:
				settings.cold_boot_save = !settings.cold_boot_save;
				break;
			case 6:
				settings.smsa_bios = !settings.smsa_bios;
				break;
			case 7:
				settings.wsv_bios = !settings.wsv_bios;
				break;
			case 8:
				settings.ngp_bios = !settings.ngp_bios;
				break;
			}
		}
		if (pressed & KEY_B) {
			break;
		}
		if (pressed & KEY_UP) {
			--cursor;
			if (cursor < 0)
				cursor += 9;
		}
		if (pressed & KEY_DOWN) {
			++cursor;
			if (cursor > 8)
				cursor -= 9;
		}
	}
	
	iprintf("Saving settings...\n");
	FILE *settings_file = fopen("/scfw/settings.bin", "wb");
	if (settings_file) {
		fwrite(&settings, 1, sizeof settings, settings_file);
		fclose(settings_file);
	}
}

bool has_reset_token() {
	sc_mode(SC_RAM_RW);
	u32 reset_token = *(vu32*) 0x09ffff80;
	sc_mode(SC_MEDIA);
	return reset_token == 0xa55aa55a;
}

int main() {
	irqInit();
	irqEnable(IRQ_VBLANK);

	scanKeys();
	keysDownRepeat();

	consoleDemoInit();

	iprintf("SCFW Kernel v0.5.2-HVCA-B \nGBA-mode\n\n");
	
	*(vu16*) 0x04000204	 = 0x40c0;
	if (overclock_ewram())
		iprintf("Overclocked EWRAM\n");
	else
		iprintf("Could not overclock EWRAM\n");

	_my_io_scsd.startup();
	if (fatMountSimple("fat", &_my_io_scsd)) {
		iprintf("FAT system initialised\n");
	} else {
		iprintf("FAT initialisation failed!\n");
		tryAgain();
	}
	chdir("fat:/");

	{
		iprintf("Loading settings...\n");
		FILE *settings_file = fopen("/scfw/settings.bin", "rb+");
		if (settings_file) {
			iprintf("Reading settings\n");
			if (fread(&settings, 1, sizeof settings, settings_file) != sizeof settings) {
					iprintf("Appending new defaults\n");
					freopen("", "wb", settings_file);
					fwrite(&settings, 1, sizeof settings, settings_file);
			}
			fclose(settings_file);
		} else {
			iprintf("Creating settings file\n");
			settings_file = fopen("/scfw/settings.bin", "wb");
			if (settings_file) {
				fwrite(&settings, 1, sizeof settings, settings_file);
				fclose(settings_file);
			}
		}
		iprintf("Settings loaded!\n");
	}

	if (settings.autosave) {
		if (settings.cold_boot_save || has_reset_token()) {
			FILE *lastSaved = fopen("/scfw/lastsaved.txt", "rb");
			if (lastSaved) {
				char path[PATH_MAX];
				path[fread(path, 1, PATH_MAX, lastSaved)] = '\0';
				saveSram(path);
			}
		}
		else {
			iprintf("Skipping autosave due to cold boot.\n");
		}
		remove("/scfw/lastsaved.txt");
	}

	for (;;) {
		char cwd[PATH_MAX];
		getcwd(cwd, PATH_MAX);
		u32 cwdlen = strlen(cwd);
		DIR *dir = opendir(".");
		EWRAM_DATA static struct dirent_brief dirents[0x200];
		union paging_index dirents_len;
		bool dirents_overflow = false;
		dirents_len.abs = 0;
		for (;;) {
			u32 off = telldir(dir);
			struct dirent *dirent = readdir(dir);
			if (!dirent)
				break;
			if (dirents_len.abs >= 0x200) {
				dirents_overflow = true;
				break;
			}
			if ((*filters[settings.filter])(dirent)) {
				dirents[dirents_len.abs].off = off;
				dirents[dirents_len.abs].isdir = dirent->d_type == DT_DIR;
 				u32 namelen = strlen(dirent->d_name);
				if (dirent->d_type == DT_DIR)
					if (namelen > 27)
						sprintf(dirents[dirents_len.abs].nickname, "%.20s*%s/", dirent->d_name, dirent->d_name + namelen - 6);
					else
						sprintf(dirents[dirents_len.abs].nickname, "%s/", dirent->d_name);
				else
					if (namelen > 28)
						sprintf(dirents[dirents_len.abs].nickname, "%.20s*%s", dirent->d_name, dirent->d_name + namelen - 7);
					else
						sprintf(dirents[dirents_len.abs].nickname, "%s", dirent->d_name);
				++dirents_len.abs;
			}
		}
		if (!dirents_len.abs) {
			iprintf("No directory entries!\n");
			tryAgain();
		}
		if (sorts[settings.sort])
			qsort(dirents, dirents_len.abs, sizeof *dirents, sorts[settings.sort]);

		for (union paging_index cursor = { .abs = 0 };;) {
			iprintf("\x1b[2J");
			iprintf("%s\n%d/%d%s\n", cwdlen > 28 ? cwd + cwdlen - 28 : cwd, 1 + cursor.page, (union paging_index){ .abs = 15 + dirents_len.abs }.page, dirents_overflow ? "!" : "");

			for (union paging_index i = { .page = cursor.page }; i.abs < dirents_len.abs && i.page == cursor.page; ++i.abs)
				iprintf("%c%s\n", i.abs == cursor.abs ? '>' : ' ', dirents[i.abs].nickname);

			do {
				scanKeys();
				pressed = keysDownRepeat();
				VBlankIntrWait();
			} while (!(pressed & (KEY_A | KEY_B | KEY_START | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R)));

			if (pressed & KEY_A) {
				seekdir(dir, dirents[cursor.abs].off);
				struct dirent *dirent = readdir(dir);
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
			else if (pressed & KEY_B) {
				if (chdir(".."))
					change_settings(NULL);
				break;
			}
			else if (pressed & KEY_START) {
				FILE *lastPlayed = fopen("/scfw/lastplayed.txt", "rb");
				if (lastPlayed) {
					char path[PATH_MAX];
					path[fread(path, 1, PATH_MAX, lastPlayed)] = '\0';
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
			else if (pressed & KEY_DOWN) {
				++cursor.row;
				if (cursor.abs >= dirents_len.abs)
					cursor.row = 0;
			}
			else if (pressed & KEY_UP) {
				--cursor.row;
				if (cursor.abs >= dirents_len.abs)
					cursor.row = dirents_len.row - 1;
			}
			else if (pressed & KEY_LEFT) {
				--cursor.page;
				if (cursor.abs < 0) {
					u32 row = cursor.row;
					cursor.abs = dirents_len.abs - 1;
					if (row < cursor.row)
						cursor.row = row;
				}
			}
			else if (pressed & KEY_RIGHT) {
				++cursor.page;
				if (cursor.page >= (union paging_index){ .abs = dirents_len.abs+15 }.page)
					cursor.page = 0;
				else if (cursor.abs >= dirents_len.abs) {
					cursor.row = dirents_len.row - 1;
				}
			}
			else if (pressed & KEY_L) {
				++settings.sort;
				if (settings.sort >= SORT_LEN)
					settings.sort -= SORT_LEN;

				break;
			}
			else if (pressed & KEY_R) {
				++settings.filter;
				if (settings.filter >= FILTER_LEN)
					settings.filter -= FILTER_LEN;

				break;
			}
		}
		closedir(dir);
	}


	tryAgain();
}