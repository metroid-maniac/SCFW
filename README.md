#  SCFW: Bleeding-edge modular kernel branch

This branch is primarily for those who'd like to test out/play with new revisions of the kernel that have yet to be pushed to the main branch.

## Installation
1 - Download the zip file and extract the contents to the root directory of the sdcard.  
2 - This should replace the existing kernel.gba along with adding multiple emulator support.  
> **_NOTE:_** You can also select the firmware.frm file from within the kernel to flash SCFW to the Supercard's firmware. Because the firmware is minimal and the kernel is loaded from the SD card, updates to the firmware should be rare. You can enjoy kernel updates without updating the firmware.  

## Prerequisites for emulator use
For Game Boy / Game Boy Color, you need to download your preferred Goomba fork/binary and rename it to:
- gbc.gba
    - For Game Boy Color emulation
- gb.gba
	- For Game Boy emulation

For NES/Famicom, you need to download your preferred PocketNES fork/binary and rename it to:
- nes.gba

For Sega Master System, Game Gear, and Sega Game 1000 (Sega 1000), you need to download your preferred SMSAdvance fork/binary and rename it to:
- smsa.gba

For NEC PC-Engine/TurboGrafx-16, you need to download your preferred PCEAdvance fork/binary and rename it to:
- pcea.gba

For Watara/Quickshot Supervision, you need to download your preferred WasabiGBA fork/binary and rename it to:
- wsv.gba

For Neo Geo Pocket / Color, you need to download your preferred NGPGBA fork/binary and rename it to:
- ngp.gba

For Home Video Computer Advance, builds require additional work - but after doing so you can rename the binary to:
- hvca.gba
    * The base.bin file of hvca requires gbafix and an addiontal step:
        * _"Like retail games, this emulator tries to increase the ROM speed which supercard is not compatible with, I just disabled that"_ - Metroid Maniac

Once you have those files, transfer these to the scfw folder.
You should find the ff. within the scfw folder:
- gb.gba
- gbc.gba
- kernel.gba
- nes.gba
- ngp.gba
- pcea.gba
- smsa.gba
- wsv.gba
- hvca.gba (Custom built version by metroid-maniac)
- ./hvca/ (folder)
- ./hvca/mapr/ (folder)

## Differences between this and the main kernel:
- Goomba support ✅
    - Loads Game Boy Color games (*.gbc)
    - Loads Game Boy games (*.gb)
- PCEAdvance support ✅
	- Loads PC-Engine/TurboGrafx-16 games (*.pce)
- PocketNES support ✅
    - Loads NES / Famicom games (*.nes)
    - Automatic ROM region detection (PAL / NTSC timing)
- SMSAdvance support ✅
	- Loads Game Gear games (*.gg)
	- Loads Sega Game 1000 / Sega 1000 games (*.sg)
    - Loads Sega Master System games (*.sms)
	- Custom BIOS loading support. Can be toggled within kernel settings
- WasabiGBA support ✅
    - Loads Watara/Quickshot Supervision games (*.sv)
	- Custom BIOS loading support. Can be toggled within kernel settings
- NGPGBA support ✅
    - Loads Neo Geo Pocket games (*.ngp)
	- Loads Neo Geo Pocket Color games (*.ngc)
	- Custom BIOS loading support. Can be toggled within kernel settings
- HVCA support ✅
    - Loads Famicom Disk System games (*.fds)
	- Plays Nintendo Sound Files (*.nsf)
	 
## Observations
- ✅ Stable on:
    - GBA
    - NDS / NDSL Game Boy mode
- ❌ Unstable on:
    - EXEQ Game Box (clone console)
	
##NOTES
- ⚠Some GBAOAC devices such as the EXEQ Game Box SP don't play nice with flash carts as it doesn't have the same wait time. Thus, ROMs boot faster and the flash cart does not have enough time to prepare. Try to toggle "Boot games through BIOS" each time you exit a GBC/GB game.
    - Alternative method for GBAOC devices: Create a ROM compilation and sideload the resulting gba file. This process is tedious, but it works best for clones like these.
- ⚠WARNING: The cart **appears** to not have enough time to properly load both emulator and ROM if you skip the BIOS. It's better to leave that kernel option "Boot games through BIOS" as 1 (on).

## Links / Binaries
[GBATemp Bleeding-edge kernel thread](https://gbatemp.net/threads/scfw-bleeding-edge-modular-kernel-branch.656629/)

## Credits
[metroid maniac](https://github.com/metroid-maniac) - Main developer  
[Archeychen](https://github.com/ArcheyChen) - Early development into another loader, SDHC support  
[OmDRetro](https://github.com/OmDRetro) - Kernel enhancements, significantly more supported filetypes  
[RocketRobz](https://github.com/RocketRobz) - Twilightmenu++ "gbapatcher" code for patching Supercard ROMs  
[SiliconExarch](https://github.com/SiliconExarch) - Finding an old DevkitARM release with a functioning Supercard SD drive  
