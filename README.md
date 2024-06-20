#  SCFW: Bleeding-edge modular kernel branch

This branch is primarily for those who'd like to test out/play with new revisions of the kernel that have yet to be pushed to the main branch.

## Installation
Download the zip file and extract the contents to the root directory of the sdcard.
This should replace the existing kernel.gba along with adding multiple emulation binaries.

## Prerequisites
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

Once you have those files, transfer these to the scfw folder.
You should find the ff. files within the scfw folder:
- kernel.gba
- gb.gba
- gbc.gba
- nes.gba
- smsa.gba

## Differences between this and the main kernel:
- PCEAdvance support ✅
	- Loads PC-Engine/TurboGrafx-16 games (*.pce)
- SMSAdvance support ✅
    - Loads Sega Master System games (*.sms)
	- Loads Game Gear games (*.gg)
	- Loads Sega Game 1000 / Sega 1000 games (*.sg)
- PocketNES support ✅
    - Loads NES / Famicom games (*.nes)
    - Automatic ROM region detection (PAL / NTSC timing)
- Goomba support ✅
    - Loads Game Boy games (*.gb)
    - Loads Game Boy Color games (*.gbc)
	 
## Observations
- ✅ Stable on:
    - GBA
    - NDS / NDSL Game Boy mode
- ❌ Unstable on:
    - EXEQ Game Box (clone console)
	
##NOTES
- ⚠Some GBAOAC devices such as the EXEQ Game Box SP don't play nice with flash carts as it doesn't have the same wait time. Thus, ROMs boot faster and the flash cart does not have enough time to prepare. Try to toggle "Boot games through BIOS" each time you exit a GBC/GB game.
    - Alternative method for GBAOC devices: Create a ROM compilation using "goombafront" and sideload the gba file. This process is tedious, but it works best for clones like these.
- ⚠WARNING: The cart **appears** to not have enough time to properly load both emulator and ROM if you skip the BIOS. It's better to leave that kernel option "Boot games through BIOS" as 1 (on).

## Planned features
- NGPGBA
- WasabiGBA

## Links / Binaries
[GBATemp Bleeding-edge kernel thread](https://gbatemp.net/threads/scfw-bleeding-edge-modular-kernel-branch.656629/)

## Credits
[metroid maniac](https://github.com/metroid-maniac) - Main developer  
[OmDRetro](https://github.com/OmDRetro) - Kernel enhancements
