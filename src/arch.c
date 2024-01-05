// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

#include <stdint.h>

#include <sysreg/ctr_el0.h>

void clear_dcache_range(uint64_t start, uint64_t size)
{
	uint64_t cache_line_size = (1 << read_ctr_el0().dminline) * 4;
	uint64_t i, end = start + size;

	start = -cache_line_size & start;

	for (i = start; i < end; i += cache_line_size) {
		__asm__ volatile("dc civac, %0\n" : : "r" (i) :"memory");
	}
}

uint64_t smc(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3)
{
	register uint64_t r0 __asm__("r0") = x0;
	register uint64_t r1 __asm__("r1") = x1;
	register uint64_t r2 __asm__("r2") = x2;
	register uint64_t r3 __asm__("r3") = x3;

	__asm__ volatile(
		"smc	#0\n"
		: "+r" (r0) : : "r1", "r2", "r3"
	);
	return r0;
}

void psci_off(void)
{
	__asm__ volatile(
		"mov x0, #0x84000000\n\t"
		"add x0, x0, #8\n\t"
		"smc #0\n\t"
	);
}

void tb_func(struct sl_tb_data *tb_data)
{
	__asm__ volatile(
		"mov x0, #0x84000000\n\t"
		"add x0, x0, #8\n\t"
		"smc #0\n\t"
	);
}
