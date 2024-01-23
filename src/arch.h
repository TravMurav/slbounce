#ifndef ARCH_H
#define ARCH_H

#include <stdint.h>
#include "sl.h"

void clear_dcache_range(uint64_t start, uint64_t size);
uint64_t smc(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3);
void psci_off(void);

/* In trans.s */
void tb_entry(void);

#endif
