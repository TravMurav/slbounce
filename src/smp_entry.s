.equ	LINUX_BASE,	0xA0000000
.equ	LINUX_DTB,	0xA4000000


.global _asm_tb_entry
_asm_tb_entry:
	mov	x20, x0			// x0 contains struct address, save.
	hvc	0x1

	/* disable mmu */
	mrs	x0, sctlr_el2
	mov	x1, #~0b1000000000101
	and	x0, x0, x1
	msr	sctlr_el2, x0
	isb

	msr	daifset, #0b1111
	isb

	msr	hcr_el2, xzr
	msr	vbar_el2, xzr

	b	boot_linux

	b	psci_off

boot_linux:
	mov	x0, LINUX_DTB
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr

	mov	x4, LINUX_BASE
	br	x4

	b psci_off



psci_off:
	mov	x0, #0x84000000
	add	x0, x0, #0x8
	smc	0x0
