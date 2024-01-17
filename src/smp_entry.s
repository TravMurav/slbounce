.equ	FB_BASE,	0x9bc00000
.equ	FB_STRIDE,	(1920 * 4)

.equ	HYP_LOG_BASE,	0x801fa000
.equ	HYP_LOG_SIZE,	0x00002000

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

	isb

	//mov	sp, x20
	//bl	second_cpu_entry

	mov	x7, FB_BASE
	add	x7, x7, #(FB_STRIDE * 16)

	mov	x0, #'H'
	bl	put_char
	mov	x0, #'e'
	bl	put_char

	mov	x0, #0x20000
	bl	delay_count

	mov	x0, #'l'
	bl	put_char

	mov	x7, FB_BASE
	add	x7, x7, #(FB_STRIDE * 32)

	bl	dump_hyp_log

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
 * Arguments:
 * 	x0: delay count (~ 0x10000 for 1 sec?)
 * 	x7: current fb base (updated)
 *
 * Clobbers:
 *	x9-x16: put_nibble
 *	x17: lr
 */
delay_count:
	mov	x17, lr

dc_loop:
	bl	put_64
	sub	x7, x7, #(16 * 17)

	sub	x0, x0, #1
	cbnz	x0, dc_loop

	mov	lr, x17
	ret


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

/*
 * Arguments:
 * 	x0: char
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
put_char:
	mov	x10, xzr

	// load glyph
	and	x9, x0, #0xff	// load char offset
	mov	x14, #8		// each char is 8 bytes
	mul	x9, x9, x14
	adr	x14, ascii_font	// get base of the font data (pc relative I hope)
	add	x9, x9, x14	// add offset to the base
	ldr	x9, [x9]	// load the glyph from the font table

	mov	x11, #8		// draw 8 lines
pc_line:
	mov	x12, #8		// draw 8 columns
pc_col:
	and	x13, x9, #1	// get the lowest bit of the font
	mov	x14, #0xffffffff
	mul	x13, x13, x14	// if the bit is set then white else black

	str	w13, [x7, x10]	// write pixel at fb+offt
	add	x10, x10, #4	// offt + px size
	asr	x9, x9, #1	// font >> 1

	sub	x12, x12, #1	// col_left--
	cbnz	x12, pc_col	// repeat for 8 cols in a line

	mov	x13, (FB_STRIDE - 32)	// advance offt to the next line
	add	x10, x10, x13

	sub	x11, x11, #1	// lines_left--
	cbnz	x11, pc_line	// repeat for 8 lines

	add	x7, x7, #32	// advance glyph position 8 pixels right
	ret

/*
 * Arguments:
 * 	x7: current fb base (updated)
 *
 * Clobbers:
 *	x0: char codepoint arg
 *	x9-x14: put_char
		x14: tmp
 *	x15: lr
 *	x16: bytes left
 *	x17: current char addr
 *	x18: chars in current line
 */
dump_hyp_log:
	mov	x15, lr

	mov	x16, HYP_LOG_SIZE
	movz	x17, #0xa000, lsl #0	// HYP_LOG_BASE = 0x801fa000
	movk	x17, #0x801f, lsl #16

	//mov	x16, #10
	//adr	x17, test_string
	mov	x18, xzr

dhl_loop:
	ldurb	w0, [x17]
	//mov	w0, #'A'
	cbz	x0, dhl_next	// null, don't print.

	mov	x14, #'\n'
	cmp	x0, x14
	b.eq	dhl_nl		// newline

	mov	x14, #'\r'
	cmp	x0, x14
	b.eq	dhl_next

	bl	put_char
	add	x18, x18, #1
	b	dhl_next

dhl_nl:
	cbz	x18, dhl_next	// skip if nothing on a line
	mov	x14, #32	// 4*8=char width
	mul	x18, x18, x14	// width of the line so far.
	sub	x7, x7, x18	// now at the start of the line
	mov	x14, FB_STRIDE
	mov	x13, #10
	mul	x14, x14, x13
	add	x7, x7, x14	// advance to next line
	mov	x18, xzr

dhl_next:
	add	x17, x17, #1
	sub	x16, x16, #1
	cbnz	x16, dhl_loop

	mov	lr, x15
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

.align 4
ascii_font:
	.dword	0x0000000000000000   // U+0000 (nul)
	.dword	0x0000000000000000   // U+0001
	.dword	0x0000000000000000   // U+0002
	.dword	0x0000000000000000   // U+0003
	.dword	0x0000000000000000   // U+0004
	.dword	0x0000000000000000   // U+0005
	.dword	0x0000000000000000   // U+0006
	.dword	0x0000000000000000   // U+0007
	.dword	0x0000000000000000   // U+0008
	.dword	0x0000000000000000   // U+0009
	.dword	0x0000000000000000   // U+000A
	.dword	0x0000000000000000   // U+000B
	.dword	0x0000000000000000   // U+000C
	.dword	0x0000000000000000   // U+000D
	.dword	0x0000000000000000   // U+000E
	.dword	0x0000000000000000   // U+000F
	.dword	0x0000000000000000   // U+0010
	.dword	0x0000000000000000   // U+0011
	.dword	0x0000000000000000   // U+0012
	.dword	0x0000000000000000   // U+0013
	.dword	0x0000000000000000   // U+0014
	.dword	0x0000000000000000   // U+0015
	.dword	0x0000000000000000   // U+0016
	.dword	0x0000000000000000   // U+0017
	.dword	0x0000000000000000   // U+0018
	.dword	0x0000000000000000   // U+0019
	.dword	0x0000000000000000   // U+001A
	.dword	0x0000000000000000   // U+001B
	.dword	0x0000000000000000   // U+001C
	.dword	0x0000000000000000   // U+001D
	.dword	0x0000000000000000   // U+001E
	.dword	0x0000000000000000   // U+001F
	.dword	0x0000000000000000   // U+0020 (space)
	.dword	0x00180018183C3C18   // U+0021 (!)
	.dword	0x0000000000003636   // U+0022 (")
	.dword	0x0036367F367F3636   // U+0023 (#)
	.dword	0x000C1F301E033E0C   // U+0024 ($)
	.dword	0x0063660C18336300   // U+0025 (%)
	.dword	0x006E333B6E1C361C   // U+0026 (&)
	.dword	0x0000000000030606   // U+0027 (')
	.dword	0x00180C0606060C18   // U+0028 (()
	.dword	0x00060C1818180C06   // U+0029 ())
	.dword	0x0000663CFF3C6600   // U+002A (*)
	.dword	0x00000C0C3F0C0C00   // U+002B (+)
	.dword	0x060C0C0000000000   // U+002C (,)
	.dword	0x000000003F000000   // U+002D (-)
	.dword	0x000C0C0000000000   // U+002E (.)
	.dword	0x000103060C183060   // U+002F (/)
	.dword	0x003E676F7B73633E   // U+0030 (0)
	.dword	0x003F0C0C0C0C0E0C   // U+0031 (1)
	.dword	0x003F33061C30331E   // U+0032 (2)
	.dword	0x001E33301C30331E   // U+0033 (3)
	.dword	0x0078307F33363C38   // U+0034 (4)
	.dword	0x001E3330301F033F   // U+0035 (5)
	.dword	0x001E33331F03061C   // U+0036 (6)
	.dword	0x000C0C0C1830333F   // U+0037 (7)
	.dword	0x001E33331E33331E   // U+0038 (8)
	.dword	0x000E18303E33331E   // U+0039 (9)
	.dword	0x000C0C00000C0C00   // U+003A (:)
	.dword	0x060C0C00000C0C00   // U+003B (;)
	.dword	0x00180C0603060C18   // U+003C (<)
	.dword	0x00003F00003F0000   // U+003D (=)
	.dword	0x00060C1830180C06   // U+003E (>)
	.dword	0x000C000C1830331E   // U+003F (?)
	.dword	0x001E037B7B7B633E   // U+0040 (@)
	.dword	0x0033333F33331E0C   // U+0041 (A)
	.dword	0x003F66663E66663F   // U+0042 (B)
	.dword	0x003C66030303663C   // U+0043 (C)
	.dword	0x001F36666666361F   // U+0044 (D)
	.dword	0x007F46161E16467F   // U+0045 (E)
	.dword	0x000F06161E16467F   // U+0046 (F)
	.dword	0x007C66730303663C   // U+0047 (G)
	.dword	0x003333333F333333   // U+0048 (H)
	.dword	0x001E0C0C0C0C0C1E   // U+0049 (I)
	.dword	0x001E333330303078   // U+004A (J)
	.dword	0x006766361E366667   // U+004B (K)
	.dword	0x007F66460606060F   // U+004C (L)
	.dword	0x0063636B7F7F7763   // U+004D (M)
	.dword	0x006363737B6F6763   // U+004E (N)
	.dword	0x001C36636363361C   // U+004F (O)
	.dword	0x000F06063E66663F   // U+0050 (P)
	.dword	0x00381E3B3333331E   // U+0051 (Q)
	.dword	0x006766363E66663F   // U+0052 (R)
	.dword	0x001E33380E07331E   // U+0053 (S)
	.dword	0x001E0C0C0C0C2D3F   // U+0054 (T)
	.dword	0x003F333333333333   // U+0055 (U)
	.dword	0x000C1E3333333333   // U+0056 (V)
	.dword	0x0063777F6B636363   // U+0057 (W)
	.dword	0x0063361C1C366363   // U+0058 (X)
	.dword	0x001E0C0C1E333333   // U+0059 (Y)
	.dword	0x007F664C1831637F   // U+005A (Z)
	.dword	0x001E06060606061E   // U+005B ([)
	.dword	0x00406030180C0603   // U+005C (\)
	.dword	0x001E18181818181E   // U+005D (])
	.dword	0x0000000063361C08   // U+005E (^)
	.dword	0xFF00000000000000   // U+005F (_)
	.dword	0x0000000000180C0C   // U+0060 (`)
	.dword	0x006E333E301E0000   // U+0061 (a)
	.dword	0x003B66663E060607   // U+0062 (b)
	.dword	0x001E3303331E0000   // U+0063 (c)
	.dword	0x006E33333e303038   // U+0064 (d)
	.dword	0x001E033f331E0000   // U+0065 (e)
	.dword	0x000F06060f06361C   // U+0066 (f)
	.dword	0x1F303E33336E0000   // U+0067 (g)
	.dword	0x006766666E360607   // U+0068 (h)
	.dword	0x001E0C0C0C0E000C   // U+0069 (i)
	.dword	0x1E33333030300030   // U+006A (j)
	.dword	0x0067361E36660607   // U+006B (k)
	.dword	0x001E0C0C0C0C0C0E   // U+006C (l)
	.dword	0x00636B7F7F330000   // U+006D (m)
	.dword	0x00333333331F0000   // U+006E (n)
	.dword	0x001E3333331E0000   // U+006F (o)
	.dword	0x0F063E66663B0000   // U+0070 (p)
	.dword	0x78303E33336E0000   // U+0071 (q)
	.dword	0x000F06666E3B0000   // U+0072 (r)
	.dword	0x001F301E033E0000   // U+0073 (s)
	.dword	0x00182C0C0C3E0C08   // U+0074 (t)
	.dword	0x006E333333330000   // U+0075 (u)
	.dword	0x000C1E3333330000   // U+0076 (v)
	.dword	0x00367F7F6B630000   // U+0077 (w)
	.dword	0x0063361C36630000   // U+0078 (x)
	.dword	0x1F303E3333330000   // U+0079 (y)
	.dword	0x003F260C193F0000   // U+007A (z)
	.dword	0x00380C0C070C0C38   // U+007B ({)
	.dword	0x0018181800181818   // U+007C (|)
	.dword	0x00070C0C380C0C07   // U+007D (})
	.dword	0x0000000000003B6E   // U+007E (~)
	.dword	0x0000000000000000   // U+007F

.align 3
test_string:
	.ascii	"Hellorld! This is a fun message!!\nNew line!\nThird line!\nFOURTH line!\naa\n\nbb\n"

