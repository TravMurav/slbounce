/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

/* System Control Register (EL2) */
.equ	SCTLR_EL2_RES0,		0b1111111111111111111011111111111100000101000100001100011110000000
.equ	SCTLR_EL2_RES1,		0b0000000000000000000000000000000000110000110001010000100000110000
.equ	SCTLR_EL2_M,		1 << 0	/* MPU enable. */

/* Counter-timer Hypervisor Control Register */
.equ	CNTHCTL_EL2_EL1PCTEN,	1 << 0	/* Traps accesses to the EL1 physical counter register to EL2. */
.equ	CNTHCTL_EL2_EL1PCEN,	1 << 1	/* Traps accesses to the EL1 physical timer registers to EL2. */

/* Hypervisor Configuration Register (EL2) */
.equ	HCR_EL2_TGE,		1 << 27	/* Traps general exceptions. */
.equ	HCR_EL2_E2H,		1 << 34	/* EL2 Host. */


/*
 * tb_entry() - Entry point for "Transition back" feature of tcblaunch.exe
 * x0: Pointer to tz_data.tb_data struct.
 */
.global tb_entry
tb_entry:
	mov	x9, x0			// x0 contains struct address, save.
	hvc	0x1			// Magic moment here - The call will return in EL2

	/* Disable MMU for now */
	mrs	x0, sctlr_el2
	bic	x0, x0, #SCTLR_EL2_M
	msr	sctlr_el2, x0
	isb

	/* Make sure interrupts are disabled too */
	msr	daifset, #0b1111
	isb

	ldr	x1, [x9, #8]		// tb_data->sp holds framebuffer base
	ldr	x2, [x9, #16]		// tb_data->tcr holds framebuffer stride
	cbnz	x1, draw_on_screen	// If we have a framebuffer, draw on it and hang.

	ldr	x0, [x9]		// tb_data->mair holds a pointer to our tb_jmp_buf
	mov	x1, #1
	bl	tb_longjmp

	b	_psci_off

draw_on_screen:
	mov	x0, #(16 * 4)
	mov	x4, #0x0000FF00
	mul	x0, x0, x2

draw_loop:
	str	w4, [x1, x0]
	sub	x0, x0, #4
	cbnz	x0, draw_loop

	b	halt


/* _psci_off() - Turn off the device */
_psci_off:
	mov	x0, #0xc4000000
	add	x0, x0, #0x8
	smc	0x0


/* halt() - Loop forever */
halt:
	wfe
	b	halt


.macro movq Xn, imm
	movz    \Xn, (\imm >> 0 ) & 0xFFFF
	movk    \Xn, (\imm >> 16) & 0xFFFF, lsl 16
	movk    \Xn, (\imm >> 32) & 0xFFFF, lsl 32
	movk    \Xn, (\imm >> 48) & 0xFFFF, lsl 48
.endm


/*
 * tb_setjmp() - setjmp that saves system registers too.
 * x0: Pointer to the register save array.
 *
 * Note that EL1 registers are saved.
 */
.global tb_setjmp
tb_setjmp:

	.macro st_sys sysreg, offt
		/* Save a system register */
		mrs	x2, \sysreg
		str	x2, [x0,\offt]
	.endm

	/* Save system registers. */

	/* AArch64Support.S */
	st_sys	vbar_el1,  #104
	st_sys	tpidr_el0, #112

	/* ArmLibSupport.S */
	st_sys	cpacr_el1, #120
	st_sys	ttbr0_el1, #128
	st_sys	tcr_el1,   #136
	st_sys	mair_el1,  #144
	st_sys	sctlr_el1, #152

	st_sys	daif,      #160

	/* Save calle-saved registers and the stack. */
	stp	x19, x20, [x0,#0]
	stp	x21, x22, [x0,#16]
	stp	x23, x24, [x0,#32]
	stp	x25, x26, [x0,#48]
	stp	x27, x28, [x0,#64]
	stp	x29, x30, [x0,#80]

	mov	x2, sp
	str	x2, [x0,#96]

	mov	x0, #0
	ret


/*
 * tb_longjmp() - longjmp that restores system registers too.
 * x0: Pointer to the register save array.
 * x1: Return code.
 *
 * Note that the registers are restored in EL2
 */
.global tb_longjmp
tb_longjmp:

	.macro ld_sys sysreg, offt
		/* Save a system register */
		ldr	x2, [x0,\offt]
		msr	\sysreg, x2
		isb
	.endm

	/* Restore system registers. */

	/*
	 * Invalidate all TLB.
	 * With workaround for Cortex-A76 r3p0 errata 1286807,
	 * tho probably doesn't matter for 1 core here.
	 */
	tlbi	alle2
	dsb	ish
	tlbi	alle2

	/* AArch64Support.S */
	ld_sys	vbar_el2,  #104
	ld_sys	tpidr_el0, #112

		//cptr_el2

	/* ArchInitialize() */
	movq	x2, (HCR_EL2_TGE | HCR_EL2_E2H)
	msr	hcr_el2, x2

	mov	x2, #(CNTHCTL_EL2_EL1PCTEN | CNTHCTL_EL2_EL1PCEN)
	msr	cnthctl_el2, x2

	/* ArmLibSupport.S */
	ld_sys	cpacr_el1, #120
	ld_sys	ttbr0_el2, #128
	ld_sys	tcr_el2,   #136
	ld_sys	mair_el2,  #144

	dsb	ish
	isb

	ldr	x2, [x0,#152]
	movq	x3, ~SCTLR_EL2_RES0
	and	x2, x2, x3
	movq	x3, SCTLR_EL2_RES1
	orr	x2, x2, x3
	msr	sctlr_el2, x2
	isb

	/* Now restore calle-saved registers and the stack */
	ldp	x19, x20, [x0,#0]
	ldp	x21, x22, [x0,#16]
	ldp	x23, x24, [x0,#32]
	ldp	x25, x26, [x0,#48]
	ldp	x27, x28, [x0,#64]
	ldp	x29, x30, [x0,#80]

	ldr	x2, [x0,#96]
	mov	sp, x2

	/* Finally, restore interrupts */
	ld_sys	daif,      #160

	cmp w1, 0
	csinc w0, w1, wzr, ne		// x0 = (x1 ? x1 : 1)
	br	x30


.data

.align	3
.global tb_jmp_buf
tb_jmp_buf:
	.zero	8 * 12	// x19-x30 - callee saved registers.
	.zero	8 * 1	// x31 - sp
	.zero	8 * 8	// System registers
