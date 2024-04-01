#include <gba.h>

#define MEMCNT (*(vu32*) 0x04000800)

IWRAM_CODE bool overclock_ewram() {
	bool success = false;
	int ime = REG_IME;
	REG_IME = 0;

	EWRAM_DATA static volatile int test_var;
	test_var = 0;
	if (MEMCNT == 0x0D000020) {
		MEMCNT = 0x0E000020;
		test_var = -1;
		if (test_var == -1)
			success = true;
		else
			MEMCNT = 0x0D000020;
	}
	else if (MEMCNT == 0x0E000020) {
		success = true;
	}

	REG_IME = ime;
	return success;
}