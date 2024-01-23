/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

.global tb_entry
tb_entry:
	mov	x20, x0			// x0 contains struct address, save.
	hvc	0x1			// Magic moment here - The call will return in EL2

	/* Disable MMU for now */
	mrs	x0, sctlr_el2
	mov	x1, #~0b1000000000101
	and	x0, x0, x1
	msr	sctlr_el2, x0
	isb

	/* Make sure interrupts are disabled too */
	msr	daifset, #0b1111
	isb

	msr	hcr_el2, xzr
	msr	vbar_el2, xzr

	b	_psci_off

_psci_off:
	mov	x0, #0x84000000
	add	x0, x0, #0x8
	smc	0x0
