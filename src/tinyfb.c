// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

#include <stdint.h>

#define COLOR_FG	0xffffffff
#define COLOR_BG	0x00000000

#define FONT_W		4
#define FONT_H		6
static const uint32_t font[] = {
	0b000001110101010101010111, // 0
	0b000001110010001000110010, // 1
	0b000001110001011101000111, // 2
	0b000001110100011101000111, // 3
	0b000001000100011101010101, // 4
	0b000001110100011100010111, // 5
	0b000001110101011100010111, // 6
	0b000000100010010001000111, // 7
	0b000001110101011101010111, // 8
	0b000001110100011101010111, // 9
	0b000001010101011101010010, // a
	0b000001110101011100010001, // b
	0b000001110001000100010111, // c
	0b000001110101011101000100, // d
	0b000001110001011100010111, // e
	0b000000010001011100010111, // f
};

static void put_nibble(uint64_t nibble, uint32_t **fb, uint64_t stride)
{
	uint32_t glyph = font[nibble & 0xf];
	int line, col;

	for (line = 0; line < FONT_H; ++line) {
		for (col = 0; col < FONT_W; ++col) {
			(*fb)[line * stride + col] = (glyph & 1 ? COLOR_FG : COLOR_BG);
			glyph >>= 1;
		}
	}

	*fb += FONT_W;
}

void put_hex(uint64_t val, uint32_t **fb, uint64_t stride)
{
	int i;

	for (i = 0; i < 16; ++i) {
		put_nibble(val, fb, stride);
		val >>= 4;
	}

	*fb += FONT_W;
}
