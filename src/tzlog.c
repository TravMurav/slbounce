// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

#include <stdint.h>

#include <efi.h>
#include <efilib.h>

#include "arch.h"
#include "reg.h"
#include "tzlog.h"

struct tzlog {
	uint64_t base;
	uint64_t size;
	uint64_t hyp_addr_offt;
	uint64_t hyp_size_offt;
};

struct tzlog sc7180_tzlog = {
	.base = 0x146aa720,
	.size = 0x3000,
	.hyp_addr_offt = 0x410,
	.hyp_size_offt = 0x414,
};

struct tzlog *soc_tzlog = &sc7180_tzlog;

static void puts_skipmnl(char *log, uint64_t log_size)
{
	char *p = log;
	for (int i = 0; i < log_size; ++i) {
		if (p[i] && !(p[i] == '\n' && p[i-2] == '\n'))
			Print(L"%c", readb(p+i));
	}
}

void dump_hyp_logs(void)
{
	/* tzlog at 0x146aa720 + 0x410 */
	uint64_t log_phys = readl(soc_tzlog->base + soc_tzlog->hyp_addr_offt); // 0x801fa000
	uint64_t log_size = readl(soc_tzlog->base + soc_tzlog->hyp_size_offt); // 0x00002000

	clear_dcache_range(log_phys, log_size);

	char *log = (char *)log_phys;

	Print(L"===== HYP logs ====\n");
	puts_skipmnl(log, log_size);
	Print(L"===================\n");
}

#define SCM_QSEEOS_FNID(s, c) (((((s) & 0xFF) << 8) | ((c) & 0xFF)) | 0x32000000)

#define QSEE_LOG_BUF_SIZE 0x10000

/* TZ Diagnostic Area legacy version number */
#define TZBSP_DIAG_MAJOR_VERSION_LEGACY	2
/*
 * Preprocessor Definitions and Constants
 */
#define TZBSP_MAX_CPU_COUNT 0x08
/*
 * Number of VMID Tables
 */
#define TZBSP_DIAG_NUM_OF_VMID 16
/*
 * VMID Description length
 */
#define TZBSP_DIAG_VMID_DESC_LEN 7
/*
 * Number of Interrupts
 */
#define TZBSP_DIAG_INT_NUM  32
/*
 * Length of descriptive name associated with Interrupt
 */
#define TZBSP_MAX_INT_DESC 16
/*
 * VMID Table
 */
struct tzdbg_vmid_t {
	uint8_t vmid; /* Virtual Machine Identifier */
	uint8_t desc[TZBSP_DIAG_VMID_DESC_LEN];	/* ASCII Text */
};
/*
 * Boot Info Table
 */
struct tzdbg_boot_info_t {
	uint32_t wb_entry_cnt;	/* Warmboot entry CPU Counter */
	uint32_t wb_exit_cnt;	/* Warmboot exit CPU Counter */
	uint32_t pc_entry_cnt;	/* Power Collapse entry CPU Counter */
	uint32_t pc_exit_cnt;	/* Power Collapse exit CPU counter */
	uint32_t warm_jmp_addr;	/* Last Warmboot Jump Address */
	uint32_t spare;	/* Reserved for future use. */
};
/*
 * Reset Info Table
 */
struct tzdbg_reset_info_t {
	uint32_t reset_type;	/* Reset Reason */
	uint32_t reset_cnt;	/* Number of resets occured/CPU */
};
/*
 * Interrupt Info Table
 */
struct tzdbg_int_t {
	/*
	 * Type of Interrupt/exception
	 */
	uint16_t int_info;
	/*
	 * Availability of the slot
	 */
	uint8_t avail;
	/*
	 * Reserved for future use
	 */
	uint8_t spare;
	/*
	 * Interrupt # for IRQ and FIQ
	 */
	uint32_t int_num;
	/*
	 * ASCII text describing type of interrupt e.g:
	 * Secure Timer, EBI XPU. This string is always null terminated,
	 * supporting at most TZBSP_MAX_INT_DESC characters.
	 * Any additional characters are truncated.
	 */
	uint8_t int_desc[TZBSP_MAX_INT_DESC];
	uint64_t int_count[TZBSP_MAX_CPU_COUNT]; /* # of times seen per CPU */
};
/*
 * Log ring buffer position
 */
struct tzdbg_log_pos_t {
	uint16_t wrap;
	uint16_t offset;
};
 /*
 * Log ring buffer
 */
struct tzdbg_log_t {
	struct tzdbg_log_pos_t	log_pos;
	/* open ended array to the end of the 4K IMEM buffer */
	uint8_t					log_buf[];
};
/*
 * Diagnostic Table
 * Note: This is the reference data structure for tz diagnostic table
 * supporting TZBSP_MAX_CPU_COUNT, the real diagnostic data is directly
 * copied into buffer from i/o memory.
 */
struct tzdbg_t {
	uint32_t magic_num;
	uint32_t version;
	/*
	 * Number of CPU's
	 */
	uint32_t cpu_count;
	/*
	 * Offset of VMID Table
	 */
	uint32_t vmid_info_off;
	/*
	 * Offset of Boot Table
	 */
	uint32_t boot_info_off;
	/*
	 * Offset of Reset info Table
	 */
	uint32_t reset_info_off;
	/*
	 * Offset of Interrupt info Table
	 */
	uint32_t int_info_off;
	/*
	 * Ring Buffer Offset
	 */
	uint32_t ring_off;
	/*
	 * Ring Buffer Length
	 */
	uint32_t ring_len;
	/*
	 * VMID to EE Mapping
	 */
	struct tzdbg_vmid_t vmid_info[TZBSP_DIAG_NUM_OF_VMID];
	/*
	 * Boot Info
	 */
	struct tzdbg_boot_info_t  boot_info[TZBSP_MAX_CPU_COUNT];
	/*
	 * Reset Info
	 */
	struct tzdbg_reset_info_t reset_info[TZBSP_MAX_CPU_COUNT];
	uint32_t num_interrupts;
	struct tzdbg_int_t  int_info[TZBSP_DIAG_INT_NUM];
	/*
	 * We need at least 2K for the ring buffer
	 */
	struct tzdbg_log_t ring_buffer;	/* TZ Ring Buffer */
};
struct hypdbg_log_pos_t {
	uint16_t wrap;
	uint16_t offset;
};
struct hypdbg_boot_info_t {
	uint32_t warm_entry_cnt;
	uint32_t warm_exit_cnt;
};
struct hypdbg_t {
	/* Magic Number */
	uint32_t magic_num;
	/* Number of CPU's */
	uint32_t cpu_count;
	/* Ring Buffer Offset */
	uint32_t ring_off;
	/* Ring buffer position mgmt */
	struct hypdbg_log_pos_t log_pos;
	uint32_t log_len;
	/* S2 fault numbers */
	uint32_t s2_fault_counter;
	/* Boot Info */
	struct hypdbg_boot_info_t boot_info[TZBSP_MAX_CPU_COUNT];
	/* Ring buffer pointer */
	uint8_t log_buf_p[];
};
/*
 * Enumeration order for VMID's
 */
enum tzdbg_stats_type {
	TZDBG_BOOT = 0,
	TZDBG_RESET,
	TZDBG_INTERRUPT,
	TZDBG_VMID,
	TZDBG_GENERAL,
	TZDBG_LOG,
	TZDBG_QSEE_LOG,
	TZDBG_HYP_GENERAL,
	TZDBG_HYP_LOG,
	TZDBG_STATS_MAX
};

static EFI_PHYSICAL_ADDRESS logs_phys;

void register_tz_logs(void)
{
	EFI_STATUS ret;
	UINT64 logs_pages = QSEE_LOG_BUF_SIZE / 4096 + 1;

	if (logs_phys != 0)
		return;

	ret = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiReservedMemoryType, logs_pages, &logs_phys);
	if (EFI_ERROR(ret))
		return;

	Print(L"Allocated %d pages at 0x%x (logs)\n", logs_pages, logs_phys);
	SetMem((void*)logs_phys, 4096 * logs_pages, 0);

	uint64_t fn_id = SCM_QSEEOS_FNID(1, 6) | 0x40000000;
	uint64_t smcret = smc(fn_id, 0x22, logs_phys, QSEE_LOG_BUF_SIZE);
	Print(L"tzlogs smc ret = 0x%x\n", (int)smcret);

}

void dump_tz_logs(void)
{
	uint64_t tzdiag_phys = readl(soc_tzlog->base);

	struct tzdbg_t *diag_buf = (void *)tzdiag_phys;

	Print(L"tzdbg: [0x%x v%x] %d cpus\n",
			diag_buf->magic_num, diag_buf->version, diag_buf->cpu_count, diag_buf->ring_off, diag_buf->ring_len);

	char *log = (char *)(tzdiag_phys + diag_buf->ring_off);
	Print(L"===== TZ logs =====\n");
	puts_skipmnl(log, diag_buf->ring_len);
	Print(L"===================\n");

	log = (char *)logs_phys;
	Print(L"===== QHEE logs =====\n");
	puts_skipmnl(log, QSEE_LOG_BUF_SIZE);
	Print(L"===================\n");
}
