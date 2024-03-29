@ GBA ROM entrypoint at 0x08000000 is patched to patched_entrypoint.
@ patched_entrypoint installs irq_stub at 0x03ffffe0 
@ and points the bios irq handler to it at 0x03fffffc
@ before jumping to the original entrypoint.
@ If this handler is already installed, it instead loads
@ code to iwram to reset the Supercard's state and then 
@ boots the firmware there

.arm

patched_entrypoint:
	mov r0, #0x04000000
	ldr r1, [r0, # -4]
	sub r0, # 0x20
	cmp r0, r1
	beq do_reset
	
install_hook:
	adr r1, hook
	ldm r1, {r4-r7}
	stm r0, {r4-r7}
	str r0, [r0, # 0x1c]
	ldr pc, original_entrypoint

@ extremely optimised ARM code fits in 16 free bytes at 0x03ffffe0
@ branch to original user IRQ handler if face buttons held and dpad not
@ else branch to entrypoint for reset 
hook:
	ldrb r1, [r0, # 0x130]
	cmp r1, # 0x00f0
	ldrne pc, [pc, # 4]
	mov pc, # 0x08000000
	
do_reset:
	mov r0, # 0x9f 
	msr cpsr, r0
	
@ disable sound, dma, and interrupts
	mov r0, # 0x04000000
	strh r0, [r0, # 0x84]
	strh r0, [r0, # 0xba]
	strh r0, [r0, # 0xc6]
	strh r0, [r0, # 0xd2]
	strh r0, [r0, # 0xde]
	strb r0, [r0, # 0x208]

@ copy to stack and execute
	adr r1, do_reset_iwram
	adr r2, do_reset_iwram_end
do_reset_loop:
	ldr r3, [r2, #-4]!
	str r3, [sp, #-4]!
	cmp r1, r2
	blt do_reset_loop
	mov pc, sp

do_reset_iwram:
	mov r0, # 0x0a000000
	sub r0, r0, #0x02
	mov r1, # 0x005a
	add r1, # 0xa500
	strh r1, [r0]
	strh r1, [r0]
	mov r1, # 4
	strh r1, [r0]
	strh r1, [r0]
	@ In ARM mode, SWI indexes must be shifted left 16 bits.
	swi 0x260000
do_reset_iwram_end:

original_entrypoint:
	@ patcher inserts ARM code address here
	