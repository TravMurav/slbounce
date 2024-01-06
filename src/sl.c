// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

#define EFI_DEBUG 1

#include <stdint.h>

#include <efi.h>
#include <efilib.h>
#include <efidebug.h>

#include <sysreg/currentel.h>

#include "winnt.h"

#include "util.h"
#include "arch.h"
#include "sl.h"

EFI_STATUS sl_get_cert_entry(UINT8 *tcb_data, UINT8 **data, UINT64 *size)
{
	PIMAGE_DOS_HEADER pe = (PIMAGE_DOS_HEADER)tcb_data;

	if (pe->e_magic != IMAGE_DOS_SIGNATURE)
		return EFI_INVALID_PARAMETER;

	PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)((UINT8 *)pe + pe->e_lfanew);

	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return EFI_INVALID_PARAMETER;

	if (nt->OptionalHeader.Magic != 0x20b)
		return EFI_INVALID_PARAMETER;

	PIMAGE_DATA_DIRECTORY security = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];

	Print(L"Security entry at offt 0x%x with size 0x%x\n", security->VirtualAddress, security->Size);

	*data = (UINT8 *)pe + security->VirtualAddress;
	*size = security->Size;

	PWIN_CERTIFICATE cert = (PWIN_CERTIFICATE)*data;

	Print(L"Cert: Len=0x%x, Rev=0x%x, Type=0x%x\n", cert->dwLength, cert->wRevision, cert->wCertificateType);

	if (cert->wRevision != 0x200 || cert->wCertificateType != 2)
		return EFI_INVALID_PARAMETER;

	return EFI_SUCCESS;
}

static void dump_smc_params(struct sl_smc_params *dat)
{
	Print(L"SMC Params: [%d/%d/0x%x] id=%d | pe: 0x%x (0x%x b) | arg: 0x%x (0x%x b)\n",
		dat->a, dat->b, dat->version, dat->num, dat->pe_data, dat->pe_size, dat->arg_data, dat->arg_size);
}

static void dump_hyp_logs()
{
	/* tzlog at 0x146aa720 + 0x410 */
	uint64_t log_phys = 0x801fa000;
	uint64_t log_size = 0x00002000;

	//uefi_call_wrapper(BS->Stall, 1, 1000000);
	clear_dcache_range(log_phys, log_size);

	char *log = (char *)log_phys;

	char *p = log;

	Print(L"===== HYP logs ====\n");

	for (int i = 0; i < log_size; ++i) {
		if (p[i] && !(p[i] == '\n' && p[i-2] == '\n'))
			Print(L"%c", p[i]);
	}

	Print(L"===================\n");
}

EFI_STATUS sl_bounce(EFI_FILE_HANDLE tcblaunch)
{
	EFI_STATUS ret = EFI_SUCCESS;
	uint64_t smcret = 0;

	/* Allocate and load the tcblaunch.exe file. */

	UINT64 tcb_size = FileSize(tcblaunch);
	UINT64 tcb_pages = tcb_size / 4096 + 1;
	EFI_PHYSICAL_ADDRESS tcb_phys = 0;

	ret = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, tcb_pages, &tcb_phys);
	if (EFI_ERROR(ret))
		goto exit;

	Print(L"Allocated %d pages at 0x%x (for %d bytes in file)\n", tcb_pages, tcb_phys, tcb_size);

	UINT8 *tcb_data = (UINT8 *)tcb_phys;

	FileRead(tcblaunch, tcb_data, tcb_size);

	/* Extract the certificate/signature section address. */

	UINT8 *cert_data;
	UINT64 cert_size;

	ret = sl_get_cert_entry(tcb_data, &cert_data, &cert_size);
	if (EFI_ERROR(ret))
		goto exit_tcb;

	UINT64 cert_pages = cert_size / 4096 + 1;

	/* Allocate a buffer for Secure Launch procecss. */

	EFI_PHYSICAL_ADDRESS buf_phys = 0;
	UINT64 buf_pages = 27 + cert_pages + 3;

	ret = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, buf_pages, &buf_phys);
	if (EFI_ERROR(ret))
		goto exit_tcb;

	Print(L"Allocated %d pages at 0x%x (cert pages = %d)\n", buf_pages, buf_phys, cert_pages);

	/*
	 * Our memory map for pages in this buffer is:
	 *
	 * | Off| Usage			|
	 * |----|-----------------------|
	 * | 0	| (Unused)		|
	 * | 1	| SMC data		|
	 * | 2	| TZ data		|
	 * | 3	| TCB's CRT memory	|
	 * | 27	| Cert entry		|
	 * | ??	| TCG Log		|
	 *
	 */

	UINT8 *buf = (UINT8 *)buf_phys;
	SetMem(buf, 4096 * buf_pages, 0);

	struct sl_smc_params *smc_data = (struct sl_smc_params *)(buf + 4096 * 1);
	smc_data->a = 1;
	smc_data->b = 0;
	smc_data->version = 0x10;
	smc_data->num = 0;
	smc_data->pe_data = (uint64_t)tcb_data;
	smc_data->pe_size = (uint64_t)tcb_size;

	struct sl_tz_data *tz_data = (struct sl_tz_data *)(buf + 4096 * 2);
	SetMem(tz_data, 4096, 0);

	tz_data->version = 1;
	tz_data->cert_offt = 4096 * 25;
	tz_data->cert_size = cert_size;

	UINT8 *buf_cert_data = (UINT8 *)tz_data + tz_data->cert_offt;
	CopyMem(buf_cert_data, cert_data, cert_size);

	tz_data->tcg_offt = tz_data->cert_offt + tz_data->cert_size;
	tz_data->tcg_size = 4096 * 2;
	tz_data->tcg_used = 0;
	tz_data->tcg_ver = 2;

	tz_data->this_size = 4096 * (buf_pages - 3);
	tz_data->this_phys = (uint64_t)tz_data;

	tz_data->crt_offt = 4096 * 1;
	tz_data->crt_pages_cnt = 24;

	/* Set up return code path for when tcblaunch.exe fails to start */

	tz_data->tb_entry_point = (uint64_t)tb_func;
	tz_data->tb_virt = (((uint64_t)tb_func) / 4096 - 1) * 4096;
	tz_data->tb_phys = tz_data->tb_virt;
	tz_data->tb_size = 4096 * 2;

	Print(L"TB entrypoint is 0x%x, section is at 0x%x, size= 0x%x\n",
		tz_data->tb_entry_point, tz_data->tb_virt, tz_data->tb_size);

	/* Allocate (bogus) boot parameters for tcb. */

	EFI_PHYSICAL_ADDRESS bootparams_phys = 0;
	UINT64 bootparams_pages = 3;

	ret = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, bootparams_pages, &bootparams_phys);
	if (EFI_ERROR(ret))
		goto exit_buf;

	Print(L"Allocated %d pages at 0x%x\n", bootparams_pages, bootparams_phys);

	struct sl_boot_params *bootparams = (struct sl_boot_params *)bootparams_phys;
	SetMem(bootparams, 4096 * bootparams_pages, 0xff);

	tz_data->boot_params = (uint64_t)bootparams;
	tz_data->boot_params_size = 4096 * bootparams_pages;

	smc_data->arg_data = tz_data->this_phys;
	smc_data->arg_size = tz_data->this_size;

	/* Do some sanity checks */

	/* mssecapp.mbn */
	ASSERT(tz_data->version == 1);
	ASSERT(tz_data->cert_offt > 0x17);
	ASSERT(tz_data->cert_size != 0);
	ASSERT(tz_data->this_size > tz_data->cert_offt);
	ASSERT(tz_data->this_size - tz_data->cert_offt >= tz_data->cert_size);
	ASSERT(tz_data->tcg_offt > 0x17);
	ASSERT(tz_data->tcg_size != 0);
	ASSERT(tz_data->this_size > tz_data->tcg_offt);
	ASSERT(tz_data->this_size - tz_data->tcg_offt >= tz_data->tcg_size);

	//tz_data->version = 2;
	//tz_data->cert_offt = 2;

	//goto exit_bp; // ===========================================================================================

	/* Perform the SMC calls to launch the tcb with our data */

	Print(L"Data creation is done. Trying to bounce...\n");

	dump_smc_params(smc_data);

	clear_dcache_range((uint64_t)tcb_data, 4096 * tcb_pages);
	clear_dcache_range((uint64_t)buf, 4096 * buf_pages);
	clear_dcache_range((uint64_t)bootparams, 4096 * bootparams_pages);

	smc_data->num = SL_CMD_IS_AVAILABLE;
	clear_dcache_range((uint64_t)smc_data, 4096 * 1);

	Print(L" == Available: ");
	smcret = smc(SMC_SL_ID, (uint64_t)smc_data, smc_data->num, 0);
	Print(L"0x%x\n", smcret);
	if (smcret)
		goto exit_corrupted;

	smc_data->num = SL_CMD_AUTH;
	clear_dcache_range((uint64_t)smc_data, 4096 * 1);

	Print(L" == Auth: ");
	smcret = smc(SMC_SL_ID, (uint64_t)smc_data, smc_data->num, 0);
	Print(L"0x%x\n", smcret);
	if (smcret)
		goto exit_corrupted;

	smc_data->num = SL_CMD_LAUNCH;
	clear_dcache_range((uint64_t)smc_data, 4096 * 1);

	Print(L" == Launch: ");
	smcret = smc(SMC_SL_ID, (uint64_t)smc_data, smc_data->num, 0);
	Print(L"0x%x\n", smcret);
	if (smcret)
		goto exit_corrupted;

	Print(L"Bounce is done. Cleaning up.\n");

exit_bp:
	ret = uefi_call_wrapper(BS->FreePages, 2, bootparams_phys, bootparams_pages);
	if (EFI_ERROR(ret))
		return ret;

exit_buf:
	ret = uefi_call_wrapper(BS->FreePages, 2, buf_phys, buf_pages);
	if (EFI_ERROR(ret))
		return ret;

exit_tcb:
	ret = uefi_call_wrapper(BS->FreePages, 2, tcb_phys, tcb_pages);
	if (EFI_ERROR(ret))
		return ret;
exit:
	return ret;

exit_corrupted:
	Print(L"===========================================\n");
	Print(L"      SMC failed with ret = 0x%x\n", smcret);
	Print(L" Assume this system is in corrupted state!\n");
	Print(L"===========================================\n");

	dump_hyp_logs();

	return EFI_UNSUPPORTED;
}
