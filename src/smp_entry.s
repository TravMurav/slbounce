.equ	FB_BASE,	0x9bc00000
.equ	FB_STRIDE,	(1920 * 4)

.global _second_cpu_start
_second_cpu_start:

	msr	daifset, #0b1111
	isb

	mov	x20, x0			// x0 contains sp address, save.
	mov	x7, FB_BASE
	add	x7, x7, #(FB_STRIDE * 8)

	mov	x0, #0xFFFFFFFF
	str	w0, [x7]

	mov	x0, #0xFFFFFFFF
	str	w0, [x7, #8]

	mov	x0, x20
	bl	put_64

	mrs	x0, currentel
	bl	put_64

	mov	x0, x20
	bl	put_64

	movz	x0, #0xcdef, lsl #0
	movk	x0, #0x89ab, lsl #16
	movk	x0, #0x4567, lsl #32
	movk	x0, #0x0123, lsl #48
	bl	put_64

	mov	sp, x20
	//bl	second_cpu_entry


	b	count_forever
	b	halt

count_forever:
	mov	x0, xzr

cf_repeat:
	add	x0, x0, #1
	bl	put_64
	sub	x7, x7, #(16 * 17)

	b	cf_repeat

halt:
	wfe
	b	halt


/*
 * x0: color
 * x7: fb base
 *
 * x9: offset
 * x10: tmp
 * x11: px color
 */
dump_bits:
	mov	x9, #(64*8)

db_repeat:
	and	x10, x0, #1
	mov	x11, #0x00ff0000
	cbz	x10, dump_zero
	mov	x11, #0x0000ff00
dump_zero:
	asr	x0, x0, #1
	str	w11, [x7, x9]
	sub	x9, x9, #8
	cbnz	x9, db_repeat

	ret



/*
 * Arguments:
 * 	x0: value
 * 	x7: current fb base (updated)
 *
 * Clobbers:
 *	x9-x14: put_nibble
 *	x15: lr
 *	x16: nibbles_left
 */
put_64:
	mov	x15, lr
	mov	x16, #16

p64_repeat:
	ror	x0, x0, #60
	bl	put_nibble

	sub	x16, x16, #1
	cbnz	x16, p64_repeat

	add	x7, x7, #16
	mov	lr, x15
	ret

/*
 * Arguments:
 * 	x0: nibble
 * 	x7: current fb base (updated)
 *
 * Clobbers:
 *	x9:  glyph
 *	x10: fb offset
 *	x11: line count
 *	x12: col count
 *	x13: pixel color
 *	x14: tmp
 */
put_nibble:
	mov	x10, xzr

	// load glyph
	and	x9, x0, #0xf	// load nibble offset
	mov	x14, #4
	mul	x9, x9, x14
	adr	x14, font	// get base of the font data (pc relative I hope)
	add	x9, x9, x14	// add offset to the base
	ldr	w9, [x9]	// load the glyph from the font table

	mov	x11, #6		// draw 6 lines
pn_line:
	mov	x12, #4		// draw 4 columns
pn_col:
	and	x13, x9, #1	// get the lowest bit of the font
	mov	x14, #0xffffffff
	mul	x13, x13, x14	// if the bit is set then white else black

	str	w13, [x7, x10]	// write pixel at fb+offt
	add	x10, x10, #4	// offt + px size
	asr	x9, x9, #1	// font >> 1

	sub	x12, x12, #1	// col_left--
	cbnz	x12, pn_col	// repeat for 4 cols in a line

	mov	x13, (FB_STRIDE - 16)	// advance offt to the next line
	add	x10, x10, x13

	sub	x11, x11, #1	// lines_left--
	cbnz	x11, pn_line	// repeat for 6 lines

	add	x7, x7, #16	// advance glyph position 4 pixels right
	ret

psci_off:
	mov	x0, #0x84000000
	add	x0, x0, #0x8
	smc	0x0

.data
.align 3
font:
	.word	0b000001110101010101010111 // 0
	.word	0b000001110010001000110010 // 1
	.word	0b000001110001011101000111 // 2
	.word	0b000001110100011101000111 // 3
	.word	0b000001000100011101010101 // 4
	.word	0b000001110100011100010111 // 5
	.word	0b000001110101011100010111 // 6
	.word	0b000000100010010001000111 // 7
	.word	0b000001110101011101010111 // 8
	.word	0b000001110100011101010111 // 9
	.word	0b000001010101011101010010 // a
	.word	0b000001110101011100010001 // b
	.word	0b000001110001000100010111 // c
	.word	0b000001110101011101000100 // d
	.word	0b000001110001011100010111 // e
	.word	0b000000010001011100010111 // f
