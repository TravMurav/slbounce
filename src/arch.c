// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

#include <stdint.h>

#include <efi.h>
#include <efilib.h>

#include <sysreg/ctr_el0.h>
#include <sysreg/daif.h>

#include <sysreg/cntp_ctl_el0.h>
#include <sysreg/cntp_tval_el0.h>

#include "sl.h"
#include "arch.h"

void clear_dcache_range(uint64_t start, uint64_t size)
{
	uint64_t cache_line_size = (1 << read_ctr_el0().dminline) * 4;
	uint64_t i, end = start + size;

	start &= ~(cache_line_size - 1);

	for (i = start; i < end; i += cache_line_size) {
		__asm__ volatile("dc civac, %0\n" : : "r" (i) :"memory");
	}
}

uint64_t _smc(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3)
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

uint64_t smc(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3)
{
	uint64_t ret;
	union daif daif_bak = read_daif();

	/*
	 * Hyp code will call into TZ at some point and if an
	 * interrupt happens while the cpu is in TZ, it will
	 * return to our code (!!!) with ret=1 (INTERRUPTED).
	 *
	 * There is no way to re-enter hyp and make it re-enter
	 * TZ to continue the task so we must disable interrupts
	 * to make sure it finishes properly.
	 */
	read_modify_write_daif( .i=1 );

	ret = _smc(x0, x1, x2, x3);

	unsafe_write_daif(daif_bak);

	return ret;
}

void psci_off(void)
{
	__asm__ volatile(
		"mov x0, #0x84000000\n\t"
		"add x0, x0, #8\n\t"
		"smc #0\n\t"
	);
}

void psci_reboot(void)
{
	__asm__ volatile(
		"mov x0, #0x84000000\n\t"
		"add x0, x0, #9\n\t"
		"smc #0\n\t"
	);
}
