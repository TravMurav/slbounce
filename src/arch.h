#ifndef ARCH_H
#define ARCH_H

#include <stdint.h>

void clear_dcache_range(uint64_t start, uint64_t size);
uint64_t smc(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3);
void psci_off(void);

/* In trans.s */
void tb_entry(void);
int tb_setjmp(uint64_t *jmp_buf) __attribute__((returns_twice));
int tb_longjmp(uint64_t *jmp_buf, uint64_t retval) __attribute__((noreturn));

extern uint64_t tb_jmp_buf[21];

#endif
