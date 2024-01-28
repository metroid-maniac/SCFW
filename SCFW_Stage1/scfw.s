cart_base:
.org 0x00
	b entrypoint
.org 0x04
	.fill 0x9c, 0, 0
.org 0xa0
	.ascii "DsBooterSCFW"
.org 0xac
	.ascii "PASSsc"
.org 0xb2
	.byte 0x96
.org 0xb4
	.fill 8, 0, 0
.org 0xbc
	.byte 0
.org 0xbd
	.fill 3, 0, 0


.org 0xc0
entrypoint:
	# copy and run this fn from RAM
	mov r0, #0x02000000
	adr r1, sc_mode_flash_rw
	adr r2, sc_mode_flash_rw_end
sc_mode_flash_rw_loop:
	ldr r3, [r1], # 4
	str r3, [r0], # 4
	cmp r1, r2
	blt sc_mode_flash_rw_loop
	mov lr, pc
	mov pc, # 0x02000000
	
	# detect GBA/NDS using mirroring
	mov r0, # 0x02000000
	mov r2, # 0
	str r2, [r0]
	add r1, r0, #0x00040000
	mov r2, # 1
	str r2, [r1]
	ldr r2, [r0]
	cmp r2, # 0
	beq load_nds


# load & execute multiboot gba rom
load_gba:
	adrl r0, gba_rom
	adrl r1, gba_rom_end
	mov r2, # 0x02000000
	add lr, r2, # 0x000000c0
load_gba_loop:
	ldr r3, [r0], # 4
	str r3, [r2], # 4
	cmp r0, r1
	blt load_gba_loop
	bx lr
# load & execute nds rom
load_nds:
	adrl r0, nds_rom
	ldr r1, [r0, # 0x20]
	ldr r2, [r0, # 0x28]
	ldr r3, [r0, # 0x2c]
	add r1, r1, r0 
	add r3, r3, r1
load_nds_arm9_loop:
	ldr r4, [r1], # 4
	str r4, [r2], # 4
	cmp r1, r3
	blt load_nds_arm9_loop
	ldr r1, [r0, # 0x30]
	ldr r2, [r0, # 0x38]
	ldr r3, [r0, # 0x3c]
	add r1, r1, r0
	add r3, r3, r1
load_nds_arm7_loop:
	ldr r4, [r1], # 4
	str r4, [r2], # 4
	cmp r1, r3
	blt load_nds_arm7_loop
	mov r1, # 0x02800000
	sub r1, r1, # 0x200
	ldr r2, [r0, # 0x24]
	str r2, [r1, # 0x24]
	ldr lr, [r0, # 0x34]
	bx lr

sc_mode_flash_rw:
	mov r0, # 0x0a000000
	sub r0, r0, #0x02
	mov r1, # 0x005a
	add r1, # 0xa500
	strh r1, [r0]
	strh r1, [r0]
	mov r1, # 4
	strh r1, [r0]
	strh r1, [r0]
	bx lr
sc_mode_flash_rw_end:

.balign 4, 0xff
gba_rom:
.incbin "../SCFW_Stage2_GBA/SCFW_Stage2_GBA_mb.gba"
gba_rom_end:

.balign 4, 0xff
nds_rom:
.incbin "../SCFW_Stage2_NDS/SCFW_Stage2_NDS.nds"
nds_rom_end:

