#pragma once
#include "Save.h"

bool flash_patchV120(const struct save_type* type);
bool flash_patchV123(const struct save_type* type);
bool flash_patchV126(const struct save_type* type);
bool flash_patch512V130(const struct save_type* type);
bool flash_patch1MV102(const struct save_type* type);
bool flash_patch1MV103(const struct save_type* type);