// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

#include <stdint.h>

#include "arch.h"
#include "tinyfb.h"
#include "smp.h"

#define FB_BASE 0x9bc00000

uint8_t cpu1_stack[1024];

void second_cpu_entry(void)
{
	uint32_t *fb = (uint32_t*)FB_BASE + 1920 * 16;
	put_hex(0x0123456789abcdef, &fb, 1920);

	while(1)
		;
}

void _second_cpu_start();

uint64_t spin_up_second_cpu(void)
{
	uint64_t sp = &cpu1_stack[512];
	sp &= 0xfffffff0;

	return _smc(0xc4000003, 0x1, _second_cpu_start, sp);
}
