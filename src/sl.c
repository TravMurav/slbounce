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
#include "tzlog.h"
#include "sl.h"

#include "smp.h"

#include "tinyfb.h"

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

	//cert->wRevision = 0xDEAD;
	//cert[1].wRevision = 0xDEAD;

	return EFI_SUCCESS;
}

EFI_STATUS sl_load_pe(UINT8 *load_addr, UINT64 load_size, PIMAGE_DOS_HEADER pe, UINT64 pe_size)
{
	if (pe->e_magic != IMAGE_DOS_SIGNATURE)
		return EFI_INVALID_PARAMETER;

	PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)((UINT8 *)pe + pe->e_lfanew);

	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return EFI_INVALID_PARAMETER;

	if (nt->OptionalHeader.Magic != 0x20b)
		return EFI_INVALID_PARAMETER;

	SetMem(load_addr, load_size, 0);

	CopyMem(load_addr, pe, 0x400); // Header

	PIMAGE_SECTION_HEADER headers = (UINT8*)nt + 0x108; // FIXME don't hardcode this?
	UINT64 header_count = nt->FileHeader.NumberOfSections;

	// FIXME this should probably handle errors...
	for (int i = 0; i < header_count; ++i) {
		Print(L"Loading section '%a'\n", headers[i].Name);
		ASSERT(headers[i].VirtualAddress + headers[i].SizeOfRawData < load_size);

		CopyMem(load_addr + headers[i].VirtualAddress,
			(UINT8*)pe + headers[i].PointerToRawData,
			headers[i].SizeOfRawData); // I hope rawdata size is correct, this is how much is hashed...
	}

	return EFI_SUCCESS;
}

static void dump_smc_params(struct sl_smc_params *dat)
{
	Print(L"SMC Params: [%d/%d/0x%x] id=%d | pe: 0x%x (0x%x b) | arg: 0x%x (0x%x b)\n",
		dat->a, dat->b, dat->version, dat->num, dat->pe_data, dat->pe_size, dat->arg_data, dat->arg_size);
}

static int sl_reserve_dma_region()
{
	EFI_STATUS ret = EFI_SUCCESS;
	uint64_t smcret = 0;
	EFI_PHYSICAL_ADDRESS dummy_phys = 0, smc_phys = 0;

	ret = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, 1, &dummy_phys);
	if (EFI_ERROR(ret))
		return ret;
	Print(L"Allocated %d pages at 0x%x (dummy protect))\n", 1, dummy_phys);

	ret = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, 2, &smc_phys);
	if (EFI_ERROR(ret))
		return ret;
	Print(L"Allocated %d pages at 0x%x (protect smc))\n", 2, smc_phys);

	struct sl_smc_dma_params *smc_data = (void*)smc_phys;
	struct sl_smc_dma_entry *table =  (void*)(smc_phys+4096);
	SetMem((UINT8*)smc_phys, 4096 * 2, 0);

	smc_data->a = 1;
	smc_data->b = 0;
	smc_data->version = 0x10;
	smc_data->num = SL_CMD_RESERVE_MEM;

	smc_data->count = 1; // 1
	smc_data->entry_size = 0x28;
	smc_data->unk_2 = 2; // 2
	smc_data->table_offt = 4096; // 0x38

	table->phys = dummy_phys;
	table->virt = dummy_phys;
	table->size = 4096;
	table->perm = 6;
	table->flags = 1;

	clear_dcache_range((uint64_t)smc_data, 4096 * 2);

	Print(L" == Reserve: ");
	smcret = smc(SMC_SL_ID, (uint64_t)smc_data, smc_data->num, 0);
	Print(L"0x%x\n", smcret);

	return smcret;
}

EFI_STATUS sl_bounce(EFI_FILE_HANDLE tcblaunch, EFI_HANDLE ImageHandle)
{
	EFI_STATUS ret = EFI_SUCCESS;
	uint64_t smcret = 0;

	/* Allocate and load the tcblaunch.exe file. */

	UINT64 tcb_size = FileSize(tcblaunch);
	UINT64 tcb_pages = 512; // FIXME: don't hardcode...
	EFI_PHYSICAL_ADDRESS tcb_phys = 0;

	ret = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, tcb_pages, &tcb_phys);
	if (EFI_ERROR(ret))
		goto exit;

	Print(L"Allocated %d pages at 0x%x (for %d bytes in file)\n", tcb_pages, tcb_phys, tcb_size);

	UINT8 *tcb_data = (UINT8 *)tcb_phys;

	SetMem(tcb_data, 4096 * tcb_pages, 0);

	UINT8 *tcb_tmp_file = 0;

	ret = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, tcb_size, &tcb_tmp_file);
	if (EFI_ERROR(ret)) {
		Print(L"Can't allocate pool\n");
		goto exit_tcb;
	}

	UINT64 read_tcb_size = FileRead(tcblaunch, tcb_tmp_file, tcb_size);
	ASSERT(read_tcb_size == tcb_size);

	/* Load the PE into memory */
	ret = sl_load_pe(tcb_data, tcb_pages * 4096, tcb_tmp_file, tcb_size);
	if (EFI_ERROR(ret)) {
		Print(L"Can't load PE\n");
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

	EFI_PHYSICAL_ADDRESS buf_phys = 0; // 0x9479c000
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
	smc_data->pe_size = 4096 * tcb_pages;

	struct sl_tz_data *tz_data = (struct sl_tz_data *)(buf + 4096 * 2);
	SetMem(tz_data, 4096, 0);

	tz_data->version = 1;
	tz_data->cert_offt = 4096 * 25;
	tz_data->cert_size = cert_size;

	UINT8 *buf_cert_data = (UINT8*)tz_data + tz_data->cert_offt;
	CopyMem(buf_cert_data, cert_data, cert_size);

	uefi_call_wrapper(BS->FreePool, 1, tcb_tmp_file); // Don't need the raw pe file anymore...


	tz_data->tcg_offt = tz_data->cert_offt + cert_size;
	tz_data->tcg_size = 4096 * 2;
	tz_data->tcg_used = 0;
	tz_data->tcg_ver = 2;

	tz_data->this_size = 4096 * (buf_pages - 3);
	tz_data->this_phys = (uint64_t)tz_data;

	tz_data->crt_offt = 4096 * 1;
	tz_data->crt_pages_cnt = 24; // 24

	/* Set up return code path for when tcblaunch.exe fails to start */

	EFI_LOADED_IMAGE *image = NULL;
	EFI_GUID lipGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	ret = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &lipGuid, (void **) &image);
	if (EFI_ERROR(ret))
		goto exit_buf;

	tz_data->tb_entry_point = (uint64_t)_asm_tb_entry;
	tz_data->tb_virt = (uint64_t)image->ImageBase;
	tz_data->tb_phys = (uint64_t)image->ImageBase;
	tz_data->tb_size = image->ImageSize;

	tz_data->tb_virt = (tz_data->tb_entry_point & 0xfffffffffffff000);
	tz_data->tb_phys = tz_data->tb_virt;
	tz_data->tb_size = 4096 * 2;

	Print(L"TB entrypoint is 0x%x, Image is at 0x%x, size= 0x%x\n",
		tz_data->tb_entry_point, tz_data->tb_virt, tz_data->tb_size);

	/* Allocate (bogus) boot parameters for tcb. */

	EFI_PHYSICAL_ADDRESS bootparams_phys = 0;
	UINT64 bootparams_pages = 3;

	ret = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, bootparams_pages, &bootparams_phys);
	if (EFI_ERROR(ret))
		goto exit_buf;

	Print(L"Allocated %d pages at 0x%x (bootparams)\n", bootparams_pages, bootparams_phys);

	struct sl_boot_params *bootparams = (struct sl_boot_params *)bootparams_phys;
	SetMem(bootparams, 4096 * bootparams_pages, 0xff);

	tz_data->boot_params = (uint64_t)bootparams;
	tz_data->boot_params_size = 4096 * bootparams_pages;

	uint64_t arg_data = tz_data->this_phys;
	uint64_t arg_size = tz_data->this_size;

	smc_data->a = 1;
	smc_data->b = 0;
	smc_data->version = 0x10;
	smc_data->num = 0;
	smc_data->pe_data = (uint64_t)tcb_data;
	smc_data->pe_size = 4096 * tcb_pages;
	smc_data->arg_data = arg_data;
	smc_data->arg_size = arg_size;



	/* Do some sanity checks */

	/* mssecapp.mbn */
	ASSERT(smc_data->arg_data !=0);
	ASSERT(smc_data->arg_size > 0x17);
	ASSERT(smc_data->pe_data != 0);
	ASSERT(smc_data->pe_size != 0);

	PIMAGE_DOS_HEADER pe = (PIMAGE_DOS_HEADER)smc_data->pe_data;
	ASSERT(pe->e_magic == IMAGE_DOS_SIGNATURE);

	PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)((UINT8 *)pe + pe->e_lfanew);
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

	/* Leftover from real SL */
	ASSERT(sizeof(struct sl_tz_data) == 0xc8);
	ASSERT(tz_data->cert_offt == 0x19000);
	ASSERT(tz_data->cert_size == 0x4030);
	ASSERT(tz_data->tcg_offt == 0x01d030);
	ASSERT(tz_data->tcg_size == 0x2000);
	ASSERT(tz_data->tcg_used == 0x0);
	ASSERT(tz_data->tcg_ver == 0x2);
	ASSERT(tz_data->this_size == 0x20000);
	ASSERT(tz_data->crt_offt == 0x1000);
	ASSERT(tz_data->crt_pages_cnt == 0x18);
	ASSERT(tz_data->boot_params_size == 0x3000);


	//goto exit_bp; // <===== FIXME ======================


	//tz_data->version = 2;
	//tz_data->cert_offt = 2;
	//smc_data->arg_size = tz_data->this_size = 0x10;

	//tz_data->tcg_size = 0x15;

	//pe->e_csum = 0x1;
	//nt->FileHeader.NumberOfSections = 8;

	//register_qhee_logs();

	//dump_hyp_logs();
	//dump_tz_logs();
	//dump_qhee_logs();

	/*

	EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;

	ret = uefi_call_wrapper(BS->LocateProtocol, 3, &gopGuid, NULL, (void**)&gop);
	if(EFI_ERROR(ret))
		Print(L"Unable to locate GOP\n");

	Print(L"Framebuffer address %x size %d, width %d height %d pixelsperline %d\n",
			gop->Mode->FrameBufferBase,
			gop->Mode->FrameBufferSize,
			gop->Mode->Info->HorizontalResolution,
			gop->Mode->Info->VerticalResolution,
			gop->Mode->Info->PixelsPerScanLine
	       );

	uint32_t *fb = (uint32_t *)gop->Mode->FrameBufferBase; // 0x9bc00000

	SetMem(fb, (1920*4*1000), 0);

	*fb = 0xFFFFFFFF;
	put_hex(0x0123456789abcdef, &fb, gop->Mode->Info->HorizontalResolution);

	// =========================================================================================================
	EFI_MEMORY_DESCRIPTOR MemoryMap[64];
	UINTN MemoryMapSize = sizeof(MemoryMap);
	UINTN MapKey = 0;
	UINTN DescriptorSize;
	UINT32 DescriptorVersion;

	ret = uefi_call_wrapper(BS->GetMemoryMap, 6, &MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorSize);
	put_hex(ret, &fb, gop->Mode->Info->HorizontalResolution);
	ret = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, MapKey);
	put_hex(ret, &fb, gop->Mode->Info->HorizontalResolution);

	//smcret = spin_up_second_cpu();

	clear_dcache_range((uint64_t)tcb_data, 4096 * tcb_pages);
	clear_dcache_range((uint64_t)buf, 4096 * buf_pages);
	clear_dcache_range((uint64_t)bootparams, 4096 * bootparams_pages);
	smc_data->a = 1;
	smc_data->b = 0;
	smc_data->version = 0x10;
	smc_data->num = 0;
	smc_data->pe_data = (uint64_t)tcb_data;
	smc_data->pe_size = 4096 * tcb_pages;
	smc_data->arg_data = arg_data;
	smc_data->arg_size = arg_size;
	smc_data->num = SL_CMD_IS_AVAILABLE;
	clear_dcache_range((uint64_t)smc_data, 4096 * 1);

	put_hex(smc_data->num, &fb, gop->Mode->Info->HorizontalResolution);
	smcret = smc(SMC_SL_ID, (uint64_t)smc_data, smc_data->num, 0);
	put_hex(smcret, &fb, gop->Mode->Info->HorizontalResolution);

	smc_data->a = 1;
	smc_data->b = 0;
	smc_data->version = 0x10;
	smc_data->num = 0;
	smc_data->pe_data = (uint64_t)tcb_data;
	smc_data->pe_size = 4096 * tcb_pages;
	smc_data->arg_data = arg_data;
	smc_data->arg_size = arg_size;
	smc_data->num = SL_CMD_AUTH;
	clear_dcache_range((uint64_t)smc_data, 4096 * 1);

	put_hex(smc_data->num, &fb, gop->Mode->Info->HorizontalResolution);
	smcret = smc(SMC_SL_ID, (uint64_t)smc_data, smc_data->num, 0);
	put_hex(smcret, &fb, gop->Mode->Info->HorizontalResolution);

	smc_data->a = 1;
	smc_data->b = 0;
	smc_data->version = 0x10;
	smc_data->num = 0;
	smc_data->pe_data = (uint64_t)tcb_data;
	smc_data->pe_size = 4096 * tcb_pages;
	smc_data->arg_data = arg_data;
	smc_data->arg_size = arg_size;
	smc_data->num = SL_CMD_LAUNCH;
	clear_dcache_range((uint64_t)smc_data, 4096 * 1);

	put_hex(smc_data->num, &fb, gop->Mode->Info->HorizontalResolution);
	smcret = smc(SMC_SL_ID, (uint64_t)smc_data, smc_data->num, 0);
	put_hex(smcret, &fb, gop->Mode->Info->HorizontalResolution);

	smc_data->num = 0;
	clear_dcache_range((uint64_t)smc_data, 4096 * 1);

	put_hex(smc_data->num, &fb, gop->Mode->Info->HorizontalResolution);
	smcret = smc(SMC_SL_ID, (uint64_t)smc_data, smc_data->num, 0);
	put_hex(smcret, &fb, gop->Mode->Info->HorizontalResolution);

	while (1)
		;

	*/

	//goto exit_bp; // ===========================================================================================

	/* Perform the SMC calls to launch the tcb with our data */

	Print(L"Data creation is done. Trying to bounce...\n");

	//Print(L"Trying to boot second core! ret = ");
	//smcret = spin_up_second_cpu();
	//while(1)
	//	;
	//Print(L"0x%x\n", smcret);

	dump_smc_params(smc_data);

	clear_dcache_range((uint64_t)tcb_data, 4096 * tcb_pages);
	clear_dcache_range((uint64_t)buf, 4096 * buf_pages);
	clear_dcache_range((uint64_t)bootparams, 4096 * bootparams_pages);

	smc_data->num = SL_CMD_IS_AVAILABLE;
	clear_dcache_range((uint64_t)smc_data, 4096 * 1);

	EFI_TPL OldTpl;

	//smcret = sl_reserve_dma_region();
	//if (smcret)
	//	goto exit_corrupted;

	Print(L" == Available: ");
	//OldTpl = uefi_call_wrapper(BS->RaiseTPL, 1, TPL_HIGH_LEVEL);
	smcret = smc(SMC_SL_ID, (uint64_t)smc_data, smc_data->num, 0);
	//uefi_call_wrapper(BS->RestoreTPL, 1, OldTpl);
	Print(L"0x%x\n", smcret);
	if (smcret)
		goto exit_corrupted;

	smc_data->a = 1;
	smc_data->b = 0;
	smc_data->version = 0x10;
	smc_data->num = 0;
	smc_data->pe_data = (uint64_t)tcb_data;
	smc_data->pe_size = 4096 * tcb_pages;
	smc_data->arg_data = arg_data;
	smc_data->arg_size = arg_size;
	dump_smc_params(smc_data);

	smc_data->num = SL_CMD_AUTH;
	clear_dcache_range((uint64_t)smc_data, 4096 * 1);

	Print(L" == Auth: ");
	//OldTpl = uefi_call_wrapper(BS->RaiseTPL, 1, TPL_HIGH_LEVEL);
	smcret = smc(SMC_SL_ID, (uint64_t)smc_data, smc_data->num, 0);
	//uefi_call_wrapper(BS->RestoreTPL, 1, OldTpl);
	Print(L"0x%x\n", smcret);
	if (smcret)
		goto exit_corrupted;

	smc_data->a = 1;
	smc_data->b = 0;
	smc_data->version = 0x10;
	smc_data->num = 0;
	smc_data->pe_data = (uint64_t)tcb_data;
	smc_data->pe_size = 4096 * tcb_pages;
	smc_data->arg_data = arg_data;
	smc_data->arg_size = arg_size;
	dump_smc_params(smc_data);

	smc_data->num = SL_CMD_LAUNCH;
	//smc_data->num = SL_CMD_UNMAP;
	clear_dcache_range((uint64_t)smc_data, 4096 * 1);

	Print(L" == Launch: ");
	//OldTpl = uefi_call_wrapper(BS->RaiseTPL, 1, TPL_NOTIFY);
	smcret = smc(SMC_SL_ID, (uint64_t)smc_data, smc_data->num, 0);
	//uefi_call_wrapper(BS->RestoreTPL, 1, OldTpl);
	Print(L"0x%x\n", smcret);

	dump_smc_params(smc_data);

	//if (smcret)
		goto exit_corrupted;

	Print(L"Bounce is done. Cleaning up.\n");

exit_bp:
	uefi_call_wrapper(BS->FreePages, 2, bootparams_phys, bootparams_pages);

exit_buf:
	uefi_call_wrapper(BS->FreePages, 2, buf_phys, buf_pages);

exit_tcb:
	uefi_call_wrapper(BS->FreePages, 2, tcb_phys, tcb_pages);
exit:
	return ret;

exit_corrupted:
	Print(L"===========================================\n");
	Print(L"      SMC failed with ret = 0x%x\n", smcret);
	Print(L" Assume this system is in corrupted state!\n");
	Print(L"===========================================\n");

	Print(L" == Available: ");
	smcret = smc(SMC_SL_ID, (uint64_t)smc_data, SL_CMD_IS_AVAILABLE, 0);
	Print(L"0x%x\n", smcret);

	//uefi_call_wrapper(BS->Stall, 1, 5000000);

	//dump_hyp_logs();
	//dump_tz_logs();
	//dump_qhee_logs();

	/* Sanity check that SMC works */
	uint64_t psci_version = smc(0x84000000, 0, 0, 0);
	Print(L"PSCI version = 0x%x\n", psci_version);


	while(1)
		;

	return EFI_UNSUPPORTED;
}
