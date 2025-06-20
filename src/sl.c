// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

#include <stdint.h>

#include <efi.h>
#include <efilib.h>
#include <efidebug.h>

#include <sysreg/currentel.h>
#include <sysreg/daif.h>

#include "winnt.h"

#include "util.h"
#include "arch.h"
#include "sl.h"

/**
 * sl_get_cert_entry() - Get a pointer to the start of the security structure in PE.
 */
EFI_STATUS sl_get_cert_entry(UINT8 *tcb_data, UINT8 **data, UINT64 *size)
{
	if (!tcb_data || !data || !size)
		return EFI_INVALID_PARAMETER;

	PIMAGE_DOS_HEADER pe = (PIMAGE_DOS_HEADER)tcb_data;

	if (pe->e_magic != IMAGE_DOS_SIGNATURE)
		return EFI_INVALID_PARAMETER;

	PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)((UINT8 *)pe + pe->e_lfanew);

	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return EFI_INVALID_PARAMETER;

	if (nt->OptionalHeader.Magic != 0x20b)
		return EFI_INVALID_PARAMETER;

	PIMAGE_DATA_DIRECTORY security = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];

	Dbg(L"Security entry at offt 0x%x with size 0x%x\n", security->VirtualAddress, security->Size);

	*data = (UINT8 *)pe + security->VirtualAddress;
	*size = security->Size;

	PWIN_CERTIFICATE cert = (PWIN_CERTIFICATE)*data;

	Dbg(L"Cert: Len=0x%x, Rev=0x%x, Type=0x%x\n", cert->dwLength, cert->wRevision, cert->wCertificateType);

	if (cert->wRevision != 0x200 || cert->wCertificateType != 2)
		return EFI_INVALID_PARAMETER;

	return EFI_SUCCESS;
}

/**
 * sl_load_pe() - Load a PE image into memory.
 *
 * We want to make sure we just load the image header and the
 * sections into ram as-is since we expect them to be signature
 * checked later.
 */
EFI_STATUS sl_load_pe(UINT8 *load_addr, UINT64 load_size, UINT8 *pe_data, UINT64 pe_size)
{
	if (!load_addr || !load_size || !pe_data || !pe_size)
		return EFI_INVALID_PARAMETER;

	PIMAGE_DOS_HEADER pe = (PIMAGE_DOS_HEADER)pe_data;

	if (pe->e_magic != IMAGE_DOS_SIGNATURE)
		return EFI_INVALID_PARAMETER;

	PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)((UINT8 *)pe + pe->e_lfanew);

	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return EFI_INVALID_PARAMETER;

	if (nt->OptionalHeader.Magic != 0x20b)
		return EFI_INVALID_PARAMETER;

	if (nt->OptionalHeader.Subsystem != IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION)
		return EFI_INVALID_PARAMETER;

	SetMem(load_addr, load_size, 0);

	Dbg(L"Loadint PE header with %d bytes to 0x%x\n", nt->OptionalHeader.SizeOfHeaders, load_addr);

	CopyMem(load_addr, pe, nt->OptionalHeader.SizeOfHeaders); // Header

	PIMAGE_SECTION_HEADER headers = (PIMAGE_SECTION_HEADER)((UINT8*)nt + 0x108);
	UINT64 header_count = nt->FileHeader.NumberOfSections;

	// FIXME this should probably handle errors better...
	for (int i = 0; i < header_count; ++i) {
		Dbg(L" - Loading section '%.*a' with %d bytes from offt=0x%x to 0x%x\n",
			8, headers[i].Name, headers[i].SizeOfRawData,
			headers[i].PointerToRawData,
			load_addr + headers[i].VirtualAddress);

		ASSERT(headers[i].VirtualAddress + headers[i].SizeOfRawData < load_size);

		CopyMem(load_addr + headers[i].VirtualAddress,
			(UINT8*)pe + headers[i].PointerToRawData,
			headers[i].SizeOfRawData); // I hope rawdata size is correct, this is how much is hashed...
	}

	return EFI_SUCCESS;
}

uint64_t sl_smc(struct sl_smc_params *smc_data, enum sl_cmd cmd, uint64_t pe_data, uint64_t pe_size, uint64_t arg_data, uint64_t arg_size)
{
	/*
	 * Some versions of the hyp will clean the memory before
	 * unmapping it from EL2. We need to recreate the smc_data
	 * every time.
	 */
	smc_data->a = 1;
	smc_data->b = 0;
	smc_data->version = 0x10;
	smc_data->pe_data = pe_data;
	smc_data->pe_size = pe_size;
	smc_data->arg_data = arg_data;
	smc_data->arg_size = arg_size;

	smc_data->num = cmd;
	clear_dcache_range((uint64_t)smc_data, 4096 * 1);

	return smc(SMC_SL_ID, (uint64_t)smc_data, smc_data->num, 0);
}

EFI_STATUS sl_create_data(EFI_FILE_HANDLE tcblaunch, struct sl_smc_params **smcdata, uint64_t *pe_data, uint64_t *pe_size, uint64_t *arg_data, uint64_t *arg_size)
{
	EFI_STATUS ret = EFI_SUCCESS;

	/* Allocate and load the tcblaunch.exe file. */

	UINT64 tcb_size = FileSize(tcblaunch);
	UINT64 tcb_pages = 512; // FIXME: don't hardcode...
	EFI_PHYSICAL_ADDRESS tcb_phys = 0;

	ret = AllocateZeroPages(tcb_pages, &tcb_phys);
	if (EFI_ERROR(ret))
		goto exit;

	Dbg(L"Allocated %d pages at 0x%x (TCB)\n", tcb_pages, tcb_phys);

	UINT8 *tcb_data = (UINT8 *)tcb_phys;

	UINT8 *tcb_tmp_file = AllocatePool(tcb_size);
	if (!tcb_tmp_file) {
		Print(L"Can't allocate memory for the file\n");
		goto exit_tcb;
	}

	UINT64 read_tcb_size = FileRead(tcblaunch, tcb_tmp_file, tcb_size);
	ASSERT(read_tcb_size == tcb_size);

	/* Load the PE into memory */
	ret = sl_load_pe(tcb_data, tcb_pages * 4096, tcb_tmp_file, tcb_size);
	if (EFI_ERROR(ret)) {
		Print(L"PE format is invalid.\n");
		goto exit_tcb;
	}

	/* Extract the certificate/signature section address. */
	UINT8 *cert_data;
	UINT64 cert_size;

	ret = sl_get_cert_entry(tcb_tmp_file, &cert_data, &cert_size);
	if (EFI_ERROR(ret)) {
		Print(L"Can't get cert pointers\n");
		goto exit_tcb;
	}

	UINT64 cert_pages = cert_size / 4096 + 1;

	/* Allocate a buffer for Secure Launch procecss. */

	EFI_PHYSICAL_ADDRESS buf_phys = 0;
	UINT64 buf_pages = 27 + cert_pages + 3;

	ret = AllocateZeroPages(buf_pages, &buf_phys);
	if (EFI_ERROR(ret))
		goto exit_tcb;

	Dbg(L"Allocated %d pages at 0x%x (data, cert pages = %d)\n", buf_pages, buf_phys, cert_pages);

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

	struct sl_smc_params *smc_data = (struct sl_smc_params *)(buf + 4096 * 1);
	struct sl_tz_data    *tz_data  =    (struct sl_tz_data *)(buf + 4096 * 2);

	tz_data->version = 1;
	tz_data->cert_offt = 4096 * 25;
	tz_data->cert_size = cert_size;

	UINT8 *buf_cert_data = (UINT8*)tz_data + tz_data->cert_offt;
	CopyMem(buf_cert_data, cert_data, cert_size);

	/* Don't need raw PE anymore. */
	FreePool(tcb_tmp_file);

	tz_data->tcg_offt = tz_data->cert_offt + cert_size;
	tz_data->tcg_size = 4096 * 2;
	tz_data->tcg_used = 0;
	tz_data->tcg_ver = 2;

	tz_data->this_size = 4096 * (buf_pages - 3);
	tz_data->this_phys = (uint64_t)tz_data;

	tz_data->crt_offt = 4096 * 1;
	tz_data->crt_pages_cnt = 24;

	/* Set up return code path for when tcblaunch.exe fails to start */

	// FIXME: Probably better to just add an extra section into the PE.
	tz_data->tb_entry_point = (uint64_t)tb_entry;
	tz_data->tb_virt = (tz_data->tb_entry_point & 0xfffffffffffff000);
	tz_data->tb_phys = tz_data->tb_virt;
	tz_data->tb_size = 4096 * 2;
	tz_data->tb_data.mair = (uint64_t)tb_jmp_buf;

	Dbg(L"TB entrypoint is 0x%x, Image is at 0x%x, size= 0x%x, data[0]= 0x%x\n",
		tz_data->tb_entry_point, tz_data->tb_virt, tz_data->tb_size, tz_data->tb_data.mair);

	/* Allocate (bogus) boot parameters for tcb. */

	EFI_PHYSICAL_ADDRESS bootparams_phys = 0;
	UINT64 bootparams_pages = 3;

	ret = AllocateZeroPages(bootparams_pages, &bootparams_phys);
	if (EFI_ERROR(ret))
		goto exit_buf;

	Dbg(L"Allocated %d pages at 0x%x (bootparams)\n", bootparams_pages, bootparams_phys);

	struct sl_boot_params *bootparams = (struct sl_boot_params *)bootparams_phys;
	/*
	 * We don't really care what's in bootparams as long as it's garbage.
	 * Setting it all to 0xFF would guarantee the sanity checks to fail
	 * in tcblaunch.exe and make it transition back into whoever started it.
	 */
	SetMem(bootparams, 4096 * bootparams_pages, 0xff);

	tz_data->boot_params = (uint64_t)bootparams;
	tz_data->boot_params_size = 4096 * bootparams_pages;

	*smcdata  = smc_data;
	*pe_data  = (uint64_t)tcb_data;
	*pe_size  = 4096 * tcb_pages;
	*arg_data = tz_data->this_phys;
	*arg_size = tz_data->this_size;

	/* Do some sanity checks */

	/* mssecapp.mbn */
	ASSERT(*arg_data != 0);
	ASSERT(*arg_size > 0x17);
	ASSERT(*pe_data != 0);
	ASSERT(*pe_size != 0);

	PIMAGE_DOS_HEADER pe = (PIMAGE_DOS_HEADER)*pe_data;
	PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)((UINT8 *)pe + pe->e_lfanew);
	ASSERT(pe->e_magic == IMAGE_DOS_SIGNATURE);
	ASSERT(nt->Signature == IMAGE_NT_SIGNATURE);
	ASSERT(nt->OptionalHeader.Magic == 0x20b);

	ASSERT(tz_data->version == 1);
	ASSERT(tz_data->cert_offt > 0x17);
	ASSERT(tz_data->cert_size != 0);
	ASSERT(tz_data->this_size > tz_data->cert_offt);
	ASSERT(tz_data->this_size - tz_data->cert_offt >= tz_data->cert_size);
	ASSERT(tz_data->tcg_offt > 0x17);
	ASSERT(tz_data->tcg_size != 0);
	ASSERT(tz_data->this_size > tz_data->tcg_offt);
	ASSERT(tz_data->this_size - tz_data->tcg_offt >= tz_data->tcg_size);

	/* tcblaunch.exe */
	ASSERT(tz_data->tb_entry_point != 0);
	ASSERT(tz_data->tb_virt == tz_data->tb_phys);
	ASSERT(tz_data->tb_size > 0);

	/* Leftovers from winload.efi doing SL */
	ASSERT(sizeof(struct sl_tz_data) == 0xc8);
	ASSERT(tz_data->tcg_size == 0x2000);
	ASSERT(tz_data->tcg_used == 0x0);
	ASSERT(tz_data->tcg_ver == 0x2);
	ASSERT(tz_data->crt_offt == 0x1000);
	ASSERT(tz_data->crt_pages_cnt == 0x18);
	ASSERT(tz_data->boot_params_size == 0x3000);

	/* These depend on tcblaunch.exe from 22H2 */
	//ASSERT(tz_data->cert_offt == 0x19000);
	//ASSERT(tz_data->cert_size == 0x4030);
	//ASSERT(tz_data->tcg_offt == 0x01d030);
	//ASSERT(tz_data->this_size == 0x20000);

	clear_dcache_range((uint64_t)tcb_data, 4096 * tcb_pages);
	clear_dcache_range((uint64_t)buf, 4096 * buf_pages);
	clear_dcache_range((uint64_t)bootparams, 4096 * bootparams_pages);

	return EFI_SUCCESS;

exit_bp:
	FreePages(bootparams_phys, bootparams_pages);

exit_buf:
	FreePages(buf_phys, buf_pages);

exit_tcb:
	FreePages(tcb_phys, tcb_pages);
	FreePool(tcb_tmp_file);
exit:
	return ret;
}
