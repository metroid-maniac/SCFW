# SCFW: Custom Firmware & Kernel for Supercard

SCFW is a custom firmware and kernel for the Supercard SD.
Currently it is in a preview state with minimum functionality.

## Installation
Download the current release and copy the scfw folder to the root of your SD card.  
That's it! You can now use the kernel by loading scfw/kernel.gba from the official firmware.  
You can also select the firmware.frm file from within the kernel to flash SCFW to the Supercard's firmware. Because the firmware is minimal and the kernel is loaded from the SD card, updates to the firmware should be rare. You can enjoy kernel updates without updating the firmware.

## Current features
- Can browse files
- Can load a GBA ROM
- Can flash a Supercard firmware.
- Automatic SRAM, waitstate, and prefetch patching (buggy)
- Automatic SRAM loading & saving
- Manual SRAM management
- SDHC
## Planned features
- Nicer file browser
- Support for more filetypes with builtin goomba/pocketnes etc.
- NDS mode
- Code cleanup, lots of it.
- Soft reset patch
- Cache patches after creating them to increase loading speed
- Faster loading speeds
- Cheats(?)
- Save states(?)
## Links
GBATemp discussion thread  
https://gbatemp.net/threads/scfw-custom-firmware-kernel-for-supercard.647238/  

## Credits
[metroid maniac](https://github.com/metroid-maniac) - Main developer  
[Archeychen](https://github.com/ArcheyChen) - Early development into another loader, SDHC support  
[RocketRobz](https://github.com/RocketRobz) - Twilightmenu++ "gbapatcher" code for patching Supercard ROMs  
[SiliconExarch](https://github.com/SiliconExarch) - Finding an old DevkitARM release with a functioning Supercard SD drive
